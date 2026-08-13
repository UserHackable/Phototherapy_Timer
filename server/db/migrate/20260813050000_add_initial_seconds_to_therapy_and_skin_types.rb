class AddInitialSecondsToTherapyAndSkinTypes < ActiveRecord::Migration[8.1]
  def change
    add_column :therapy_types, :initial_seconds, :integer
    add_column :skin_types, :initial_seconds, :integer
  end
end
