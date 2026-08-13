class AddStepSecondsToTherapyAndSkinTypes < ActiveRecord::Migration[8.1]
  def change
    add_column :therapy_types, :step_seconds, :integer
    add_column :skin_types, :step_seconds, :integer
  end
end
