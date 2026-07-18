# ESP32 product / test firmware (ESP-IDF)

Multi-**app** tree (same idea as Arduino sketches): each directory under `apps/` is a full ESP-IDF project.

```text
esp32_firmware/
  apps/
    blink/         # LED 250 ms half-period (2× Arduino blink)
    wifi_scan/     # scan APs, print table on UART (no secrets)
    wifi_connect/  # join strongest known SSID (needs secrets/wifi.yaml)
  scripts/         # build/flash helpers used by ./scripts/fw
  mise.toml        # cmake + ninja
```

Arduino multi-sketch tree: [`../arduino_test_firmware/`](../arduino_test_firmware/).  
Tooling overview: [`../docs/toolchain.md`](../docs/toolchain.md).

## Commands (repo root)

```bash
./scripts/fw idf install              # once per machine
./scripts/fw idf list
./scripts/fw idf build  blink
./scripts/fw idf upload blink         # build + flash
./scripts/fw idf upload wifi_scan
./scripts/fw idf monitor wifi_scan    # serial log / AP table
```

```bash
PORT=/dev/ttyUSB1 ./scripts/fw idf upload wifi_scan
```

## Apps

| App | Role |
|-----|------|
| `blink` | GPIO 2 LED, 250 ms on/off |
| `wifi_scan` | STA scan; print APs (no secrets) |
| `wifi_connect` | Scan → strongest **known** SSID → connect (uses secrets) |

Add an app: copy `apps/blink`, rename `project(…)` in `CMakeLists.txt`, replace `main/main.c`.

## Wi‑Fi secrets + connect (NVS)

Design notes: [docs/wifi-config.md](../docs/wifi-config.md).

```bash
cp secrets/wifi.yaml.example secrets/wifi.yaml   # once; one ssid + password
./scripts/fw idf nvs-wifi                        # write NVS namespace "wifi"
./scripts/fw idf upload wifi_connect             # DHCP + SNTP
./scripts/fw idf monitor wifi_connect
```

## wifi_scan

```bash
./scripts/fw idf upload wifi_scan
./scripts/fw idf monitor wifi_scan
```

Does **not** join any network or drive the SSR.
