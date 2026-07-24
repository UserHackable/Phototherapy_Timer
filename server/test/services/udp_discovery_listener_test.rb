require "test_helper"

class UdpDiscoveryListenerTest < ActiveSupport::TestCase
  setup do
    @listener = UdpDiscoveryListener.new(port: 0, logger: Logger.new(File::NULL))
  end

  test "parse_message ping with identity field" do
    parsed = UdpDiscoveryListener.parse_message("PHOTOTHERAPY/1 PING identity=esp-mac")
    assert_equal :ping, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
  end

  test "parse_message ping positional identity" do
    parsed = UdpDiscoveryListener.parse_message("PING esp-mac")
    assert_equal :ping, parsed[:type]
    assert_equal "esp-mac", parsed[:identity]
  end

  test "parse_message pong" do
    parsed = UdpDiscoveryListener.parse_message("PHOTOTHERAPY/1 PONG identity=host ip=1.2.3.4")
    assert_equal :pong, parsed[:type]
    assert_equal "host", parsed[:identity]
    assert_equal "1.2.3.4", parsed[:ip]
  end

  test "parse_message ignores garbage" do
    assert_nil UdpDiscoveryListener.parse_message("hello world")
  end

  test "handle_packet upserts device and returns pong" do
    pong = @listener.handle_packet("PHOTOTHERAPY/1 PING identity=unit-test-esp", "192.168.50.20")

    assert pong.start_with?("PHOTOTHERAPY/1 PONG")
    assert_match(/ip=\S+/, pong)

    device = Device.find_by!(identity: "unit-test-esp")
    assert_equal "192.168.50.20", device.ip
  end

  test "handle_packet ignores pong" do
    before = Device.count
    assert_nil @listener.handle_packet(
      "PHOTOTHERAPY/1 PONG identity=other ip=1.2.3.4",
      "192.168.50.20"
    )
    assert_equal before, Device.count
  end

  test "build_pong format" do
    msg = UdpDiscoveryListener.build_pong(identity: "srv", ip: "192.168.1.1")
    assert_equal "PHOTOTHERAPY/1 PONG identity=srv ip=192.168.1.1", msg
  end
end
