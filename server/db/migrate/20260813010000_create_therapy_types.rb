class CreateTherapyTypes < ActiveRecord::Migration[8.1]
  def change
    create_table :therapy_types do |t|
      t.string :slug, null: false
      t.string :name, null: false
      t.boolean :uses_skin_type, null: false, default: false
      t.text :description, null: false

      t.timestamps
    end
    add_index :therapy_types, :slug, unique: true
  end
end
