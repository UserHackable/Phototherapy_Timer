# Wi‑Fi OTA (ESP-IDF)

LAN over-the-air firmware updates for product apps so an installed module does
not need USB access for routine upgrades.

Status: **dual-OTA partitions + `uh_ota` + `ota_smoke` + Rails publish proven
on hardware** (SHA-256 OK, reboot into new slot). **`session_timer` runs LAN
OTA when idle** (not during a lamp session or user-list paging). First boot
after a dual-OTA USB flash checks for updates ~20 s after mark-valid; later
polls are every ~15 min.

**Version:** build stamps `esp_app_desc.version` with the git short SHA
(`PROJECT_VER`). `ota-publish` writes that same string into `manifest.json`
so “already up to date” works (no re-flash loop).

## Why

- Timer board can be awkward to reach once installed.
- A second ESP cannot reprogram over Wi‑Fi without OTA (or wired UART).
- Discovery already finds the LAN server; OTA reuses that host for HTTP.

## Flash layout (4MB dual OTA)

Project table: [`esp32_firmware/partitions/partitions_two_ota_4mb.csv`](../esp32_firmware/partitions/partitions_two_ota_4mb.csv)

| Partition | Role |
|-----------|------|
| `nvs` @ **0x9000** / 0x6000 | Wi‑Fi + discovery hint (`nvs-wifi` unchanged) |
| `otadata` | Which OTA slot boots |
| `phy_init` | RF calibration |
| `ota_0` / `ota_1` | ~1.94 MiB each — app + headroom |

No `factory` slot (more room for growth). **USB serial remains recovery.**

### One-time USB flash

Changing the partition table **requires a full serial flash** (not app-only):

```bash
./scripts/fw idf upload session_timer
./scripts/fw idf nvs-wifi    # if NVS was erased or first provision
```

After that, future updates target the inactive OTA slot via LAN OTA.

## Apps using the OTA table

| App | Notes |
|-----|--------|
| `session_timer` | Product UI + discovery; **checks OTA ~every 15 min when idle** |
| `wifi_connect` | Bring-up / discovery; same table for consistency |
| `ota_smoke` | Minimal proof: Wi‑Fi + `uh_ota` against NVS `server_ip` |

Other apps may stay on single-app until needed.

### Product app (`session_timer`)

1. **One-time USB** full flash (dual-OTA table) + `nvs-wifi` if needed.
2. Module discovers the Rails host (UDP) or uses NVS `discovery/server_ip`.
3. When **not** running a session (and not in key-A user list), it GETs  
   `http://<server>/firmware/session_timer/manifest.json`.
4. If `version` differs (or `force`), downloads `app.bin`, verifies **SHA-256**,
   writes the inactive slot, reboots.
5. After a healthy boot it marks the image valid (bootloader rollback cancelled).

Publish a new build from a machine with the toolchain and SSH/docker on ami
(`OTA_SSH` defaults to `ami` or `deploy@192.168.1.202`):

```bash
./scripts/fw idf ota-publish session_timer
# force re-flash same version (proof / recovery):
OTA_FORCE=1 ./scripts/fw idf ota-publish session_timer
```

**Pi (USB programming host):** module on `/dev/ttyUSB0`; repo often under
`~/User-Hackable/Phototherapy_Timer`. One-time dual-OTA flash, then LAN only:

```bash
./scripts/fw idf upload session_timer
./scripts/fw idf ota-publish session_timer   # OTA_SSH=ami if needed
./scripts/fw idf monitor session_timer
```

Serial (optional) while testing:

```text
OTA check http://192.168.1.202/firmware/session_timer/ (running …)
uh_ota: SHA-256 OK
uh_ota: OTA complete — rebooting into ota_1
```

### Smoke test (bench)

```bash
# 1. Dual-OTA flash + Wi‑Fi / server hint
./scripts/fw idf nvs-wifi          # secrets/wifi.yaml includes server_ip:
./scripts/fw idf upload ota_smoke

# 2. Host firmware files on ami (example paths; Rails later)
#    http://192.168.1.202/firmware/ota_smoke/manifest.json
#    http://192.168.1.202/firmware/ota_smoke/app.bin
#
# manifest.json example:
# {
#   "v": 1,
#   "app": "ota_smoke",
#   "version": "NEWER_THAN_RUNNING",
#   "sha256": "<sha256 of app.bin>",
#   "url": "/firmware/ota_smoke/app.bin"
# }

# 3. Bump version in a rebuild, publish, watch serial for OTA + reboot
./scripts/fw idf monitor ota_smoke
```

Shared client: `esp32_firmware/components/uh_ota/`.

## Trust model

| Layer | Approach |
|-------|----------|
| Transport | **HTTP on LAN** (`CONFIG_OTA_ALLOW_HTTP`) |
| Integrity | **SHA-256** in `manifest.json` |
| Host | Discovered server IP / NVS hint — no hard-coded LAN IP in C |
| Safety | No OTA while session running or users list open; mark app valid after healthy boot (rollback) |
| Auth | Unauthenticated on private LAN (same as discovery UDP) — do not expose to the internet |

## Publish surface

```text
GET http://<server>/firmware/<app>/manifest.json
GET http://<server>/firmware/<app>/app.bin
```

CLI: `./scripts/fw idf ota-publish <app>` (builds, writes manifest, copies into ami Rails volume).

## Related

- [device-discovery.md](device-discovery.md) — UDP find server
- [wifi-config.md](wifi-config.md) — NVS credentials
- [toolchain.md](toolchain.md) — `./scripts/fw idf …`
