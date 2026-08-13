require "test_helper"

class UserTherapiesControllerTest < ActionDispatch::IntegrationTest
  setup do
    @user = users(:one)
    sign_in_as @user
  end

  test "create assigns eczema without skin type" do
    assert_difference("@user.user_therapies.count", 1) do
      post user_user_therapies_url(@user), params: {
        user_therapy: { therapy_type_id: therapy_types(:eczema).id, skin_type_id: "" }
      }
    end
    assert_redirected_to user_url(@user)
    assert_nil @user.user_therapies.order(:id).last.skin_type
  end

  test "create requires skin type for psoriasis" do
    assert_no_difference("UserTherapy.count") do
      post user_user_therapies_url(users(:two)), params: {
        user_therapy: { therapy_type_id: therapy_types(:psoriasis).id, skin_type_id: "" }
      }
    end
    assert_response :unprocessable_content
  end

  test "destroy removes assignment" do
    assignment = user_therapies(:one_psoriasis)
    assert_difference("UserTherapy.count", -1) do
      delete user_user_therapy_url(@user, assignment)
    end
    assert_redirected_to user_url(@user)
  end

  test "guest cannot assign therapy" do
    sign_out
    post user_user_therapies_url(@user), params: {
      user_therapy: { therapy_type_id: therapy_types(:eczema).id }
    }
    assert_redirected_to new_session_path
  end
end
