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
{"v":1,"type":"ping","identity":"esp32-b4bfe9e70e64",
 "app":"session_timer","version":"99eab52",
 "status":{"state":"entry","user":"Guest","lcd":["Guest      0:30","* clear  start #"],
           "led":"00:30","led_kind":"timer","lamp":false,"fan":false}}

// ESP → server when LCD/LED/mode changes (no reply expected)
{"v":1,"type":"status","identity":"esp32-b4bfe9e70e64","app":"session_timer","version":"99eab52",
 "status":{"state":"running","user":"rob","entry":"0:45","remain_seconds":29,
           "lcd":["rob        0:29","* abort  Running"],"led":"00:29","led_kind":"timer",
           "lamp":true,"fan":true}}

// Server → ESP (unicast to sender)
{"v":1,"type":"pong","identity":"dreamquest","ip":"192.168.1.163",
 "unix":1721830496,"iso8601":"2026-07-24T12:34:56-06:00",
 "tz":"America/Denver","tz_offset":-21600,"tz_posix":"MST7MDT,M3.2.0,M11.1.0",
 "published_version":"99eab52"}

// ESP → server (key A on session_timer): request user list
{"v":1,"type":"users","identity":"esp32-b4bfe9e70e64"}

// Server → ESP (household first; Guest id 0 always last for A+0)
{"v":1,"type":"users","users":[{"id":1,"name":"rob"},{"id":2,"name":"shirlene"},{"id":0,"name":"Guest"}]}

// ESP → server (key B): therapy + skin keypad lists
{"v":1,"type":"therapies","identity":"esp32-b4bfe9e70e64"}

// Server → ESP (stable keypad ids: 1 Manual, 2 Psoriasis, 3 Vitiligo, 4 Eczema)
{"v":1,"type":"therapies",
 "therapies":[{"id":1,"name":"Manual","uses_skin_type":false},
              {"id":2,"name":"Psoriasis","uses_skin_type":true},
              {"id":3,"name":"Vitiligo","uses_skin_type":false},
              {"id":4,"name":"Eczema","uses_skin_type":false}],
 "skin_types":[{"id":1,"name":"Type I"},{"id":2,"name":"Type II"},
               {"id":3,"name":"Type III"},{"id":4,"name":"Type IV"},
               {"id":5,"name":"Type V"},{"id":6,"name":"Type VI"}]}

// ESP → server (B then digits): assign to selected user (0 = Guest)
{"v":1,"type":"assign_therapy","identity":"esp32-…","user_id":4,"therapy_id":2,"skin_id":1}

// Server → ESP
{"v":1,"type":"assign_therapy","ok":true,"user_id":4,"therapy_id":2,"skin_id":1}

// ESP → server (key A, then digit = user id): therapy recommendation
{"v":1,"type":"therapy","identity":"esp32-b4bfe9e70e64","user_id":4}

// Server → ESP. recommended_seconds is last exposure after 44h, 0 if more recent,
// else initial_seconds. * on the module restores initial_seconds.
// Optional "message" is shown on the module 16x2 after A+digit, then entry UI resumes.
// Server fills message from that user's newest Exposure (e.g. "Last session 1d 22h ago").
// C = recommended + step, D = recommended − step.
// Happy path: recommended_seconds is last duration. If recommended is 0, C and D stay at 0.
{"v":1,"type":"therapy","user_id":4,"name":"miriam","recommended_seconds":50,
 "step_seconds":16,"max_seconds":333,"initial_seconds":50,
 "last_duration_seconds":90,"therapy_id":2,"skin_id":1,
 "message":"Last session\n1:30 0d9h44m ago"}

// ESP → server when the lamp turns off (complete or abort with ≥1 s on)
{"v":1,"type":"exposure","identity":"esp32-…","user_id":0,"duration_seconds":28,
 "unix":1721830496,"therapy_id":2,"skin_id":1}

// Server → ESP
{"v":1,"type":"exposure","ok":true,"id":12,"user_id":0,"duration_seconds":28,"started_at":"…"}
```

| Field | Where | Meaning |
|-------|--------|---------|
| `v` | both | Protocol version (**1**) |
| `type` | both | `"ping"`, `"pong"`, `"users"`, `"therapies"`, `"assign_therapy"`, `"therapy"`, `"exposure"`, `"status"`, or `"ota"` |
| `ota` | web → server → module | Immediate firmware check (`/devices` **Check for update**) |
| `identity` | both | Device id (`esp32-` + MAC) or server hostname |
| `app` | ping / pong | Firmware app name (`session_timer`) |
| `version` | ping | Running image version (git short SHA from `esp_app_desc`) |
| `published_version` | pong | OTA `manifest.json` version on the server, when present |
| `status` | ping / status | UI snapshot: `state`, `user`, `lcd` (2×16), `led`, lamp/fan |
| `ip` | pong | Server LAN IP the module should use |
| `unix` | pong / exposure | UTC Unix seconds (clock set / **lamp-off end time**) |
| `iso8601` | pong | Human-readable local time (logging / debug) |
| `tz` | pong | IANA zone name (e.g. `America/Denver`) |
| `tz_offset` | pong | Seconds east of UTC at pong time (e.g. `-21600`) |
| `tz_posix` | pong | POSIX `TZ` string for ESP `setenv` (override: `UDP_DISCOVERY_TZ_POSIX`) |
| `users` | users reply | Household ids 1–9, then **`{id:0,name:"Guest"}` last** |
| `therapies` | therapies reply | Keypad **1–4**: Manual, Psoriasis, Vitiligo, Eczema; `uses_skin_type` for psoriasis |
| `skin_types` | therapies reply | Keypad **1–6** (Table 1 I–VI) |
| `therapy_id` | assign / therapy / exposure | Keypad **1–4** (Manual / Psoriasis / Vitiligo / Eczema). Therapy reply is the user's current assignment; lamp-off log stores it on the Exposure |
| `skin_id` | assign / therapy / exposure | Skin keypad **1–6** when the therapy uses a skin type |
| `user_id` | therapy / assign / exposure | Key digit **0–9** (0 = Guest) |
| `recommended_seconds` | therapy reply | Suggested light-on duration; module loads MMSS entry. Last exposure after 44h, **0** if more recent, else `initial_seconds` |
| `step_seconds` | therapy reply | Increment for keys **C** / **D** against `recommended_seconds`. From the user's newest therapy assignment (EGT / Manual **15**), else **10** |
| `max_seconds` | therapy reply | Hard cap for programmed time. EGT listed max, Manual / none **1200** (20:00) |
| `initial_seconds` | therapy reply | First-session dose. EGT listed initial (psoriasis I–II **50**, III–IV **83**, V–VI **133**; vitiligo / eczema **50**); Manual / none **30**. Key **\*** restores this |
| `last_duration_seconds` | therapy reply | Newest exposure duration for that user (`0` if none) |
| `message` | therapy reply | Optional free text for the 16x2 (up to ~32 chars shown; held ~5s) |
| `duration_seconds` | exposure | Actual lamp-on seconds for this run |
| `error` | therapy / assign / exposure | Optional: `"not_found"`, `"bad_user_id"`, `"need_skin"`, `"bad_duration"` |

**Guest:** seeded `User` with **id 0**, name `Guest`. Always last in the key-A list; select with **A** then **0**. Also the default label when nobody is selected.

| Piece | Value |
|-------|--------|
| Port | **UDP 3000** (Rails HTTP is **TCP 3000**) |
| ESP identity | `esp32-` + Wi‑Fi STA MAC, 12 hex digits, no colons |
| Server identity | Hostname, or `UDP_DISCOVERY_IDENTITY` |

Legacy text `PHOTOTHERAPY/1 PING …` is still **accepted** by the server for transition; modules send **JSON only**.

## How the module finds the server (no hard-coded LAN IP)

After DHCP the ESP sends UDP **ping** on port **3000** to:

1. **Last known server** — from the previous pong (RAM) or NVS key `discovery/server_ip`
2. **Default gateway** (DHCP)
3. **Subnet broadcast**
4. **`255.255.255.255`**

A **pong** supplies `identity`, `ip` (stored for users/therapy/exposure + NVS hint), and wall clock
`unix` / timezone. Follow-up traffic is **unicast** only to that discovered `ip`.

## Time source priority

1. **UDP discovery pong** `unix` field (preferred).
2. **SNTP** public pools (`pool.ntp.org`, `time.google.com`) only if discovery fails or returns no usable `unix`.

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
discovery payload: {"v":1,"type":"ping","identity":"esp32-…","app":"session_timer","version":"…"}
discovery pong … identity=… ip=… time_from_disc=1
firmware matches published …
wall time from discovery unix=…
server known: …
```

**Rails log:**

```text
[udp_discovery] ping from 192.168.1.x identity=esp32-… app=session_timer version=…
[udp_discovery] Device#N ip=… identity=esp32-… fw=…
[udp_discovery] pong → 192.168.1.x:… ({"v":1,"type":"pong",…,"unix":…})
```

**UI:** http://&lt;host&gt;:3000/devices (login required).

## Self-test (host)

```bash
python3 - <<'PY'
import json, socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
ping = json.dumps({"v":1,"type":"ping","identity":"manual-test","app":"session_timer","version":"dev"})
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
| Falls back to SNTP always | Rails not running; UDP 3000 blocked; AP client isolation blocking broadcast |
| `devices.ip` is `172.x` | Docker userland-proxy rewriting source IP — see `server/docs/deploy-ami.md` |

## Related

- [wifi-config.md](wifi-config.md) — NVS secrets
- [server/README.md](../server/README.md) — Rails setup
- Code: `server/app/services/udp_discovery_listener.rb`,
  `esp32_firmware/apps/session_timer/main/main.c`,
  `esp32_firmware/apps/wifi_connect/main/main.c`
