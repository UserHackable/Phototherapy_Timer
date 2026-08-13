class AddFirmwareToDevices < ActiveRecord::Migration[8.1]
  def change
    add_column :devices, :firmware_version, :string
    add_column :devices, :firmware_app, :string
  end
end
