# Wi‑Fi OTA (ESP-IDF)

LAN over-the-air firmware updates for product apps so an installed module does
not need USB access for routine upgrades.

**Status:** dual-OTA partitions, `uh_ota`, Rails publish, and product
`session_timer` idle checks are live on hardware (SHA-256 verify, reboot into
the inactive slot).

## Behavior summary

| Item | Detail |
|------|--------|
| Apps | `session_timer` (product), `ota_smoke` (bench), `wifi_connect` (same table) |
| Poll | First check ~20 s after boot (after mark-valid); then every ~15 min when idle |
| Trigger | Key **B**, or **Check for update** on `/devices` (UDP `type:ota` to the module) |
| Gates | No OTA during a lamp session or A-key user-list paging |
| Version | Build stamps `esp_app_desc.version` with git short SHA (`PROJECT_VER`); `ota-publish` copies that into `manifest.json`. Module ping reports `version`; `/devices` compares it to published |
| Host | Discovered Rails IP or NVS `discovery/server_ip` |

## Why

- Timer board can be awkward to reach once installed.
- Dual-slot OTA is required for reliable Wi‑Fi upgrades (inactive slot + reboot).
- Discovery already finds the LAN server; OTA reuses that host for HTTP.

## Flash layout (4MB dual OTA)

Project table: [`esp32_firmware/partitions/partitions_two_ota_4mb.csv`](../esp32_firmware/partitions/partitions_two_ota_4mb.csv)

| Partition | Role |
|-----------|------|
| `nvs` @ **0x9000** / 0x6000 | Wi‑Fi + discovery hint (same offset as `nvs-wifi`) |
| `otadata` | Which OTA slot boots |
| `phy_init` | RF calibration |
| `ota_0` / `ota_1` | ~1.94 MiB each — app + headroom |

No `factory` slot (more room for growth). **USB serial remains recovery.**

### One-time USB flash

Changing the partition table **requires a full serial flash** (not app-only):

```bash
./scripts/fw idf upload session_timer
./scripts/fw idf nvs-wifi    # if NVS is empty or first provision
```

After that, routine upgrades target the inactive OTA slot over LAN.

## Apps using the OTA table

| App | Notes |
|-----|--------|
| `session_timer` | Product UI + discovery; idle LAN OTA (above) |
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

To check **now** instead of waiting ~15 minutes: press **B** on the keypad, or
**Check for update** on `/devices`. The module listens on UDP **3000** for
`{"v":1,"type":"ota","identity":"esp32-…"}`. Busy sessions defer until idle.

Publish a new build from a machine with the toolchain and SSH/docker on ami
(`OTA_SSH` defaults to the first reachable of `ami` or `deploy@192.168.1.202`):

```bash
./scripts/fw idf ota-publish session_timer
# re-flash the same version (recovery / force):
OTA_FORCE=1 ./scripts/fw idf ota-publish session_timer
```

**Pi (USB programming host):** module on `/dev/ttyUSB0`; repo often under
`~/User-Hackable/Phototherapy_Timer`. After the one-time dual-OTA flash:

```bash
./scripts/fw idf ota-publish session_timer
./scripts/fw idf monitor session_timer
```

Serial while testing:

```text
OTA check http://192.168.1.202/firmware/session_timer/ (running …)
uh_ota: SHA-256 OK
uh_ota: OTA complete — rebooting into ota_1
```

### Smoke test (bench)

```bash
# 1. Dual-OTA flash + Wi‑Fi / server hint
./scripts/fw idf nvs-wifi          # secrets/wifi.yaml may include server_ip
./scripts/fw idf upload ota_smoke

# 2. Publish image to ami Rails storage
./scripts/fw idf ota-publish ota_smoke
#    http://192.168.1.202/firmware/ota_smoke/manifest.json
#    http://192.168.1.202/firmware/ota_smoke/app.bin

# 3. Watch serial for OTA + reboot into the other slot
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
