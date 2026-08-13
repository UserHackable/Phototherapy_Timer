class AddLastStatusToDevices < ActiveRecord::Migration[8.1]
  def change
    add_column :devices, :last_status, :json
    add_column :devices, :last_status_at, :datetime
  end
end
