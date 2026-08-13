json.extract! device, :id, :identity, :ip, :firmware_version, :firmware_app,
              :last_status, :last_status_at, :created_at, :updated_at
json.published_version Device.published_ota_version(device.firmware_app.presence || Device::DEFAULT_FIRMWARE_APP)
json.firmware_matches_published device.firmware_matches_published?
json.url device_url(device, format: :json)
