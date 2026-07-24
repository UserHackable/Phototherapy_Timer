require "test_helper"

class ExposuresControllerTest < ActionDispatch::IntegrationTest
  setup do
    @user = users(:one)
    @exposure = exposures(:one)
    sign_in_as @user
  end

  test "should get index nested under user" do
    get user_exposures_url(@user)
    assert_response :success
    assert_match @user.name, response.body
  end

  test "index lists exposures for that user only" do
    get user_exposures_url(@user)
    assert_response :success
    assert_match "45", response.body # fixture one duration
    assert_no_match "120s", response.body # other user's fixture duration wording
  end

  test "should get new" do
    get new_user_exposure_url(@user)
    assert_response :success
  end

  test "should create exposure for user" do
    assert_difference("Exposure.count") do
      post user_exposures_url(@user), params: {
        exposure: { duration_seconds: 90, started_at: Time.current }
      }
    end

    exposure = Exposure.order(:id).last
    assert_equal @user.id, exposure.user_id
    assert_equal 90, exposure.duration_seconds
    assert_redirected_to user_exposure_url(@user, exposure)
  end

  test "create rejects zero duration" do
    assert_no_difference("Exposure.count") do
      post user_exposures_url(@user), params: {
        exposure: { duration_seconds: 0, started_at: Time.current }
      }
    end
    assert_response :unprocessable_content
  end

  test "create rejects missing started_at" do
    assert_no_difference("Exposure.count") do
      post user_exposures_url(@user), params: {
        exposure: { duration_seconds: 60, started_at: nil }
      }
    end
    assert_response :unprocessable_content
  end

  test "should show exposure" do
    get user_exposure_url(@user, @exposure)
    assert_response :success
    assert_match @user.name, response.body
  end

  test "should get edit" do
    get edit_user_exposure_url(@user, @exposure)
    assert_response :success
  end

  test "should update exposure" do
    patch user_exposure_url(@user, @exposure), params: {
      exposure: { duration_seconds: 120, started_at: @exposure.started_at }
    }
    assert_redirected_to user_exposure_url(@user, @exposure)
    assert_equal 120, @exposure.reload.duration_seconds
  end

  test "should destroy exposure" do
    assert_difference("Exposure.count", -1) do
      delete user_exposure_url(@user, @exposure)
    end

    assert_redirected_to user_exposures_url(@user)
  end

  test "cannot show another user exposure via wrong nest" do
    other = exposures(:two)
    get user_exposure_url(@user, other)
    assert_response :not_found
  end

  test "guest is redirected to sign in" do
    sign_out
    get user_exposures_url(@user)
    assert_redirected_to new_session_path
  end
end
