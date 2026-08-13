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

  test "show lists assigned therapies" do
    get user_url(@user)
    assert_response :success
    assert_match "Psoriasis", @response.body
    assert_match "Type I", @response.body
  end

  test "assign form marks therapies that need a skin type" do
    get user_url(@user)
    assert_select "form[data-controller='therapy-form']"
    assert_select "option[value=?][data-uses-skin-type=true]", therapy_types(:psoriasis).id.to_s
    assert_select "option[value=?][data-uses-skin-type=false]", therapy_types(:eczema).id.to_s
    assert_select "option[value=?][data-uses-skin-type=false]", therapy_types(:manual).id.to_s
    assert_select "[data-therapy-form-target=skinType][hidden]"
  end

  test "guest is redirected to sign in" do
    sign_out
    get users_url
    assert_redirected_to new_session_path
  end
end
