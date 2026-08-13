class AddMaxSecondsToTherapyAndSkinTypes < ActiveRecord::Migration[8.1]
  def change
    add_column :therapy_types, :max_seconds, :integer
    add_column :skin_types, :max_seconds, :integer
  end
end
