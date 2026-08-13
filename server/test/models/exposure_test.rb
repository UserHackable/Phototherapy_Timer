require "test_helper"

class ExposureTest < ActiveSupport::TestCase
  test "valid fixture" do
    assert exposures(:one).valid?
  end

  test "belongs to user" do
    assert_equal users(:one), exposures(:one).user
  end

  test "requires started_at" do
    exposure = Exposure.new(user: users(:one), duration_seconds: 30)
    assert_not exposure.valid?
    assert_includes exposure.errors[:started_at], "can't be blank"
  end

  test "requires positive duration" do
    exposure = Exposure.new(user: users(:one), started_at: Time.current, duration_seconds: 0)
    assert_not exposure.valid?
    assert_includes exposure.errors[:duration_seconds], "must be greater than 0"
  end

  test "rejects negative duration" do
    exposure = Exposure.new(user: users(:one), started_at: Time.current, duration_seconds: -5)
    assert_not exposure.valid?
  end

  test "ended_at is started_at plus duration" do
    started = Time.zone.parse("2026-07-24 10:00:00")
    exposure = Exposure.new(user: users(:one), started_at: started, duration_seconds: 90)
    assert_equal started + 90.seconds, exposure.ended_at
  end

  test "duration_mmss formats minutes and seconds" do
    exposure = Exposure.new(duration_seconds: 95)
    assert_equal "1:35", exposure.duration_mmss
  end

  test "therapy_label includes skin type when present" do
    exposure = Exposure.new(therapy_type: therapy_types(:psoriasis), skin_type: skin_types(:one))
    assert_equal "Psoriasis — Type I", exposure.therapy_label
    assert_equal "Eczema", Exposure.new(therapy_type: therapy_types(:eczema)).therapy_label
    assert_nil Exposure.new.therapy_label
  end

  test "newest_first orders by started_at descending" do
    base = Time.zone.parse("2026-01-01 12:00:00")
    older = Exposure.create!(user: users(:one), started_at: base, duration_seconds: 30)
    newer = Exposure.create!(user: users(:one), started_at: base + 1.hour, duration_seconds: 45)
    ordered = users(:one).exposures.newest_first.where(id: [ older.id, newer.id ]).to_a
    assert_equal [ newer, older ], ordered
  end

  test "destroying user destroys exposures" do
    user = User.create!(
      name: "temp",
      email_address: "temp-exposure@example.com",
      password: "password",
      password_confirmation: "password"
    )
    Exposure.create!(user: user, started_at: Time.current, duration_seconds: 60)
    assert_difference("Exposure.count", -1) { user.destroy! }
  end

  test "ago_dhm skips zero units" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      e = Exposure.new(user: users(:one), started_at: Time.zone.now, duration_seconds: 90)
      assert_equal "just now", e.ago_dhm

      e.started_at = 45.minutes.ago
      assert_equal "45m", e.ago_dhm

      e.started_at = (10.hours + 8.minutes).ago
      assert_equal "10h 8m", e.ago_dhm

      e.started_at = (2.days + 3.hours).ago
      assert_equal "2d 3h", e.ago_dhm
    end
  end

  test "last_session_detail_line fits 16 columns" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      e = Exposure.new(
        user: users(:one),
        started_at: (10.hours + 8.minutes).ago,
        duration_seconds: 105
      )
      line = e.last_session_detail_line
      assert_operator line.length, :<=, 16
      assert_match(/1:45/, line)
      assert_match(/10h/, line)
      assert_match(/ago/, line)
      assert_no_match(/0d/, line)
    end
  end

  test "recommended_seconds_for uses default when user has no exposures" do
    assert_equal 30, Exposure.recommended_seconds_for(users(:fresh), default_seconds: 30)
  end

  test "recommended_seconds_for is zero when last exposure is recent" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      Exposure.create!(user: users(:fresh), started_at: 10.hours.ago, duration_seconds: 105)
      assert_equal 0, Exposure.recommended_seconds_for(users(:fresh), default_seconds: 30)
    end
  end

  test "recommended_seconds_for uses last duration after 44 hours" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      user = users(:fresh)
      Exposure.create!(user: user, started_at: (44.hours + 1.minute).ago, duration_seconds: 105)
      assert_equal 105, Exposure.recommended_seconds_for(user, default_seconds: 30)
    end
  end

  test "recommended_seconds_for uses last duration after 50 hours" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      Exposure.create!(user: users(:fresh), started_at: 50.hours.ago, duration_seconds: 90)
      assert_equal 90, Exposure.recommended_seconds_for(users(:fresh), default_seconds: 30)
    end
  end

  test "last_duration_seconds_for returns newest exposure duration" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      user = users(:fresh)
      assert_nil Exposure.last_duration_seconds_for(user)
      assert_nil Exposure.last_duration_seconds_for(nil)

      Exposure.create!(user: user, started_at: 2.days.ago, duration_seconds: 60)
      Exposure.create!(user: user, started_at: 10.hours.ago, duration_seconds: 105)
      assert_equal 105, Exposure.last_duration_seconds_for(user)
    end
  end

  test "last_session_message_for is two LCD lines" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      user = users(:fresh)
      assert_equal "No prior session", Exposure.last_session_message_for(user)

      Exposure.create!(user: user, started_at: (10.hours + 8.minutes).ago, duration_seconds: 105)
      msg = Exposure.last_session_message_for(user)
      lines = msg.split("\n", 2)
      assert_equal "Last session", lines[0]
      assert_operator lines[1].length, :<=, 16
      assert_match(/1:45/, lines[1])
    end
  end
end
