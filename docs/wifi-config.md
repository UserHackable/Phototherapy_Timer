# Wi‑Fi configuration (multi-network, secrets-safe)

## Goals

- Module can join **several known** home networks (e.g. `Ferney`, `FamilyGuy`).
- **Passwords never enter git.**
- Build products that embed passwords (`*.bin`, generated headers) stay out of git.
- Same host inventory script can list SSIDs; passwords stay in a separate secrets file.

## Recommended layout

```text
secrets/
  wifi.yaml.example     # committed template
  wifi.yaml             # gitignored — real credentials
  generated/            # gitignored — optional dumps

esp32_firmware/apps/
  wifi_scan/            # list visible APs (no secrets)
  wifi_connect/         # STA connect using generated credentials
  blink/                # unrelated bring-up
```

### `secrets/wifi.yaml` shape

```yaml
---
- ssid: Ferney
  password: "correct-horse-battery"
- ssid: FamilyGuy
  password: "other-secret"
```

SSIDs only (no passwords) can still live in `known_wifi.yaml` from
`./scripts/export-known-wifi.sh` for documentation or merge helpers.

## Runtime strategy on the module

**Scan → match known list → pick best RSSI → connect.**

1. Load table of `{ssid, password}` compiled from `secrets/wifi.yaml`.
2. `esp_wifi` STA start + active scan.
3. For each seen AP whose SSID is in the table, keep the one with highest RSSI.
4. `esp_wifi_set_config` + `esp_wifi_connect` with that network’s password.
5. Wait for `IP_EVENT_STA_GOT_IP`; log IP / reconnect on disconnect.

Why not “try networks in YAML order only”? Scanning first handles “which AP is actually here and strongest” when several known SSIDs are in range (home + guest, two floors, etc.).

### Future (better long-term)

| Approach | Pros | Cons |
|----------|------|------|
| **Build-time secrets** (current) | Simple; no first-boot UI | Password lives in flash image; rebuild to change nets |
| **NVS after first provision** | Rebuild without secrets; change nets without full reflash of logic | Need a provision path once (serial, SoftAP, BLE) |
| **SoftAP / captive portal** | Phone-friendly setup | More code; security UX care |
| **BLE provisioning (BluFi / custom)** | No temporary open AP | More moving parts |

Reasonable evolution: keep YAML for **dev flash**, later copy successful credentials into **NVS** and prefer NVS on boot.

## Host workflow (current: one network in NVS)

```bash
# 1. Credentials on the PC (gitignored)
cp secrets/wifi.yaml.example secrets/wifi.yaml
$EDITOR secrets/wifi.yaml
# Use a single entry for now:
# ---
# - ssid: Ferney
#   password: "…"

# 2. Write SSID + password into device NVS (namespace "wifi")
./scripts/fw idf nvs-wifi
# same as: ./scripts/nvs-wifi-provision.sh

# 3. Flash connect app (no password in the .bin app image)
./scripts/fw idf upload wifi_connect
./scripts/fw idf monitor wifi_connect
```

On success the log should show **DHCP got ip** and **local time** from SNTP.

Optional: list SSIDs this PC already knows (no passwords):

```bash
./scripts/export-known-wifi.sh
```

Legacy helper (embed credentials in a generated header for experiments):

```bash
./scripts/gen-wifi-credentials.sh
```

Prefer **NVS provision** for `wifi_connect` so app rebuilds do not require baking PSKs into the firmware image.

### NVS layout (namespace `wifi`)

| Key | Type | Meaning |
|-----|------|---------|
| `ssid` | string | Network name |
| `password` | string | PSK (empty if open) |

Flash target: default single-app NVS partition at **0x9000**, size **0x6000** (override with `NVS_OFFSET` / `NVS_SIZE` if you change the partition table).

## What must stay out of git

| Path | Why |
|------|-----|
| `secrets/wifi.yaml` | Passwords |
| `secrets/generated/` | NVS CSV/bin and other host-side secret artifacts |
| `**/wifi_credentials.generated.h` | Embedded passphrases (legacy path) |
| `esp32_firmware/apps/*/build/` | Build products |
| `*.bin`, `*.elf` | May contain secrets if generated from provision tools |

## Security notes

- NVS on the chip still holds the PSK; physical flash dump can recover it unless NVS encryption is enabled later.
- `idf.py erase-flash` clears NVS (and app) — re-run `nvs-wifi` after a full erase.
- Do not log passwords at runtime.
- Prefer WPA2/WPA3 PSK home networks; enterprise EAP is out of scope for v1.

## CLI

| Command | Role |
|---------|------|
| `./scripts/export-known-wifi.sh` | Host known SSIDs → YAML (no passwords) |
| `./scripts/fw idf nvs-wifi` | First entry of `secrets/wifi.yaml` → device NVS |
| `./scripts/fw idf upload wifi_scan` | See airwaves (no secrets) |
| `./scripts/fw idf upload wifi_connect` | Connect via NVS + DHCP + SNTP |
