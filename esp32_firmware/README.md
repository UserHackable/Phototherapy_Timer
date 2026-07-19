# ESP32 product / test firmware (ESP-IDF)

Multi-**app** tree (same idea as Arduino sketches): each directory under `apps/` is a full ESP-IDF project.

```text
esp32_firmware/
  apps/
    blink/         # LED 250 ms half-period (2× Arduino blink)
    lcd_hello/     # LCD1602 bring-up
    net_clock/     # LCD + Wi‑Fi + DHCP + SNTP clock
    wifi_scan/     # scan APs, print table on UART (no secrets)
    wifi_connect/  # NVS Wi‑Fi + DHCP + SNTP (UART only)
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
| `i2c_scan` | Probe I²C on SDA=21 / SCL=22; print addresses that ACK |
| `lcd_hello` | “Hello, world!” on LCD1602 via PCF8574 backpack |
| `net_clock` | LCD progress UI + NVS Wi‑Fi + DHCP + SNTP date/time clock |
| `wifi_scan` | STA scan; print APs (no secrets) |
| `wifi_connect` | NVS Wi‑Fi + DHCP + SNTP on UART (credentials via `nvs-wifi`) |

Add an app: copy `apps/blink`, rename `project(…)` in `CMakeLists.txt`, replace `main/main.c`.

## Wi‑Fi secrets + connect (NVS)

Design notes: [docs/wifi-config.md](../docs/wifi-config.md).

```bash
cp secrets/wifi.yaml.example secrets/wifi.yaml   # once; one ssid + password
./scripts/fw idf nvs-wifi                        # write NVS namespace "wifi"
./scripts/fw idf upload wifi_connect             # DHCP + SNTP (UART)
./scripts/fw idf monitor wifi_connect
# or LCD progress + clock:
./scripts/fw idf upload net_clock
./scripts/fw idf monitor net_clock
```

## net_clock

LCD-first: Hello world → Wi‑Fi/DHCP steps on the 16×2 → SNTP → live date/time. Same NVS credentials as `wifi_connect`. Progress screens hold ~1 s; clock refreshes every second; SNTP every 6 h and on reconnect.

```bash
./scripts/fw idf nvs-wifi
./scripts/fw idf upload net_clock
./scripts/fw idf monitor net_clock
```

## wifi_scan

```bash
./scripts/fw idf upload wifi_scan
./scripts/fw idf monitor wifi_scan
```

Does **not** join any network or drive the SSR.
