require "test_helper"
require "fileutils"

class DeviceTest < ActiveSupport::TestCase
  test "upsert_from_discovery creates by ip when identity missing" do
    device = Device.upsert_from_discovery!(ip: "10.0.0.5")
    assert_equal "10.0.0.5", device.ip
    assert_nil device.identity
    assert_equal 1, Device.where(ip: "10.0.0.5").count
  end

  test "upsert_from_discovery updates same identity when ip changes" do
    first = Device.upsert_from_discovery!(ip: "10.0.0.5", identity: "esp-abc")
    second = Device.upsert_from_discovery!(ip: "10.0.0.9", identity: "esp-abc")

    assert_equal first.id, second.id
    assert_equal "10.0.0.9", second.ip
    assert_equal "esp-abc", second.identity
    assert_equal 1, Device.where(identity: "esp-abc").count
  end

  test "upsert_from_discovery requires ip" do
    assert_raises(ArgumentError) { Device.upsert_from_discovery!(ip: "  ") }
  end

  test "upsert_from_discovery stores firmware version from ping" do
    device = Device.upsert_from_discovery!(
      ip: "10.0.0.5",
      identity: "esp-fw",
      firmware_version: "99eab52",
      firmware_app: "session_timer"
    )
    assert_equal "99eab52", device.firmware_version
    assert_equal "session_timer", device.firmware_app

    again = Device.upsert_from_discovery!(ip: "10.0.0.5", identity: "esp-fw")
    assert_equal "99eab52", again.firmware_version
    assert_equal "session_timer", again.firmware_app
  end

  test "published_ota_version reads manifest and match helper" do
    app_dir = Rails.root.join("storage", "firmware", "session_timer")
    FileUtils.mkdir_p(app_dir)
    manifest = app_dir.join("manifest.json")
    begin
      manifest.write({ "v" => 1, "app" => "session_timer", "version" => "abc1234" }.to_json)

      assert_equal "abc1234", Device.published_ota_version("session_timer")

      device = Device.upsert_from_discovery!(
        ip: "10.0.0.8",
        identity: "esp-match",
        firmware_version: "abc1234",
        firmware_app: "session_timer"
      )
      assert_equal true, device.firmware_matches_published?

      device.update!(firmware_version: "old0001")
      assert_equal false, device.firmware_matches_published?
    ensure
      manifest.delete if manifest.exist?
    end
  end

  test "upsert_from_discovery stores sanitized UI status" do
    device = Device.upsert_from_discovery!(
      ip: "10.0.0.6",
      identity: "esp-status",
      status: {
        "state" => "entry",
        "user_id" => 4,
        "user" => "rob",
        "entry" => "1:30",
        "lamp" => false,
        "fan" => true,
        "lcd" => [ "rob        1:30", "* clear  start #" ],
        "led" => "01:30",
        "led_kind" => "timer",
        "ignore_me" => "nope"
      }
    )
    assert_equal "entry", device.last_status["state"]
    assert_equal 4, device.last_status["user_id"]
    assert_equal "rob", device.last_status["user"]
    assert_equal [ "rob        1:30", "* clear  start #" ], device.last_status["lcd"]
    assert_equal "01:30", device.last_status["led"]
    assert_not device.last_status.key?("ignore_me")
    assert device.last_status_at.present?

    again = Device.upsert_from_discovery!(ip: "10.0.0.6", identity: "esp-status")
    assert_equal "entry", again.last_status["state"]
  end

  test "sanitize_status drops junk and truncates lcd lines" do
    assert_nil Device.sanitize_status(nil)
    assert_nil Device.sanitize_status("nope")

    out = Device.sanitize_status(
      "state" => "running-extra-long-name",
      "lcd" => [ "x" * 40, "y", "z" ],
      "led_kind" => "nope",
      "remain_seconds" => -5
    )
    assert_equal "running-extra-lo", out["state"]
    assert_equal [ "x" * 16, "y" ], out["lcd"]
    assert_not out.key?("led_kind")
    assert_equal 0, out["remain_seconds"]
  end

  test "upsert_from_discovery refreshes updated_at when nothing else changes" do
    device = Device.upsert_from_discovery!(ip: "10.0.0.5", identity: "esp-stable")
    original = device.updated_at

    travel 2.seconds do
      again = Device.upsert_from_discovery!(ip: "10.0.0.5", identity: "esp-stable")
      assert_equal device.id, again.id
      assert_operator again.updated_at, :>, original
    end
  end
end
