# Device discovery (ESP32 ↔ Rails)

LAN bring-up so the phototherapy board and the host Rails app find each other
without hard-coding only one side of the address pair.

| Side | Role |
|------|------|
| **ESP32** (`wifi_connect`) | After DHCP, sends UDP **PING** with stable identity (MAC-based) |
| **Rails** (`server/`) | Listens on UDP **3000**, upserts `Device`, replies **PONG** with hostname + LAN IP |

## Wire protocol

UTF-8, one line:

```text
# ESP → server (unicast to known host and/or broadcast)
PHOTOTHERAPY/1 PING identity=esp32-b4bfe9e70e64

# Server → ESP (unicast to sender)
PHOTOTHERAPY/1 PONG identity=dreamquest ip=192.168.1.163
```

Also accepted on the server: bare `PING <identity>`.

| Piece | Value |
|-------|--------|
| Port | **UDP 3000** (Rails HTTP is **TCP 3000** — fine to share the number) |
| ESP identity | `esp32-` + Wi‑Fi STA MAC, 12 hex digits, no colons |
| Server identity | Hostname, or `UDP_DISCOVERY_IDENTITY` |

## Host setup (one-time)

### 1. Rails app

```bash
cd server
bin/setup
bin/rails server -b 0.0.0.0 -p 3000
```

Log line on boot:

```text
[udp_discovery] listening on UDP 0.0.0.0:3000
```

### 2. Firewall

If **UFW** (or similar) is active, allow discovery or packets from the board
are dropped before Rails sees them:

```bash
sudo ufw allow 3000/udp comment 'Phototherapy device discovery'
sudo ufw allow 3000/tcp comment 'Phototherapy Rails HTTP'   # optional
sudo ufw status
```

Symptoms of a blocked port: ESP logs `discovery PING → …` then
`no server PONG`; Rails log never shows `PING from …`.

### 3. Optional: local NTP (chrony)

`wifi_connect` prefers SNTP at the build PC (`192.168.1.163` in firmware —
adjust if your host IP changes). Serving NTP on that machine:

```bash
sudo pacman -S chrony
sudo systemctl disable --now systemd-timesyncd
# ensure /etc/chrony.conf has something like:
#   allow 192.168.1.0/24
#   local stratum 10
sudo systemctl enable --now chronyd
```

Reserve a DHCP address for the PC so the hardcoded SNTP / discovery unicast
target stays valid.

### 4. Wi‑Fi credentials on the module

```bash
# repo root
cp secrets/wifi.yaml.example secrets/wifi.yaml   # once
$EDITOR secrets/wifi.yaml                        # ssid + password
./scripts/fw idf nvs-wifi
```

### 5. Flash and monitor

```bash
./scripts/fw idf upload wifi_connect
./scripts/fw idf monitor wifi_connect
```

## Success criteria

**ESP log:**

```text
device identity: esp32-…
DHCP got ip: 192.168.1.x
network time applied: …
discovery PING → 192.168.1.163:3000 (known LAN host)
discovery PONG from 192.168.1.163: identity=… ip=192.168.1.163
server known: identity=… ip=192.168.1.163
```

**Rails log:**

```text
[udp_discovery] PING from 192.168.1.x identity=esp32-…
[udp_discovery] Device#N ip=192.168.1.x identity=esp32-…
[udp_discovery] PONG → 192.168.1.x:…
```

**UI:** http://&lt;host&gt;:3000/devices shows the board.

## ESP discovery behavior (`wifi_connect`)

After DHCP + SNTP:

1. Build identity from STA MAC.
2. Send PING to, in order:
   - known LAN host (`SNTP_SERVER_LAN` in `main.c`, currently `192.168.1.163`)
   - default gateway
   - subnet broadcast
   - `255.255.255.255`
3. Wait up to ~2 s for a PONG; retry up to 3 rounds.
4. Re-run on reconnect and every **5 minutes**.

Unicast to the known host is important: many APs handle broadcasts poorly;
UFW must still allow **inbound UDP 3000** for unicast from the board.

## Troubleshooting

| Symptom | Check |
|---------|--------|
| No PONG, Rails silent | UFW / firewall; Rails bound `0.0.0.0`; UDP listener log on boot |
| No PONG, Rails sees PING | Reply path / wrong `UDP_DISCOVERY_IP` |
| Device IP wrong / missing | Identity upsert: same MAC keeps one row and updates IP |
| SNTP slow / year 1969 | LAN chrony down or blocked; public pools are fallbacks (`CONFIG_LWIP_SNTP_MAX_SERVERS=3`) |
| Host IP changed | Update `SNTP_SERVER_LAN` in `wifi_connect` main.c, reflash; DHCP-reserve the PC |

## Related docs

- [wifi-config.md](wifi-config.md) — NVS secrets, `nvs-wifi`, scan vs connect
- [server/README.md](../server/README.md) — Rails-only setup and ENV
- Protocol implementation: `server/app/services/udp_discovery_listener.rb`,
  `esp32_firmware/apps/wifi_connect/main/main.c`
