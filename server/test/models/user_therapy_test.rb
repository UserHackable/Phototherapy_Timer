require "test_helper"

class UserTherapyTest < ActiveSupport::TestCase
  test "valid fixture with skin type" do
    assert user_therapies(:one_psoriasis).valid?
    assert_equal users(:one), user_therapies(:one_psoriasis).user
    assert_equal therapy_types(:psoriasis), user_therapies(:one_psoriasis).therapy_type
    assert_equal skin_types(:one), user_therapies(:one_psoriasis).skin_type
  end

  test "valid fixture without skin type when therapy does not use it" do
    assert user_therapies(:two_eczema).valid?
    assert_nil user_therapies(:two_eczema).skin_type
  end

  test "requires skin type for psoriasis" do
    assignment = UserTherapy.new(user: users(:two), therapy_type: therapy_types(:psoriasis))
    assert_not assignment.valid?
    assert_includes assignment.errors[:skin_type], "is required for Psoriasis"
  end

  test "allows missing skin type for eczema" do
    assignment = UserTherapy.new(user: users(:one), therapy_type: therapy_types(:eczema))
    assert assignment.valid?
  end

  test "user and therapy type pair is unique" do
    dup = UserTherapy.new(
      user: users(:one),
      therapy_type: therapy_types(:psoriasis),
      skin_type: skin_types(:three)
    )
    assert_not dup.valid?
    assert_includes dup.errors[:therapy_type_id], "is already assigned to this user"
  end

  test "step_seconds comes from skin type for psoriasis" do
    assert_equal 16, user_therapies(:one_psoriasis).step_seconds
  end

  test "max_seconds comes from skin type for psoriasis" do
    assert_equal 333, user_therapies(:one_psoriasis).max_seconds
  end

  test "initial_seconds comes from skin type for psoriasis" do
    assert_equal 50, user_therapies(:one_psoriasis).initial_seconds
  end

  test "step_seconds comes from therapy type for eczema" do
    assert_equal 16, user_therapies(:two_eczema).step_seconds
  end

  test "max_seconds comes from therapy type for eczema" do
    assert_equal 166, user_therapies(:two_eczema).max_seconds
  end

  test "initial_seconds comes from therapy type for eczema" do
    assert_equal 50, user_therapies(:two_eczema).initial_seconds
  end

  test "user therapy_step_seconds uses newest complete assignment" do
    assert_equal 16, users(:one).therapy_step_seconds
    assert_equal 16, users(:two).therapy_step_seconds
    assert_equal 50, users(:one).therapy_initial_seconds
    assert_equal 50, users(:two).therapy_initial_seconds
  end

  test "reassigning an older therapy makes it the active one" do
    user = users(:one)
    user.user_therapies.create!(therapy_type: therapy_types(:eczema))
    assert_equal 50, user.therapy_initial_seconds
    assert_equal 166, user.therapy_max_seconds

    psoriasis = user_therapies(:one_psoriasis)
    psoriasis.update!(updated_at: Time.current)
    assert_equal 333, user.reload.therapy_max_seconds
    assert_equal 50, user.therapy_initial_seconds
  end

  test "user without assignment uses default step max and initial seconds" do
    assert_equal 10, users(:unassigned).therapy_step_seconds
    assert_equal 1200, users(:unassigned).therapy_max_seconds
    assert_equal 30, users(:unassigned).therapy_initial_seconds
  end

  test "manual therapy uses 15 second step, 20 minute max, and 30 second initial" do
    assignment = users(:unassigned).user_therapies.create!(therapy_type: therapy_types(:manual))
    assert_equal 15, assignment.step_seconds
    assert_equal 1200, assignment.max_seconds
    assert_equal 30, assignment.initial_seconds
    assert_equal 15, users(:unassigned).therapy_step_seconds
    assert_equal 1200, users(:unassigned).therapy_max_seconds
    assert_equal 30, users(:unassigned).therapy_initial_seconds
  end

  test "label includes skin type when present" do
    assert_equal "Psoriasis — Type I", user_therapies(:one_psoriasis).label
    assert_equal "Atopic dermatitis (eczema)", user_therapies(:two_eczema).label
  end

  test "destroying user destroys assignments" do
    user = users(:one)
    assert_difference("UserTherapy.count", -1) { user.destroy! }
  end
end
