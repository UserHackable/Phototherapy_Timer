# frozen_string_literal: true

require "socket"

# UDP discovery for ESP32 ↔ Rails on the LAN.
#
# Listens for broadcast/unicast pings on UDP (default port 3000 — independent of
# Puma's TCP HTTP port). On each client PING:
#   1. Log the sender IP
#   2. Create/update a Device with that IP (and optional identity)
#   3. Unicast a PONG identifying this server and its LAN IP
#
# Wire protocol (UTF-8 text, one line preferred):
#   Client:  PHOTOTHERAPY/1 PING identity=<id>
#   Server:  PHOTOTHERAPY/1 PONG identity=<name> ip=<server_ip>
#
# ENV:
#   UDP_DISCOVERY_PORT     (default 3000)
#   UDP_DISCOVERY          set to "0" to disable
#   UDP_DISCOVERY_IDENTITY server name in PONG (default hostname)
#
class UdpDiscoveryListener
  PROTOCOL = "PHOTOTHERAPY/1"
  DEFAULT_PORT = 3000
  MAX_PACKET = 1500

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
      @logger.info("[udp_discovery] listening on UDP 0.0.0.0:#{@port}")
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

  # Process one payload (used by the loop and by tests).
  # Returns the PONG string if a reply should be sent, else nil.
  def handle_packet(payload, remote_ip)
    text = payload.to_s.strip
    return nil if text.empty?

    parsed = self.class.parse_message(text)
    return nil unless parsed
    return nil if parsed[:type] == :pong

    unless parsed[:type] == :ping
      @logger.debug("[udp_discovery] ignore from #{remote_ip}: #{text.truncate(120)}")
      return nil
    end

    identity = parsed[:identity]
    @logger.info(
      "[udp_discovery] PING from #{remote_ip}" \
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
  rescue StandardError => e
    @logger.error("[udp_discovery] handle_packet error: #{e.class}: #{e.message}")
    nil
  end

  def self.parse_message(text)
    line = text.to_s.strip
    return nil if line.empty?

    # Optional protocol prefix
    body = line.sub(/\A#{Regexp.escape(PROTOCOL)}\s+/i, "")

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

  def self.build_pong(identity:, ip:)
    "#{PROTOCOL} PONG identity=#{identity} ip=#{ip}"
  end

  def self.build_ping(identity:)
    "#{PROTOCOL} PING identity=#{identity}"
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
    # "PING my-esp-id" → my-esp-id (when not key=value form)
    rest = body.sub(/\A#{Regexp.escape(verb)}\s+/i, "").strip
    return nil if rest.blank? || rest.include?("=")

    rest.split(/\s+/).first
  end

  private

  def run_loop
    socket = UDPSocket.new
    socket.setsockopt(Socket::SOL_SOCKET, Socket::SO_REUSEADDR, true)
    # Receive broadcasts directed at this port
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

        pong = handle_packet(data, remote_ip)
        next unless pong

        socket.send(pong, 0, remote_ip, remote_port)
        @logger.info("[udp_discovery] PONG → #{remote_ip}:#{remote_port} (#{pong})")
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
