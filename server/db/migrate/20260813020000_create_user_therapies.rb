class CreateUserTherapies < ActiveRecord::Migration[8.1]
  def change
    create_table :user_therapies do |t|
      t.references :user, null: false, foreign_key: true
      t.references :therapy_type, null: false, foreign_key: true
      t.references :skin_type, foreign_key: true

      t.timestamps
    end
    add_index :user_therapies, [ :user_id, :therapy_type_id ], unique: true
  end
end
