require "test_helper"

class DevicesControllerTest < ActionDispatch::IntegrationTest
  setup do
    @device = devices(:one)
    sign_in_as users(:one)
  end

  test "should get index" do
    get devices_url
    assert_response :success
  end

  test "index shows firmware version" do
    @device.update!(firmware_version: "99eab52", firmware_app: "session_timer")
    get devices_url
    assert_response :success
    assert_match "99eab52", @response.body
    assert_match "session_timer", @response.body
  end

  test "index shows last LCD snapshot" do
    @device.update!(
      last_status: {
        "state" => "entry",
        "user" => "rob",
        "lcd" => [ "rob        1:30", "* clear  start #" ],
        "led" => "01:30",
        "led_kind" => "timer",
        "lamp" => false,
        "fan" => false
      },
      last_status_at: Time.current
    )
    get devices_url
    assert_response :success
    assert_match "rob        1:30", @response.body
    assert_match "* clear  start #", @response.body
    assert_match "01:30", @response.body
    assert_match "entry", @response.body
  end

  test "should get new" do
    get new_device_url
    assert_response :success
  end

  test "should create device" do
    assert_difference("Device.count") do
      post devices_url, params: { device: { ip: @device.ip } }
    end

    assert_redirected_to device_url(Device.last)
  end

  test "should show device" do
    get device_url(@device)
    assert_response :success
  end

  test "should get edit" do
    get edit_device_url(@device)
    assert_response :success
  end

  test "should update device" do
    patch device_url(@device), params: { device: { ip: @device.ip } }
    assert_redirected_to device_url(@device)
  end

  test "should destroy device" do
    assert_difference("Device.count", -1) do
      delete device_url(@device)
    end

    assert_redirected_to devices_url
  end

  test "ota_check pokes the device and redirects" do
    post ota_check_device_url(@device)
    assert_redirected_to device_url(@device)
    assert_match(/Update check sent/, flash[:notice])
  end

  test "guest cannot request ota_check" do
    sign_out
    post ota_check_device_url(@device)
    assert_redirected_to new_session_path
  end

  test "guest is redirected to sign in" do
    sign_out
    get devices_url
    assert_redirected_to new_session_path
  end
end
