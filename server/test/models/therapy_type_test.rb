require "test_helper"

class TherapyTypeTest < ActiveSupport::TestCase
  test "valid fixture" do
    assert therapy_types(:psoriasis).valid?
  end

  test "requires slug name and description" do
    tt = TherapyType.new
    assert_not tt.valid?
    assert_includes tt.errors[:slug], "can't be blank"
    assert_includes tt.errors[:name], "can't be blank"
    assert_includes tt.errors[:description], "can't be blank"
  end

  test "label is the name" do
    assert_equal "Psoriasis", therapy_types(:psoriasis).label
  end

  test "keypad ids are stable digits 1-4" do
    assert_equal 1, therapy_types(:manual).keypad_id
    assert_equal 2, therapy_types(:psoriasis).keypad_id
    assert_equal 4, therapy_types(:eczema).keypad_id
    assert_equal 3, TherapyType::KEYPAD["vitiligo"]
    assert_equal "Eczema", therapy_types(:eczema).keypad_label
    assert_equal "Manual", therapy_types(:manual).keypad_label
    assert_equal therapy_types(:psoriasis), TherapyType.find_by_keypad_id(2)
    assert_nil TherapyType.find_by_keypad_id(3)
  end

  test "data_imp loads EGT therapy types from therapy_types.yml" do
    UserTherapy.delete_all
    TherapyType.delete_all
    DataImp.import "therapy_types.yml"
    loaded = TherapyType.ordered.to_a
    assert_equal 4, loaded.size
    assert_equal %w[atopic_dermatitis manual psoriasis vitiligo], loaded.map(&:slug).sort
    manual = TherapyType.find_by!(slug: "manual")
    assert_not manual.uses_skin_type?
    assert_equal 15, manual.step_seconds
    assert_equal 1200, manual.max_seconds
    assert_equal 30, manual.initial_seconds
    psoriasis = TherapyType.find_by!(slug: "psoriasis")
    assert psoriasis.uses_skin_type?
    assert_nil psoriasis.step_seconds
    assert_nil psoriasis.initial_seconds
    assert_equal 8, TherapyType.find_by!(slug: "vitiligo").step_seconds
    assert_equal 100, TherapyType.find_by!(slug: "vitiligo").max_seconds
    assert_equal 50, TherapyType.find_by!(slug: "vitiligo").initial_seconds
    assert_equal 16, TherapyType.find_by!(slug: "atopic_dermatitis").step_seconds
    assert_equal 166, TherapyType.find_by!(slug: "atopic_dermatitis").max_seconds
    assert_equal 50, TherapyType.find_by!(slug: "atopic_dermatitis").initial_seconds
    assert_match(/skin type/i, psoriasis.description)
    eczema = TherapyType.find_by!(slug: "atopic_dermatitis")
    assert_not eczema.uses_skin_type?
    assert_match(/eczema/i, eczema.name)
  end

  test "therapy type import updates existing rows and does not duplicate" do
    DataImp.import "therapy_types.yml"
    first = TherapyType.find_by!(slug: "psoriasis")
    first.update!(description: "stale")
    ids = TherapyType.order(:id).pluck(:id)

    assert_no_difference("TherapyType.count") { DataImp.import "therapy_types.yml" }

    assert_equal ids, TherapyType.order(:id).pluck(:id)
    assert_equal first.id, TherapyType.find_by!(slug: "psoriasis").id
    assert_match(/skin type/i, TherapyType.find_by!(slug: "psoriasis").description)
  end
end
