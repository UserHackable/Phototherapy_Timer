class CreateExposures < ActiveRecord::Migration[8.1]
  def change
    create_table :exposures do |t|
      t.references :user, null: false, foreign_key: true
      t.datetime :started_at, null: false
      t.integer :duration_seconds, null: false

      t.timestamps
    end

    add_index :exposures, [ :user_id, :started_at ]
  end
end
