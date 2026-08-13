class AddTherapyToExposures < ActiveRecord::Migration[8.1]
  def change
    add_reference :exposures, :therapy_type, foreign_key: true
    add_reference :exposures, :skin_type, foreign_key: true
  end
end
