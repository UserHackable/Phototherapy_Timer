require "test_helper"

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
end
