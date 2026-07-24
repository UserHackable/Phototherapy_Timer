# PhototherapyServer

Rails 8 app that tracks phototherapy controller boards on the LAN and serves
the future web UI.

## Requirements

- Ruby (see `.ruby-version`; mise recommended)
- SQLite (dev/test)

```bash
cd server
bin/setup          # bundle + db:prepare
bin/rails server -b 0.0.0.0 -p 3000
```

Puma serves **HTTP on TCP 3000**. On boot it also starts **UDP discovery on
port 3000** (same number, different protocol).

## UDP device discovery

When an ESP32 comes online it broadcasts/unicasts:

```text
PHOTOTHERAPY/1 PING identity=esp32-<mac12hex>
```

The server:

1. Logs the sender IP
2. Creates or updates a `Device` (`identity` preferred; always refreshes `ip`)
3. Replies unicast:

```text
PHOTOTHERAPY/1 PONG identity=<hostname> ip=<this-host-lan-ip>
```

Implementation: `app/services/udp_discovery_listener.rb` (started from
`config/puma.rb` via `on_booted`).

### ENV

| Variable | Default | Meaning |
|----------|---------|---------|
| `UDP_DISCOVERY` | `1` | Set `0` to disable the listener |
| `UDP_DISCOVERY_PORT` | `3000` | UDP port |
| `UDP_DISCOVERY_IDENTITY` | hostname | Name in PONG |
| `UDP_DISCOVERY_IP` | auto | Force IP advertised in PONG |

### Firewall (Arch / UFW)

Inbound UDP from the LAN must be allowed or PINGs never reach Rails:

```bash
sudo ufw allow 3000/udp comment 'Phototherapy device discovery'
# optional if you open the web UI from other machines:
sudo ufw allow 3000/tcp comment 'Phototherapy Rails HTTP'
sudo ufw status
```

Self-test from the same machine:

```bash
echo -n 'PHOTOTHERAPY/1 PING identity=manual-test' | nc -u -w1 127.0.0.1 3000
# or:
python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(2);s.sendto(b'PHOTOTHERAPY/1 PING identity=manual-test',('127.0.0.1',3000));print(s.recvfrom(512))"
```

Then open http://localhost:3000/devices — a row should appear.

### Devices UI

| Path | Role |
|------|------|
| `/devices` | List known boards (identity + IP) |
| `/devices/:id` | Show / edit |

## Database

```bash
bin/rails db:migrate
bin/rails test
```

`Device` fields: `identity` (unique, optional), `ip`, timestamps.

## Full stack with the ESP32

See [docs/wifi-config.md](../docs/wifi-config.md) and
[docs/device-discovery.md](../docs/device-discovery.md).
