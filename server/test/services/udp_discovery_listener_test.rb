require "test_helper"
require "json"

class UdpDiscoveryListenerTest < ActiveSupport::TestCase
  setup do
    @listener = UdpDiscoveryListener.new(port: 0, logger: Logger.new(File::NULL))
  end

  test "parse_message json ping" do
    parsed = UdpDiscoveryListener.parse_message(
      %({"v":1,"type":"ping","identity":"esp-mac"})
    )
    assert_equal :ping, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
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

  test "handle_packet ignores pong" do
    before = Device.count
    assert_nil @listener.handle_packet(
      UdpDiscoveryListener.build_pong(identity: "other", ip: "1.2.3.4"),
      "192.168.50.20"
    )
    assert_equal before, Device.count
  end

  test "build_ping format" do
    msg = JSON.parse(UdpDiscoveryListener.build_ping(identity: "esp-x"))
    assert_equal "ping", msg["type"]
    assert_equal "esp-x", msg["identity"]
    assert_equal 1, msg["v"]
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
    reply = @listener.handle_packet(
      UdpDiscoveryListener.build_therapy_request(identity: "esp-therapy", user_id: user.id),
      "192.168.50.40"
    )
    data = JSON.parse(reply)
    assert_equal "therapy", data["type"]
    assert_equal user.id, data["user_id"]
    assert_equal user.name, data["name"]
    assert_equal UdpDiscoveryListener::DEFAULT_RECOMMENDED_SECONDS, data["recommended_seconds"]
    assert_equal 30, data["recommended_seconds"]
    assert_not data.key?("error")

    device = Device.find_by!(identity: "esp-therapy")
    assert_equal "192.168.50.40", device.ip
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
  end

  test "handle_packet therapy bad_user_id for negative" do
    reply = @listener.handle_packet(
      %({"v":1,"type":"therapy","identity":"esp-x","user_id":-1}),
      "192.168.50.42"
    )
    data = JSON.parse(reply)
    assert_equal "bad_user_id", data["error"]
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
