# Device discovery (ESP32 ↔ Rails)

LAN bring-up so the phototherapy board and the host Rails app find each other
and share **wall clock time** over UDP JSON. SNTP is a **fallback** if discovery
does not return a usable time.

| Side | Role |
|------|------|
| **ESP32** (`session_timer`, `wifi_connect`) | After DHCP: **ping**; `session_timer` also **users**, **therapy**, **exposure** |
| **Rails** (`server/`) | UDP **3000**: **pong** (time + timezone), **users**, **therapy**, **exposure** log; upserts `Device` |

## Wire protocol (JSON v1)

UTF-8 JSON objects (single datagram, no framing):

```json
// ESP → server (unicast to known host and/or broadcast)
{"v":1,"type":"ping","identity":"esp32-b4bfe9e70e64"}

// Server → ESP (unicast to sender)
{"v":1,"type":"pong","identity":"dreamquest","ip":"192.168.1.163",
 "unix":1721830496,"iso8601":"2026-07-24T12:34:56-06:00",
 "tz":"America/Denver","tz_offset":-21600,"tz_posix":"MST7MDT,M3.2.0,M11.1.0"}

// ESP → server (key A on session_timer): request user list
{"v":1,"type":"users","identity":"esp32-b4bfe9e70e64"}

// Server → ESP (household first; Guest id 0 always last for A+0)
{"v":1,"type":"users","users":[{"id":1,"name":"rob"},{"id":2,"name":"shirlene"},{"id":0,"name":"Guest"}]}

// ESP → server (key A, then digit = user id): therapy recommendation
{"v":1,"type":"therapy","identity":"esp32-b4bfe9e70e64","user_id":4}

// Server → ESP (default recommended_seconds is 30 until per-user schedules exist)
{"v":1,"type":"therapy","user_id":4,"name":"miriam","recommended_seconds":30}

// ESP → server when the lamp turns off (complete or abort with ≥1 s on)
{"v":1,"type":"exposure","identity":"esp32-…","user_id":0,"duration_seconds":28,"unix":1721830496}

// Server → ESP
{"v":1,"type":"exposure","ok":true,"id":12,"user_id":0,"duration_seconds":28,"started_at":"…"}
```

| Field | Where | Meaning |
|-------|--------|---------|
| `v` | both | Protocol version (**1**) |
| `type` | both | `"ping"`, `"pong"`, `"users"`, `"therapy"`, or `"exposure"` |
| `identity` | both | Device id (`esp32-` + MAC) or server hostname |
| `ip` | pong | Server LAN IP the module should use |
| `unix` | pong / exposure | UTC Unix seconds (clock set / **lamp-off end time**) |
| `iso8601` | pong | Human-readable local time (logging / debug) |
| `tz` | pong | IANA zone name (e.g. `America/Denver`) |
| `tz_offset` | pong | Seconds east of UTC at pong time (e.g. `-21600`) |
| `tz_posix` | pong | POSIX `TZ` string for ESP `setenv` (override: `UDP_DISCOVERY_TZ_POSIX`) |
| `users` | users reply | Household ids 1–9, then **`{id:0,name:"Guest"}` last** |
| `user_id` | therapy / exposure | Key digit **0–9** (0 = Guest) |
| `recommended_seconds` | therapy reply | Suggested light-on duration; module loads MMSS entry |
| `duration_seconds` | exposure | Actual lamp-on seconds for this run |
| `error` | therapy / exposure | Optional: `"not_found"`, `"bad_user_id"`, `"bad_duration"` |

**Guest:** seeded `User` with **id 0**, name `Guest`. Always last in the key-A list; select with **A** then **0**. Also the default label when nobody is selected.

| Piece | Value |
|-------|--------|
| Port | **UDP 3000** (Rails HTTP is **TCP 3000**) |
| ESP identity | `esp32-` + Wi‑Fi STA MAC, 12 hex digits, no colons |
| Server identity | Hostname, or `UDP_DISCOVERY_IDENTITY` |

Legacy text `PHOTOTHERAPY/1 PING …` is still **accepted** by the server for transition; modules send **JSON only**.

## Time source priority

1. **UDP discovery pong** `unix` field (preferred).
2. **SNTP** — LAN host (`192.168.1.163` in firmware) then `pool.ntp.org` / `time.google.com` if discovery fails or returns no usable `unix`.

Periodic discovery (about every 5 minutes) refreshes server identity and can refresh clock from the pong. SNTP is not used while discovery has already set time.

## Host setup (one-time)

### 1. Rails app

```bash
cd server
bin/setup
bin/rails db:seed   # optional users
bin/rails server -b 0.0.0.0 -p 3000
```

Log on boot:

```text
[udp_discovery] listening on UDP 0.0.0.0:3000 (JSON v1)
```

### 2. Firewall

```bash
sudo ufw allow 3000/udp comment 'Phototherapy device discovery'
sudo ufw allow 3000/tcp comment 'Phototherapy Rails HTTP'   # optional
```

### 3. Wi‑Fi credentials on the module

```bash
cp secrets/wifi.yaml.example secrets/wifi.yaml   # once
$EDITOR secrets/wifi.yaml
./scripts/fw idf nvs-wifi
```

### 4. Flash

```bash
./scripts/fw idf upload session_timer   # product UI + discovery + clock
# or
./scripts/fw idf upload wifi_connect    # bring-up only
./scripts/fw idf monitor session_timer
```

## Success criteria

**ESP log:**

```text
device identity: esp32-…
DHCP …
discovery payload: {"v":1,"type":"ping","identity":"esp32-…"}
discovery pong … identity=… ip=… time_from_disc=1
wall time from discovery unix=…
server known: …
```

**Rails log:**

```text
[udp_discovery] ping from 192.168.1.x identity=esp32-…
[udp_discovery] Device#N ip=… identity=esp32-…
[udp_discovery] pong → 192.168.1.x:… ({"v":1,"type":"pong",…,"unix":…})
```

**UI:** http://&lt;host&gt;:3000/devices (login required).

## Self-test (host)

```bash
python3 - <<'PY'
import json, socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
ping = json.dumps({"v":1,"type":"ping","identity":"manual-test"})
s.sendto(ping.encode(), ("127.0.0.1", 3000))
data, addr = s.recvfrom(1024)
print(addr, data.decode())
print("unix", json.loads(data)["unix"], "local", time.ctime(json.loads(data)["unix"]))
PY
```

## Troubleshooting

| Symptom | Check |
|---------|--------|
| No pong, Rails silent | UFW / firewall; Rails `0.0.0.0`; UDP listener log on boot |
| Pong without `unix` | Server clock; Time.zone |
| Discovery works, clock wrong | TZ on module (`MST7MDT…`); `unix` is UTC |
| Falls back to SNTP always | Rails not running; wrong host IP in firmware (`SNTP_SERVER_LAN`) |

## Related

- [wifi-config.md](wifi-config.md) — NVS secrets
- [server/README.md](../server/README.md) — Rails setup
- Code: `server/app/services/udp_discovery_listener.rb`,
  `esp32_firmware/apps/session_timer/main/main.c`,
  `esp32_firmware/apps/wifi_connect/main/main.c`
