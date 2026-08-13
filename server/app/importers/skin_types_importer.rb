# frozen_string_literal: true

# db/data/skin_types.yml → upsert by Table 1 number (I–VI). Re-seed updates rows.
class SkinTypesImporter < ApplicationImporter
  imports SkinType, by: :number
end

