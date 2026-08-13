require "test_helper"

class SkinTypeTest < ActiveSupport::TestCase
  test "valid fixture" do
    assert skin_types(:one).valid?
  end

  test "requires number roman and description" do
    st = SkinType.new
    assert_not st.valid?
    assert_includes st.errors[:number], "can't be blank"
    assert_includes st.errors[:roman], "can't be blank"
    assert_includes st.errors[:description], "can't be blank"
  end

  test "number must be 1 through 6" do
    st = SkinType.new(number: 0, roman: "X", description: "nope")
    assert_not st.valid?
    st.number = 7
    assert_not st.valid?
  end

  test "label is Type plus roman" do
    assert_equal "Type I", skin_types(:one).label
  end

  test "data_imp loads Table 1 from skin_types.yml" do
    UserTherapy.delete_all
    SkinType.delete_all
    DataImp.import "skin_types.yml"
    loaded = SkinType.ordered.to_a
    assert_equal 6, loaded.size
    assert_equal (1..6).to_a, loaded.map(&:number)
    assert_equal %w[I II III IV V VI], loaded.map(&:roman)
    type1 = SkinType.find_by!(number: 1)
    assert_equal 16, type1.step_seconds
    assert_equal 333, type1.max_seconds
    assert_equal 50, type1.initial_seconds
    assert_match(/Always burns, never tans/i, type1.description)
    assert_equal 20, SkinType.find_by!(number: 3).step_seconds
    assert_equal 83, SkinType.find_by!(number: 3).initial_seconds
    assert_equal 25, SkinType.find_by!(number: 5).step_seconds
    assert_equal 133, SkinType.find_by!(number: 5).initial_seconds
    type5 = SkinType.find_by!(number: 5)
    assert_match(/moderately pigmented/i, type5.description)
  end

  test "skin type import updates existing rows and does not duplicate" do
    DataImp.import "skin_types.yml"
    first = SkinType.find_by!(number: 1)
    first.update!(description: "stale")
    ids = SkinType.order(:id).pluck(:id)

    assert_no_difference("SkinType.count") { DataImp.import "skin_types.yml" }

    assert_equal ids, SkinType.order(:id).pluck(:id)
    assert_equal first.id, SkinType.find_by!(number: 1).id
    assert_match(/Always burns, never tans/i, SkinType.find_by!(number: 1).description)
  end
end

