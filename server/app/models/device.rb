class Device < ApplicationRecord
  validates :ip, presence: true
  validates :identity, uniqueness: true, allow_nil: true

  # Upsert a device seen via UDP discovery.
  # Prefer stable identity when present; always refresh the observed IP.
  def self.upsert_from_discovery!(ip:, identity: nil)
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
    device.save!
    device
  end
end
