class CreateSkinTypes < ActiveRecord::Migration[8.1]
  def change
    create_table :skin_types do |t|
      t.integer :number, null: false
      t.string :roman, null: false
      t.text :description, null: false

      t.timestamps
    end
    add_index :skin_types, :number, unique: true
    add_index :skin_types, :roman, unique: true
  end
end
