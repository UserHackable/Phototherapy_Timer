class AddIdentityToDevices < ActiveRecord::Migration[8.1]
  def change
    add_column :devices, :identity, :string
    add_index :devices, :identity, unique: true
    add_index :devices, :ip
  end
end
