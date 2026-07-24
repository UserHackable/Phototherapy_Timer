require "test_helper"

class UsersControllerTest < ActionDispatch::IntegrationTest
  setup do
    @user = users(:one)
    sign_in_as @user
  end

  test "should get index" do
    get users_url
    assert_response :success
    assert_match users(:one).name, response.body
    assert_match users(:two).name, response.body
  end

  test "should show user" do
    get user_url(@user)
    assert_response :success
    assert_match @user.email_address, response.body
  end

  test "show links to exposures" do
    get user_url(@user)
    assert_select "a[href=?]", user_exposures_path(@user)
  end

  test "guest is redirected to sign in" do
    sign_out
    get users_url
    assert_redirected_to new_session_path
  end
end
