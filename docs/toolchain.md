# Toolchain: Arduino vs ESP-IDF (and what is under the hood)

Preference for this repo: **Unix terminal + vim**, not the Arduino IDE GUI. Both practical paths below are supported as folders:

| Path | Folder | Role |
|------|--------|------|
| **Arduino CLI** (hand-holding optional) | [`arduino_test_firmware/`](../arduino_test_firmware/) | Quick bring-up, multi-sketch experiments |
| **ESP-IDF** (baseline product firmware) | [`esp32_firmware/`](../esp32_firmware/) | Structured C/C++ firmware, Wi‑Fi/logging later |

### Shared CLI (reuse for every program)

From the **repo root** — do not invent a new script per sketch:

```bash
./scripts/fw arduino list
./scripts/fw arduino build  <sketch>    # e.g. blink, led_off, hello_serial
./scripts/fw arduino upload <sketch>    # compile + flash (auto PORT)
./scripts/fw arduino monitor

./scripts/fw idf list
./scripts/fw idf build  <app>           # e.g. blink, wifi_scan under apps/
./scripts/fw idf upload <app>           # build + flash
./scripts/fw idf monitor <app>
./scripts/fw port
```

Arduino sketches: `arduino_test_firmware/<name>/`.  
IDF apps: `esp32_firmware/apps/<name>/`.  
Do **not** add per-program shell scripts — pass the name to `fw`.

Folder READMEs describe sketches and IDF layout. Tool versions for Arduino CLI / cmake / ninja can be pinned with **mise**.

---

## Mental model

> Arduino for ESP32 = **Arduino API + build recipes** wrapping **ESP-IDF’s compiler and esptool**.  
> Professional Linux path = call **those tools yourself**, preferably via **`idf.py`** (or PlatformIO if you want `platformio.ini` hygiene).

Arduino is **not** a special ESP compiler. For ESP32 it is mostly a friendly UI (or CLI) + build glue around Espressif’s real stack.

---

## What Arduino uses under the hood (ESP32)

When you install **“ESP32 by Espressif Systems”** (Boards Manager or `arduino-cli core install`), you get the **[arduino-esp32](https://github.com/espressif/arduino-esp32)** core. That core sits **on top of ESP-IDF** (Espressif IoT Development Framework).

Pipeline when you compile / upload:

```text
your .ino / .cpp
        │
        ▼
Arduino build (recipes from platform.txt)
        │
        ├─► C/C++ compile:  xtensa-esp32-elf-g++  (classic ESP32 / WROOM)
        │                   (other chips: other triples, e.g. RISC-V on C3/C6)
        │
        ├─► link + produce .elf / .bin  (Espressif scripts, partitions)
        │
        └─► flash:  esptool  (USB serial → ROM bootloader)
                    (+ bootloader, partition table, app image as needed)
```

### The two functions people think of as “Arduino magic”

| Need | Actual tool | Notes |
|------|-------------|--------|
| **Cross-compile** | **GCC toolchain** from Espressif (`xtensa-esp32-elf-*` for this board) | Same family ESP-IDF uses |
| **Upload** | **`esptool`** (Python CLI) | Talks to USB-UART (CH340 / CP210x / …) |

Arduino also provides:

- **`platform.txt` / `boards.txt`** — command templates (flags, flash size, upload speed)
- **Arduino API** (`setup` / `loop`, `Wire`, `WiFi`, …) over IDF drivers
- Library manager and sketch layout
- Auto-reset (DTR/RTS) so BOOT is often automatic

### How to see the real commands

**Arduino IDE:** File → Preferences → show verbose output for compilation + upload. Console prints full `g++` / `esptool` lines.

**On disk** (after Boards Manager / CLI core install), typical paths:

- `~/.arduino15/packages/esp32/tools/` — toolchains, esptool
- `~/.arduino15/packages/esp32/hardware/esp32/<version>/` — core, `platform.txt`

**Arduino CLI** (same recipes, no GUI) — used in `arduino_test_firmware/`:

```bash
arduino-cli board list
arduino-cli compile -b esp32:esp32:esp32 ...
arduino-cli upload -p /dev/ttyUSB0 -b esp32:esp32:esp32 ...
```

---

## Alternatives for Linux / professional-style workflows

Spectrum from Arduino-shaped → Espressif-native:

### 1. ESP-IDF (baseline for this project’s product firmware)

- Official C/C++ SDK: FreeRTOS, Wi‑Fi, BLE, drivers, Kconfig, components.
- CLI: **`idf.py`** (`build`, `flash`, `monitor`, `menuconfig`).
- Same toolchain + **esptool** family Arduino uses.
- Vim-friendly: edit anywhere; build in a shell; CMake under the hood.
- Setup: see [`esp32_firmware/README.md`](../esp32_firmware/README.md).

```bash
. $IDF_PATH/export.sh   # or the project's export helper
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2. PlatformIO CLI (optional middle ground)

- `pio run`, `pio run -t upload`, `pio device monitor`.
- `framework = arduino` or `framework = espidf`.
- Not scaffolded in-repo yet; fine to add later if wanted.

### 3. Arduino CLI only

- Keep Arduino libraries / sketch model; drop the IDE.
- Great for **bring-up** and comparing with Arduino examples.
- Weaker long-term structure than IDF for a full controller product.
- Setup: see [`arduino_test_firmware/README.md`](../arduino_test_firmware/README.md) + **mise**.

### 4. Bare metal DIY (usually not worth it)

Installing only `xtensa-esp32-elf-gcc` + `esptool` and inventing linker/boot glue reinvents ESP-IDF poorly.

### 5. Niche

| Tool | Role |
|------|------|
| ESPHome / Tasmota | Config-driven IoT, not this custom timer |
| MicroPython | Fast experiments; weaker for hard fail-safe lamp timing |
| Rust (`esp-rs`) | Valid if you want Rust |

---

## Comparison for this project

| Goal | Arduino IDE | Arduino CLI | PlatformIO | **ESP-IDF** |
|------|-------------|-------------|------------|-------------|
| Cross-compile | hidden | CLI | `pio run` | `idf.py build` |
| Upload | hidden | `upload` | `pio … upload` | `idf.py flash` |
| Hand-holding | high | medium | medium | low–medium |
| Vim + terminal | awkward | good | good | **excellent** |
| Wi‑Fi / logging later | Arduino APIs | same | either fw | **native** |
| Product structure | weak | weak | decent | **strong** |
| Arduino lib examples | easiest | easy | easy | port drivers |

**Under all of them:** Espressif GCC triple + **esptool** + USB-UART driver (`ch341` / `cp210x` / …).

---

## Recommendation (both paths kept)

1. **Bring-up / learn-by-looking:** `arduino_test_firmware/` via **mise + arduino-cli** — blink, serial, optional I²C scan; verbose compile shows real tools.
2. **Baseline product firmware:** `esp32_firmware/` via **ESP-IDF** — timer state machine, SSR fail-off, I²C UI, later Wi‑Fi logging.
3. Drivers for **TM1637** and **PCF8574** are small; easy in pure IDF once pins are fixed (see hardware docs).

---

## Serial port and udev (Linux)

- Port is often `/dev/ttyUSB0` (CH340) or `/dev/ttyACM0`.
- User must be in `uucp` / `dialout` (distro-dependent) or have udev rules for the USB-UART chip.
- Download mode: hold **BOOT**, tap **EN**, release **BOOT** if auto-reset fails.

Board details: [esp32-board.md](esp32-board.md).

---

## Related

- Session notes that produced this split: [conversation-with-grok.md](../conversation-with-grok.md)
- Hardware pin map: [esp32-board.md](esp32-board.md)
