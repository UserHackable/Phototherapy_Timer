# frozen_string_literal: true

require "json"
require "socket"

# UDP discovery for ESP32 ↔ Rails on the LAN (JSON payloads).
#
# On each client ping:
#   1. Log the sender IP
#   2. Create/update a Device (identity + IP)
#   3. Unicast a JSON pong with server identity, LAN IP, and current wall time
#
# Wire protocol (UTF-8 JSON, v1):
#   Client → server:
#     {"v":1,"type":"ping","identity":"esp32-<mac>"}
#     {"v":1,"type":"users","identity":"esp32-<mac>"}   # user list request (key A)
#     {"v":1,"type":"therapy","identity":"esp32-<mac>","user_id":4}  # A then digit
#     {"v":1,"type":"exposure","identity":"esp32-<mac>",
#      "user_id":0,"duration_seconds":30,"unix":1710000000}  # lamp off
#   Server → client:
#     {"v":1,"type":"pong","identity":"<host>","ip":"<lan-ip>",
#      "unix":1710000000,"iso8601":"…","tz":"America/Denver",
#      "tz_offset":-21600,"tz_posix":"MST7MDT,M3.2.0,M11.1.0"}
#     {"v":1,"type":"users","users":[{"id":1,"name":"rob"}, ...]}  # ids 1–9 (not Guest)
#     {"v":1,"type":"therapy","user_id":4,"name":"rob","recommended_seconds":30}
#     {"v":1,"type":"exposure","ok":true,"id":12,"user_id":0,"duration_seconds":30}
#
# recommended_seconds defaults to DEFAULT_RECOMMENDED_SECONDS until per-user
# therapy schedules exist.
#
# ENV:
#   UDP_DISCOVERY_PORT     (default 3000)
#   UDP_DISCOVERY          set to "0" to disable
#   UDP_DISCOVERY_IDENTITY server name in pong (default hostname)
#   UDP_DISCOVERY_IP       force advertised IP (optional)
#
class UdpDiscoveryListener
  PROTOCOL_VERSION = 1
  DEFAULT_PORT = 3000
  MAX_PACKET = 1500
  # Temporary default until per-user recommended exposure is stored (30 seconds).
  DEFAULT_RECOMMENDED_SECONDS = 30
  # POSIX TZ for ESP setenv; override with UDP_DISCOVERY_TZ_POSIX if needed.
  DEFAULT_TZ_POSIX = "MST7MDT,M3.2.0,M11.1.0"

  class << self
    def enabled?
      ENV.fetch("UDP_DISCOVERY", "1") != "0"
    end

    def port
      Integer(ENV.fetch("UDP_DISCOVERY_PORT", DEFAULT_PORT.to_s))
    end

    def server_identity
      ENV.fetch("UDP_DISCOVERY_IDENTITY") { Socket.gethostname }
    end

    def instance
      @instance ||= new
    end

    def start!
      instance.start
    end

    def stop!
      instance.stop
    end
  end

  def initialize(port: self.class.port, logger: Rails.logger)
    @port = port
    @logger = logger
    @mutex = Mutex.new
    @thread = nil
    @socket = nil
    @running = false
  end

  def start
    @mutex.synchronize do
      return if @running

      @running = true
      @thread = Thread.new { run_loop }
      @thread.name = "udp-discovery-#{@port}"
      @thread.abort_on_exception = false
      @logger.info("[udp_discovery] listening on UDP 0.0.0.0:#{@port} (JSON v#{PROTOCOL_VERSION})")
    end
    self
  end

  def stop
    @mutex.synchronize do
      @running = false
      begin
        @socket&.close
      rescue StandardError
        nil
      end
      @socket = nil
      if @thread&.alive?
        @thread.join(2)
      end
      @thread = nil
    end
    self
  end

  def running?
    @running
  end

  # Process one payload. Returns JSON reply string if a response should be sent.
  def handle_packet(payload, remote_ip)
    text = payload.to_s.strip
    return nil if text.empty?

    parsed = self.class.parse_message(text)
    return nil unless parsed
    return nil if parsed[:type] == :pong

    case parsed[:type]
    when :ping
      handle_ping(parsed, remote_ip)
    when :users
      handle_users_request(parsed, remote_ip)
    when :therapy
      handle_therapy_request(parsed, remote_ip)
    when :exposure
      handle_exposure_log(parsed, remote_ip)
    else
      @logger.debug("[udp_discovery] ignore from #{remote_ip}: #{text.truncate(120)}")
      nil
    end
  rescue StandardError => e
    @logger.error("[udp_discovery] handle_packet error: #{e.class}: #{e.message}")
    nil
  end

  def handle_ping(parsed, remote_ip)
    identity = parsed[:identity]
    @logger.info(
      "[udp_discovery] ping from #{remote_ip}" \
      "#{identity ? " identity=#{identity}" : ""}"
    )

    ActiveRecord::Base.connection_pool.with_connection do
      device = Device.upsert_from_discovery!(ip: remote_ip, identity: identity)
      @logger.info(
        "[udp_discovery] Device##{device.id} ip=#{device.ip}" \
        "#{device.identity.present? ? " identity=#{device.identity}" : ""}"
      )
    end

    server_ip = self.class.local_ip_for(remote_ip)
    self.class.build_pong(identity: self.class.server_identity, ip: server_ip)
  end

  def handle_users_request(parsed, remote_ip)
    identity = parsed[:identity]
    @logger.info(
      "[udp_discovery] users request from #{remote_ip}" \
      "#{identity ? " identity=#{identity}" : ""}"
    )

    users =
      ActiveRecord::Base.connection_pool.with_connection do
        if identity.present?
          Device.upsert_from_discovery!(ip: remote_ip, identity: identity)
        end
        # Household ids 1–9 first, Guest (id 0) always last for A+0.
        list = User.household.limit(9).map { |u| { id: u.id, name: u.name } }
        guest = User.ensure_guest!
        list << { id: guest.id, name: guest.name }
        list
      end

    self.class.build_users_reply(users)
  end

  def handle_therapy_request(parsed, remote_ip)
    identity = parsed[:identity]
    user_id = parsed[:user_id]
    @logger.info(
      "[udp_discovery] therapy request from #{remote_ip}" \
      "#{identity ? " identity=#{identity}" : ""} user_id=#{user_id.inspect}"
    )

    # 0 = Guest (A then 0); 1–9 = household.
    unless user_id.is_a?(Integer) && user_id >= 0
      return self.class.build_therapy_reply(
        user_id: user_id,
        name: nil,
        recommended_seconds: nil,
        error: "bad_user_id"
      )
    end

    result =
      ActiveRecord::Base.connection_pool.with_connection do
        if identity.present?
          Device.upsert_from_discovery!(ip: remote_ip, identity: identity)
        end
        user = User.find_by(id: user_id)
        if user.nil? && user_id == User::GUEST_ID
          user = User.ensure_guest!
        end
        if user
          {
            user_id: user.id,
            name: user.name,
            recommended_seconds: DEFAULT_RECOMMENDED_SECONDS
          }
        else
          { user_id: user_id, name: nil, recommended_seconds: nil, error: "not_found" }
        end
      end

    self.class.build_therapy_reply(**result)
  end

  # Lamp-off log from session_timer: user, duration, end time (unix).
  def handle_exposure_log(parsed, remote_ip)
    identity = parsed[:identity]
    user_id = parsed[:user_id]
    duration = parsed[:duration_seconds]
    unix = parsed[:unix]
    @logger.info(
      "[udp_discovery] exposure log from #{remote_ip}" \
      "#{identity ? " identity=#{identity}" : ""}" \
      " user_id=#{user_id.inspect} duration=#{duration.inspect} unix=#{unix.inspect}"
    )

    unless user_id.is_a?(Integer) && user_id >= 0
      return self.class.build_exposure_reply(ok: false, error: "bad_user_id", user_id: user_id)
    end
    unless duration.is_a?(Integer) && duration.positive?
      return self.class.build_exposure_reply(
        ok: false, error: "bad_duration", user_id: user_id, duration_seconds: duration
      )
    end

    result =
      ActiveRecord::Base.connection_pool.with_connection do
        if identity.present?
          Device.upsert_from_discovery!(ip: remote_ip, identity: identity)
        end

        user = User.find_by(id: user_id)
        if user.nil? && user_id == User::GUEST_ID
          user = User.ensure_guest!
        end
        unless user
          next { ok: false, error: "not_found", user_id: user_id, duration_seconds: duration }
        end

        ended_at =
          if unix.is_a?(Integer) && unix.positive?
            Time.zone.at(unix)
          else
            Time.current
          end
        started_at = ended_at - duration.seconds

        exposure = user.exposures.create!(
          started_at: started_at,
          duration_seconds: duration
        )
        @logger.info(
          "[udp_discovery] Exposure##{exposure.id} user=#{user.id}(#{user.name}) " \
          "duration=#{duration}s started_at=#{started_at.iso8601}"
        )
        {
          ok: true,
          id: exposure.id,
          user_id: user.id,
          duration_seconds: duration,
          started_at: started_at.iso8601
        }
      end

    self.class.build_exposure_reply(**result)
  end

  def self.parse_message(text)
    line = text.to_s.strip
    return nil if line.empty?

    # Prefer JSON
    if line.start_with?("{")
      return parse_json_message(line)
    end

    # Legacy text protocol (transition only)
    parse_legacy_message(line)
  end

  def self.parse_json_message(line)
    data = JSON.parse(line)
    type = data["type"].to_s.downcase
    case type
    when "ping"
      { type: :ping, identity: data["identity"].presence, v: data["v"] }
    when "users"
      { type: :users, identity: data["identity"].presence, v: data["v"] }
    when "therapy"
      {
        type: :therapy,
        identity: data["identity"].presence,
        user_id: coerce_user_id(data["user_id"]),
        v: data["v"]
      }
    when "exposure"
      {
        type: :exposure,
        identity: data["identity"].presence,
        user_id: coerce_user_id(data["user_id"]),
        duration_seconds: coerce_nonneg_int(data["duration_seconds"]),
        unix: coerce_nonneg_int(data["unix"]),
        v: data["v"]
      }
    when "pong"
      {
        type: :pong,
        identity: data["identity"].presence,
        ip: data["ip"].presence,
        unix: data["unix"],
        iso8601: data["iso8601"].presence,
        tz: data["tz"].presence,
        tz_offset: data["tz_offset"],
        tz_posix: data["tz_posix"].presence,
        v: data["v"]
      }
    else
      nil
    end
  rescue JSON::ParserError
    nil
  end

  def self.coerce_user_id(raw)
    return raw if raw.is_a?(Integer)
    return raw.to_i if raw.is_a?(String) && raw.match?(/\A\d+\z/)
    return raw.to_i if raw.is_a?(Float) && raw == raw.to_i

    nil
  end

  def self.coerce_nonneg_int(raw)
    return raw if raw.is_a?(Integer)
    return raw.to_i if raw.is_a?(String) && raw.match?(/\A\d+\z/)
    return raw.to_i if raw.is_a?(Float) && raw == raw.to_i

    nil
  end

  def self.parse_legacy_message(line)
    body = line.sub(/\APHOTOTHERAPY\/\d+\s+/i, "")
    case body
    when /\APING\b/i
      {
        type: :ping,
        identity: extract_field(body, "identity") || extract_positional_identity(body, "PING")
      }
    when /\APONG\b/i
      {
        type: :pong,
        identity: extract_field(body, "identity"),
        ip: extract_field(body, "ip")
      }
    else
      nil
    end
  end

  def self.build_pong(identity:, ip:, time: Time.current)
    {
      v: PROTOCOL_VERSION,
      type: "pong",
      identity: identity,
      ip: ip,
      unix: time.to_i,
      iso8601: time.iso8601,
      tz: Time.zone.tzinfo.name,
      tz_offset: time.utc_offset,
      tz_posix: ENV.fetch("UDP_DISCOVERY_TZ_POSIX", DEFAULT_TZ_POSIX)
    }.to_json
  end

  def self.build_ping(identity:)
    {
      v: PROTOCOL_VERSION,
      type: "ping",
      identity: identity
    }.to_json
  end

  def self.build_users_request(identity:)
    {
      v: PROTOCOL_VERSION,
      type: "users",
      identity: identity
    }.to_json
  end

  def self.build_users_reply(users)
    {
      v: PROTOCOL_VERSION,
      type: "users",
      users: users
    }.to_json
  end

  def self.build_therapy_request(identity:, user_id:)
    {
      v: PROTOCOL_VERSION,
      type: "therapy",
      identity: identity,
      user_id: user_id
    }.to_json
  end

  def self.build_therapy_reply(user_id:, name:, recommended_seconds:, error: nil)
    payload = {
      v: PROTOCOL_VERSION,
      type: "therapy",
      user_id: user_id
    }
    payload[:name] = name if name.present?
    payload[:recommended_seconds] = recommended_seconds if recommended_seconds
    payload[:error] = error if error.present?
    payload.to_json
  end

  def self.build_exposure_request(identity:, user_id:, duration_seconds:, unix:)
    {
      v: PROTOCOL_VERSION,
      type: "exposure",
      identity: identity,
      user_id: user_id,
      duration_seconds: duration_seconds,
      unix: unix
    }.to_json
  end

  def self.build_exposure_reply(ok:, error: nil, id: nil, user_id: nil, duration_seconds: nil, started_at: nil)
    payload = {
      v: PROTOCOL_VERSION,
      type: "exposure",
      ok: ok
    }
    payload[:error] = error if error.present?
    payload[:id] = id if id
    payload[:user_id] = user_id unless user_id.nil?
    payload[:duration_seconds] = duration_seconds if duration_seconds
    payload[:started_at] = started_at if started_at.present?
    payload.to_json
  end

  # IP of the interface this host would use to reach +remote_ip+.
  def self.local_ip_for(remote_ip)
    return ENV["UDP_DISCOVERY_IP"] if ENV["UDP_DISCOVERY_IP"].present?

    UDPSocket.open do |s|
      s.connect(remote_ip, 1)
      s.addr.last
    end
  rescue StandardError
    Socket.ip_address_list.find { |a| a.ipv4? && !a.ipv4_loopback? }&.ip_address || "0.0.0.0"
  end

  def self.extract_field(text, name)
    if text =~ /\b#{Regexp.escape(name)}=("([^"]*)"|'([^']*)'|(\S+))/i
      Regexp.last_match(2) || Regexp.last_match(3) || Regexp.last_match(4)
    end
  end

  def self.extract_positional_identity(body, verb)
    rest = body.sub(/\A#{Regexp.escape(verb)}\s+/i, "").strip
    return nil if rest.blank? || rest.include?("=")

    rest.split(/\s+/).first
  end

  private

  def run_loop
    socket = UDPSocket.new
    socket.setsockopt(Socket::SOL_SOCKET, Socket::SO_REUSEADDR, true)
    if defined?(Socket::SO_BROADCAST)
      socket.setsockopt(Socket::SOL_SOCKET, Socket::SO_BROADCAST, true)
    end
    socket.bind("0.0.0.0", @port)
    @socket = socket

    while @running
      begin
        ready = IO.select([ socket ], nil, nil, 1.0)
        next unless ready

        data, addr = socket.recvfrom_nonblock(MAX_PACKET)
        remote_ip = addr[3]
        remote_port = addr[1]

        reply = handle_packet(data, remote_ip)
        next unless reply

        socket.send(reply, 0, remote_ip, remote_port)
        @logger.info("[udp_discovery] reply → #{remote_ip}:#{remote_port} (#{reply})")
      rescue IO::WaitReadable
        next
      rescue StandardError => e
        break unless @running

        @logger.error("[udp_discovery] loop error: #{e.class}: #{e.message}")
        sleep 0.2
      end
    end
  ensure
    begin
      socket&.close
    rescue StandardError
      nil
    end
    @socket = nil if @socket.equal?(socket)
    @logger.info("[udp_discovery] stopped")
  end
end
