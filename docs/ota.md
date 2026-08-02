# Wi‑Fi OTA (ESP-IDF)

LAN over-the-air firmware updates for product apps so an installed module does
not need USB access for routine upgrades.

Status: **dual-OTA partitions + `uh_ota` + `ota_smoke` + Rails publish proven
on hardware** (SHA-256 OK, reboot into new slot). `session_timer` product
integration (safety gates) is next.

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

After that, future updates target the inactive OTA slot (once the OTA client is wired).

## Apps using the OTA table

| App | Notes |
|-----|--------|
| `session_timer` | Product UI + discovery; OTA client TBD |
| `wifi_connect` | Bring-up / discovery; same table for consistency |
| `ota_smoke` | Minimal proof: Wi‑Fi + `uh_ota` against NVS `server_ip` |

Other apps may stay on single-app until needed.

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

## Trust model (planned)

| Layer | Approach |
|-------|----------|
| Transport | **HTTP on LAN** (`CONFIG_OTA_ALLOW_HTTP`) |
| Integrity | **SHA-256** in `manifest.json` |
| Host | Discovered server IP / NVS hint — no hard-coded LAN IP in C |
| Safety | No OTA while lamps/session running; mark app valid after healthy boot (rollback) |

## Publish surface (planned)

```text
GET http://<server>/firmware/<app>/manifest.json
GET http://<server>/firmware/<app>/app.bin
```

CLI (planned): `./scripts/fw idf ota-publish <app>`

## Related

- [device-discovery.md](device-discovery.md) — UDP find server
- [wifi-config.md](wifi-config.md) — NVS credentials
- [toolchain.md](toolchain.md) — `./scripts/fw idf …`
