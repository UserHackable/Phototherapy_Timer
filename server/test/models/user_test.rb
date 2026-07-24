require "test_helper"

class UserTest < ActiveSupport::TestCase
  test "requires name and email" do
    user = User.new(password: "password", password_confirmation: "password")
    assert_not user.valid?
    assert_includes user.errors[:name], "can't be blank"
    assert_includes user.errors[:email_address], "can't be blank"
  end

  test "normalizes email and name" do
    user = User.create!(
      name: "  Rob  ",
      email_address: "  Rob@Ferney.ORG ",
      password: "password",
      password_confirmation: "password"
    )
    assert_equal "Rob", user.name
    assert_equal "rob@ferney.org", user.email_address
  end

  test "has many exposures" do
    assert_includes users(:one).exposures, exposures(:one)
    assert_not_includes users(:one).exposures, exposures(:two)
  end

  test "ensure_guest creates id 0" do
    User.where(id: 0).delete_all
    guest = User.ensure_guest!(password: "password")
    assert_equal 0, guest.id
    assert_equal "Guest", guest.name
    assert guest.guest?
    # idempotent
    assert_equal guest.id, User.ensure_guest!.id
  end

  test "household scope excludes guest" do
    User.ensure_guest!
    assert_not_includes User.household.pluck(:id), User::GUEST_ID
  end
end
