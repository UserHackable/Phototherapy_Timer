# frozen_string_literal: true

# LAN firmware artifacts for ESP OTA (unauthenticated on purpose — private LAN).
# Files live under storage/firmware/<app>/ (Kamal volume).
class FirmwareController < ApplicationController
  allow_unauthenticated_access

  APP_NAME = /\A[a-z0-9][a-z0-9_-]{0,63}\z/i

  def manifest
    path = artifact_path("manifest.json")
    return head :not_found unless path

    send_file path,
              type: "application/json",
              disposition: "inline",
              filename: "manifest.json"
  end

  def app_bin
    path = artifact_path("app.bin")
    return head :not_found unless path

    send_file path,
              type: "application/octet-stream",
              disposition: "inline",
              filename: "app.bin"
  end

  private

  def artifact_path(filename)
    app = params[:app].to_s
    return nil unless app.match?(APP_NAME)

    root = Rails.root.join("storage", "firmware", app)
    path = root.join(filename).expand_path
    return nil unless path.to_s.start_with?(root.expand_path.to_s)
    return nil unless path.file?

    path
  end
end
