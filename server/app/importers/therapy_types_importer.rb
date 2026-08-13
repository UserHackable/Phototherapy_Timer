# frozen_string_literal: true

# db/data/therapy_types.yml → upsert by slug. Re-seed updates rows.
class TherapyTypesImporter < ApplicationImporter
  imports TherapyType, by: :slug
end
