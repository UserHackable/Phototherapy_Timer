require "test_helper"
require "json"
require "fileutils"

class UdpDiscoveryListenerTest < ActiveSupport::TestCase
  setup do
    @listener = UdpDiscoveryListener.new(port: 0, logger: Logger.new(File::NULL))
  end

  test "parse_message json ping" do
    parsed = UdpDiscoveryListener.parse_message(
      %({"v":1,"type":"ping","identity":"esp-mac","app":"session_timer","version":"99eab52"})
    )
    assert_equal :ping, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
    assert_equal "session_timer", parsed[:app]
    assert_equal "99eab52", parsed[:version]
  end

  test "parse_message json pong with time" do
    parsed = UdpDiscoveryListener.parse_message(
      %({"v":1,"type":"pong","identity":"host","ip":"1.2.3.4","unix":1710000000,"iso8601":"2024-03-09T00:00:00Z"})
    )
    assert_equal :pong, parsed[:type]
    assert_equal "host", parsed[:identity]
    assert_equal "1.2.3.4", parsed[:ip]
    assert_equal 1710000000, parsed[:unix]
  end

  test "parse_message legacy text ping still works" do
    parsed = UdpDiscoveryListener.parse_message("PHOTOTHERAPY/1 PING identity=esp-mac")
    assert_equal :ping, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
  end

  test "parse_message ignores garbage" do
    assert_nil UdpDiscoveryListener.parse_message("hello world")
    assert_nil UdpDiscoveryListener.parse_message("{not json")
  end

  test "handle_packet upserts device and returns json pong with unix time and timezone" do
    pong = @listener.handle_packet(
      UdpDiscoveryListener.build_ping(identity: "unit-test-esp"),
      "192.168.50.20"
    )

    data = JSON.parse(pong)
    assert_equal 1, data["v"]
    assert_equal "pong", data["type"]
    assert data["ip"].present?
    assert data["unix"].is_a?(Integer)
    assert data["iso8601"].present?
    assert data["identity"].present?
    assert data["tz"].present?
    assert data["tz_offset"].is_a?(Integer)
    assert data["tz_posix"].present?

    device = Device.find_by!(identity: "unit-test-esp")
    assert_equal "192.168.50.20", device.ip
  end

  test "handle_packet ping stores firmware version and echoes published_version" do
    app_dir = Rails.root.join("storage", "firmware", "session_timer")
    FileUtils.mkdir_p(app_dir)
    manifest = app_dir.join("manifest.json")
    begin
      manifest.write({ "v" => 1, "app" => "session_timer", "version" => "cafebabe" }.to_json)

      pong = @listener.handle_packet(
        UdpDiscoveryListener.build_ping(
          identity: "esp-fw-ping",
          version: "99eab52",
          app: "session_timer"
        ),
        "192.168.50.21"
      )
      data = JSON.parse(pong)
      assert_equal "pong", data["type"]
      assert_equal "session_timer", data["app"]
      assert_equal "cafebabe", data["published_version"]

      device = Device.find_by!(identity: "esp-fw-ping")
      assert_equal "99eab52", device.firmware_version
      assert_equal "session_timer", device.firmware_app
    ensure
      manifest.delete if manifest.exist?
    end
  end

  test "handle_packet ping stores nested status" do
    pong = @listener.handle_packet(
      UdpDiscoveryListener.build_ping(
        identity: "esp-status-ping",
        app: "session_timer",
        version: "abc1234",
        status: {
          "state" => "clock",
          "user" => "Guest",
          "lcd" => [ "Guest      0:30", "2026-08-12 WedP" ],
          "led" => "07:15",
          "led_kind" => "clock"
        }
      ),
      "192.168.50.22"
    )
    assert pong.present?
    device = Device.find_by!(identity: "esp-status-ping")
    assert_equal "clock", device.last_status["state"]
    assert_equal "Guest", device.last_status["user"]
    assert_equal "07:15", device.last_status["led"]
    assert_equal "clock", device.last_status["led_kind"]
  end

  test "handle_packet status report stores display snapshot and sends no reply" do
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_status_report(
        identity: "esp-status-push",
        app: "session_timer",
        status: {
          "state" => "running",
          "user" => "rob",
          "entry" => "0:45",
          "remain_seconds" => 29,
          "lamp" => true,
          "fan" => true,
          "lcd" => [ "rob        0:29", "* abort  Running" ],
          "led" => "00:29",
          "led_kind" => "timer"
        }
      ),
      "192.168.50.23"
    )
    assert_nil reply
    device = Device.find_by!(identity: "esp-status-push")
    assert_equal "192.168.50.23", device.ip
    assert_equal "running", device.last_status["state"]
    assert_equal true, device.last_status["lamp"]
    assert_equal [ "rob        0:29", "* abort  Running" ], device.last_status["lcd"]
  end

  test "handle_packet ota forwards to the device ip" do
    user_device = devices(:one)
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_ota_request(identity: user_device.identity),
      "192.168.50.9"
    )
    data = JSON.parse(reply)
    assert_equal "ota", data["type"]
    assert_equal true, data["ok"]
    assert_equal true, data["forwarded"]
    assert_equal user_device.ip, data["ip"]
    assert_equal user_device.identity, data["identity"]
  end

  test "handle_packet ota ack is ignored" do
    assert_nil @listener.handle_packet(
      %({"v":1,"type":"ota","ok":true,"identity":"esp-one","version":"f65b788"}),
      "192.168.1.10"
    )
  end

  test "handle_packet ota unknown identity" do
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_ota_request(identity: "no-such-esp"),
      "192.168.50.9"
    )
    data = JSON.parse(reply)
    assert_equal false, data["ok"]
    assert_equal "not_found", data["error"]
  end

  test "handle_packet ignores pong" do
    before = Device.count
    assert_nil @listener.handle_packet(
      UdpDiscoveryListener.build_pong(identity: "other", ip: "1.2.3.4"),
      "192.168.50.20"
    )
    assert_equal before, Device.count
  end

  test "build_ping format" do
    msg = JSON.parse(UdpDiscoveryListener.build_ping(identity: "esp-x", version: "deadbee", app: "session_timer"))
    assert_equal "ping", msg["type"]
    assert_equal "esp-x", msg["identity"]
    assert_equal 1, msg["v"]
    assert_equal "deadbee", msg["version"]
    assert_equal "session_timer", msg["app"]
  end

  test "handle_packet users request returns id and name for seeded users" do
    # Fixtures provide users; ensure at least two exist with names.
    assert User.count >= 2

    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_users_request(identity: "esp-users-test"),
      "192.168.50.30"
    )
    data = JSON.parse(reply)
    assert_equal "users", data["type"]
    assert data["users"].is_a?(Array)
    assert data["users"].size <= 9
    data["users"].each do |u|
      assert u["id"].is_a?(Integer)
      assert u["name"].present?
      assert_not u.key?("email_address")
      assert_not u.key?("password_digest")
    end

    device = Device.find_by!(identity: "esp-users-test")
    assert_equal "192.168.50.30", device.ip
  end

  test "parse_message json therapy" do
    parsed = UdpDiscoveryListener.parse_message(
      %({"v":1,"type":"therapy","identity":"esp-mac","user_id":4})
    )
    assert_equal :therapy, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
    assert_equal 4, parsed[:user_id]
  end

  test "handle_packet therapy returns default recommended_seconds" do
    user = users(:one)
    # Fixture exposure exists for :one — expect last-session message.
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy", user_id: user.id),
      "192.168.50.40"
    )
    data = JSON.parse(reply)
    assert_equal "therapy", data["type"]
    assert_equal user.id, data["user_id"]
    assert_equal user.name, data["name"]
    assert data["recommended_seconds"].is_a?(Integer)
    assert data["recommended_seconds"] >= 0
    assert_equal 16, data["step_seconds"]
    assert_equal 333, data["max_seconds"]
    assert_equal 50, data["initial_seconds"]
    assert data["last_duration_seconds"].is_a?(Integer)
    assert data["last_duration_seconds"] >= 0
    assert_not data.key?("error")
    assert data["message"].present?
    assert_match(/Last session|No prior session/, data["message"])

    device = Device.find_by!(identity: "esp-therapy")
    assert_equal "192.168.50.40", device.ip
  end

  test "handle_packet therapy message reports last session age" do
    user = users(:one)
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      Exposure.where(user_id: user.id).delete_all
      Exposure.create!(
        user: user,
        started_at: Time.zone.parse("2026-08-10 02:16:00"),
        duration_seconds: 90
      )

      reply = @listener.handle_packet(
        UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy-age", user_id: user.id),
        "192.168.50.47"
      )
      data = JSON.parse(reply)
      lines = data["message"].split("\n", 2)
      assert_equal "Last session", lines[0]
      assert_match(/1:30/, lines[1])
      assert_no_match(/0d/, lines[1])
      assert_match(/ago/, lines[1])
      assert_operator lines[1].length, :<=, 16
      assert_equal 0, data["recommended_seconds"]
      assert_equal 16, data["step_seconds"]
      assert_equal 90, data["last_duration_seconds"]
    end
  end

  test "handle_packet therapy recommends zero when last exposure recent" do
    user = users(:one)
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      Exposure.where(user_id: user.id).delete_all
      Exposure.create!(user: user, started_at: 10.hours.ago, duration_seconds: 105)
      reply = @listener.handle_packet(
        UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy-recent", user_id: user.id),
        "192.168.50.50"
      )
      data = JSON.parse(reply)
      assert_equal 0, data["recommended_seconds"]
    end
  end

  test "handle_packet therapy recommends last duration after 44 hours" do
    user = users(:one)
    travel_to Time.zone.parse("2026-08-10 12:00:00") do
      Exposure.where(user_id: user.id).delete_all
      Exposure.create!(user: user, started_at: 50.hours.ago, duration_seconds: 105)
      reply = @listener.handle_packet(
        UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy-old", user_id: user.id),
        "192.168.50.49"
      )
      data = JSON.parse(reply)
      assert_equal 105, data["recommended_seconds"]
      assert_equal 16, data["step_seconds"]
      assert_equal 105, data["last_duration_seconds"]
    end
  end

  test "handle_packet therapy message when user has no exposures" do
    user = users(:one)
    Exposure.where(user_id: user.id).delete_all
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy-none", user_id: user.id),
      "192.168.50.48"
    )
    data = JSON.parse(reply)
    assert_equal "No prior session", data["message"]
    assert_equal 50, data["recommended_seconds"]
    assert_equal 16, data["step_seconds"]
    assert_equal 333, data["max_seconds"]
    assert_equal 50, data["initial_seconds"]
    assert_equal 0, data["last_duration_seconds"]
  end

  test "build_therapy_reply includes optional message for module LCD" do
    json = UdpDiscoveryListener.build_therapy_reply(
      user_id: 4,
      name: "miriam",
      recommended_seconds: 30,
      step_seconds: 16,
      max_seconds: 333,
      initial_seconds: 50,
      last_duration_seconds: 90,
      message: "Last session 2d ago"
    )
    data = JSON.parse(json)
    assert_equal "therapy", data["type"]
    assert_equal 4, data["user_id"]
    assert_equal "miriam", data["name"]
    assert_equal 30, data["recommended_seconds"]
    assert_equal 16, data["step_seconds"]
    assert_equal 333, data["max_seconds"]
    assert_equal 50, data["initial_seconds"]
    assert_equal 90, data["last_duration_seconds"]
    assert_equal "Last session 2d ago", data["message"]
  end

  test "handle_packet therapy not_found for unknown user" do
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy2", user_id: 999_999),
      "192.168.50.41"
    )
    data = JSON.parse(reply)
    assert_equal "therapy", data["type"]
    assert_equal 999_999, data["user_id"]
    assert_equal "not_found", data["error"]
    assert_not data.key?("recommended_seconds")
    assert_not data.key?("step_seconds")
    assert_not data.key?("max_seconds")
    assert_not data.key?("initial_seconds")
    assert_not data.key?("last_duration_seconds")
  end

  test "handle_packet therapy bad_user_id for negative" do
    reply = @listener.handle_packet(
      %({"v":1,"type":"therapy","identity":"esp-x","user_id":-1}),
      "192.168.50.42"
    )
    data = JSON.parse(reply)
    assert_equal "bad_user_id", data["error"]
  end

  test "handle_packet therapies returns keypad lists" do
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapies_request(identity: "esp-therapies"),
      "192.168.50.60"
    )
    data = JSON.parse(reply)
    assert_equal "therapies", data["type"]
    ids = data["therapies"].map { |t| t["id"] }
    assert_equal [ 1, 2, 4 ], ids
    manual = data["therapies"].find { |t| t["id"] == 1 }
    assert_equal "Manual", manual["name"]
    assert_equal false, manual["uses_skin_type"]
    psoriasis = data["therapies"].find { |t| t["id"] == 2 }
    assert_equal "Psoriasis", psoriasis["name"]
    assert_equal true, psoriasis["uses_skin_type"]
    eczema = data["therapies"].find { |t| t["id"] == 4 }
    assert_equal "Eczema", eczema["name"]
    assert_equal [ 1, 3 ], data["skin_types"].map { |s| s["id"] }
    assert_equal "Type I", data["skin_types"].first["name"]
    assert_equal "192.168.50.60", Device.find_by!(identity: "esp-therapies").ip
  end

  test "handle_packet assign_therapy sets manual for guest" do
    User.ensure_guest!
    guest = User.find(0)
    UserTherapy.where(user_id: 0).delete_all

    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_assign_therapy_request(
        identity: "esp-assign-manual", user_id: 0, therapy_id: 1
      ),
      "192.168.50.61"
    )
    data = JSON.parse(reply)
    assert_equal "assign_therapy", data["type"]
    assert_equal true, data["ok"]
    assert_equal 0, data["user_id"]
    assert_equal 1, data["therapy_id"]
    assignment = guest.user_therapies.newest_first.first
    assert_equal therapy_types(:manual), assignment.therapy_type
    assert_nil assignment.skin_type
    assert_equal 15, guest.therapy_step_seconds
    assert_equal 30, guest.therapy_initial_seconds
  end

  test "handle_packet assign_therapy requires skin for psoriasis" do
    user = users(:two)
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_assign_therapy_request(
        identity: "esp-assign-need-skin", user_id: user.id, therapy_id: 2
      ),
      "192.168.50.62"
    )
    data = JSON.parse(reply)
    assert_equal false, data["ok"]
    assert_equal "need_skin", data["error"]
  end

  test "handle_packet assign_therapy sets psoriasis skin type" do
    user = users(:two)
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_assign_therapy_request(
        identity: "esp-assign-pso", user_id: user.id, therapy_id: 2, skin_id: 3
      ),
      "192.168.50.63"
    )
    data = JSON.parse(reply)
    assert_equal true, data["ok"]
    assert_equal 2, data["therapy_id"]
    assert_equal 3, data["skin_id"]
    assignment = user.user_therapies.find_by!(therapy_type: therapy_types(:psoriasis))
    assert_equal skin_types(:three), assignment.skin_type
    assert_equal 20, user.therapy_step_seconds
    assert_equal 83, user.therapy_initial_seconds
    assert_equal 500, user.therapy_max_seconds
  end

  test "handle_packet therapy allows guest id 0" do
    User.ensure_guest!
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapy_request(identity: "esp-guest-th", user_id: 0),
      "192.168.50.42"
    )
    data = JSON.parse(reply)
    assert_equal "therapy", data["type"]
    assert_equal 0, data["user_id"]
    assert_equal "Guest", data["name"]
    assert_equal 30, data["recommended_seconds"]
    assert_equal 10, data["step_seconds"]
    assert_equal 1200, data["max_seconds"]
    assert_equal 30, data["initial_seconds"]
    assert data["last_duration_seconds"].is_a?(Integer)
    assert_not data.key?("error")
  end

  test "users list ends with guest id 0" do
    User.ensure_guest!
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_users_request(identity: "esp-guest-list"),
      "192.168.50.43"
    )
    data = JSON.parse(reply)
    ids = data["users"].map { |u| u["id"] }
    assert_includes ids, User::GUEST_ID
    assert_equal User::GUEST_ID, ids.last
    assert ids[0...-1].all? { |id| id > 0 }
  end

  test "handle_packet exposure logs for household user" do
    user = users(:one)
    ended = Time.zone.parse("2026-07-24 15:00:30")
    unix = ended.to_i
    duration = 30

    assert_difference("Exposure.count", 1) do
      reply = @listener.handle_packet(
        UdpDiscoveryListener.build_exposure_request(
          identity: "esp-exp",
          user_id: user.id,
          duration_seconds: duration,
          unix: unix
        ),
        "192.168.50.44"
      )
      data = JSON.parse(reply)
      assert_equal true, data["ok"]
      assert_equal user.id, data["user_id"]
      assert_equal duration, data["duration_seconds"]
      assert data["id"].present?
    end

    exposure = Exposure.order(:id).last
    assert_equal user.id, exposure.user_id
    assert_equal duration, exposure.duration_seconds
    assert_in_delta ended - duration.seconds, exposure.started_at, 1.second
  end

  test "handle_packet exposure logs for guest id 0" do
    User.ensure_guest!
    unix = Time.zone.parse("2026-07-24 16:00:00").to_i

    assert_difference("Exposure.count", 1) do
      reply = @listener.handle_packet(
        UdpDiscoveryListener.build_exposure_request(
          identity: "esp-guest-exp",
          user_id: 0,
          duration_seconds: 45,
          unix: unix
        ),
        "192.168.50.45"
      )
      data = JSON.parse(reply)
      assert_equal true, data["ok"]
      assert_equal 0, data["user_id"]
    end

    exposure = Exposure.order(:id).last
    assert_equal 0, exposure.user_id
    assert_equal 45, exposure.duration_seconds
  end

  test "handle_packet exposure rejects zero duration" do
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_exposure_request(
        identity: "esp-bad",
        user_id: users(:one).id,
        duration_seconds: 0,
        unix: Time.now.to_i
      ),
      "192.168.50.46"
    )
    data = JSON.parse(reply)
    assert_equal false, data["ok"]
    assert_equal "bad_duration", data["error"]
  end
end
