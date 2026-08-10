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

  test "ago_compact formats recent and multi-day ages" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      e = Exposure.new(user: users(:one), started_at: 30.seconds.ago, duration_seconds: 30)
      assert_equal "just now", e.ago_compact

      e.started_at = 45.minutes.ago
      assert_equal "45m", e.ago_compact

      e.started_at = 22.hours.ago
      assert_equal "22h", e.ago_compact

      e.started_at = (1.day + 22.hours).ago
      assert_equal "1d 22h", e.ago_compact

      e.started_at = 3.days.ago
      assert_equal "3d", e.ago_compact
    end
  end

  test "last_session_message_for uses newest exposure" do
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      user = users(:one)
      user.exposures.destroy_all
      assert_equal "No prior session", Exposure.last_session_message_for(user)

      Exposure.create!(user: user, started_at: 2.days.ago, duration_seconds: 60)
      assert_equal "Last session 2d ago", Exposure.last_session_message_for(user)

      Exposure.create!(user: user, started_at: 90.minutes.ago, duration_seconds: 45)
      assert_equal "Last session 1h ago", Exposure.last_session_message_for(user)
    end
  end
end
