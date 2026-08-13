# frozen_string_literal: true

# Idempotent data_imp porter: find by a stable key, then update attributes.
# Never delete_all / destroy existing rows.
class ApplicationImporter < DataImp::Porter
  class_attribute :model_class, instance_writer: false
  class_attribute :find_keys, default: [], instance_writer: false

  def self.imports(model, by:)
    self.model_class = model
    self.find_keys = Array(by)
  end

  def import
    record = find_or_build
    apply_attributes(record)
    record.save!
    record
  end

  def find_or_build
    model_class.find_or_initialize_by(find_attributes)
  end

  def find_attributes
    find_keys.index_with { |key| public_send(key) }
  end

  def apply_attributes(record)
    record.assign_attributes(assign_attributes_hash)
  end

  def assign_attributes_hash
    except_keys = find_keys.map(&:to_s)
    hash.except(*except_keys)
  end
end
