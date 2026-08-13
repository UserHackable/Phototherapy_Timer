class Device < ApplicationRecord
  FIRMWARE_APP_NAME = /\A[a-z0-9][a-z0-9_-]{0,63}\z/i
  DEFAULT_FIRMWARE_APP = "session_timer"

  validates :ip, presence: true
  validates :identity, uniqueness: true, allow_nil: true

  # Upsert a device seen via UDP discovery.
  # Prefer stable identity when present; always refresh the observed IP.
  # firmware_version / firmware_app / last_status are only written when present.
  def self.upsert_from_discovery!(ip:, identity: nil, firmware_version: nil, firmware_app: nil, status: nil)
    ip = ip.to_s.strip.presence
    identity = identity.to_s.strip.presence
    raise ArgumentError, "ip is required" if ip.blank?

    device =
      if identity
        find_or_initialize_by(identity: identity)
      else
        find_or_initialize_by(ip: ip)
      end

    device.ip = ip
    device.identity = identity if identity
    device.firmware_version = firmware_version if firmware_version.present?
    device.firmware_app = firmware_app if firmware_app.present?
    sanitized = sanitize_status(status)
    if sanitized
      device.last_status = sanitized
      device.last_status_at = Time.current
    end
    # Last-seen even when IP/identity are unchanged (otherwise save is a no-op).
    device.updated_at = Time.current
    device.save!
    device
  end

  def self.sanitize_status(raw)
    data = raw
    data = raw.to_unsafe_h if raw.respond_to?(:to_unsafe_h)
    return nil unless data.is_a?(Hash)

    data = data.stringify_keys
    out = {}
    if data["state"].present?
      out["state"] = data["state"].to_s.strip[0, 16]
    end
    uid = data["user_id"]
    out["user_id"] = uid.to_i if uid.is_a?(Numeric) || (uid.is_a?(String) && uid.match?(/\A\d+\z/))
    out["user"] = data["user"].to_s.strip[0, 24] if data["user"].present?
    out["entry"] = data["entry"].to_s.strip[0, 8] if data["entry"].present?
    %w[remain_seconds planned_seconds].each do |key|
      val = data[key]
      next unless val.is_a?(Numeric) || (val.is_a?(String) && val.match?(/\A-?\d+\z/))

      n = val.to_i
      n = 0 if n.negative?
      out[key] = n
    end
    out["lamp"] = !!data["lamp"] unless data["lamp"].nil?
    out["fan"] = !!data["fan"] unless data["fan"].nil?
    out["after_complete"] = !!data["after_complete"] unless data["after_complete"].nil?
    if data["lcd"].is_a?(Array)
      out["lcd"] = data["lcd"].first(2).map { |line| line.to_s[0, 16] }
    end
    out["led"] = data["led"].to_s.strip[0, 8] if data.key?("led")
    if data["led_kind"].present?
      kind = data["led_kind"].to_s.strip[0, 8]
      out["led_kind"] = kind if %w[clock timer].include?(kind)
    end
    out.presence
  end

  # Version string from storage/firmware/<app>/manifest.json (OTA publish), or nil.
  def self.published_ota_version(app = DEFAULT_FIRMWARE_APP)
    name = app.to_s.strip
    return nil unless name.match?(FIRMWARE_APP_NAME)

    path = Rails.root.join("storage", "firmware", name, "manifest.json")
    return nil unless path.file?

    JSON.parse(path.read)["version"].to_s.strip.presence
  rescue JSON::ParserError, Errno::ENOENT
    nil
  end

  # true / false when both sides known; nil if we cannot compare.
  # Ask the LAN UDP listener to poke this module into an immediate OTA check.
  def request_ota_check!
    raise ArgumentError, "identity is required" if identity.blank?

    host = ENV.fetch("UDP_DISCOVERY_IP", "192.168.1.202")
    port = UdpDiscoveryListener.port
    payload = UdpDiscoveryListener.build_ota_request(identity: identity)
    UDPSocket.open do |s|
      s.send(payload, 0, host, port)
    end
    true
  end

  def firmware_matches_published?
    ver = firmware_version.to_s.strip
    return nil if ver.blank?

    pub = self.class.published_ota_version(firmware_app.presence || DEFAULT_FIRMWARE_APP)
    return nil if pub.blank?

    ver == pub
  end
end

