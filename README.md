# Phototherapy_Timer

Smart replacement timer / controller for a **SolRx E-Series** medium-frame master phototherapy unit (model family includes **E760M-UVBNB**).

Used for **light therapy for eczema** by several people in the household. One reason this model stood out is Solarc’s [right to repair](https://solarcsystems.com/right-to-repair/) stance — serviceable hardware, not a sealed black box.

The stock timer circuit failed (lamps no longer light on schedule). A factory replacement from [Solarc Systems](https://solarcsystems.com/) is available; this project instead treats the failure as a chance to restore the unit **and** add convenience features. First priority: **duplicate existing timer behavior**, then layer on smarter control.

Planned beyond stock:

- Reliable timed treatment sessions (parity with the original)
- Smarter control (schedules, safety limits, clearer status)
- Reporting / logging for sessions and device health

## Hardware context

- Device: [SolRx E-Series Master (medium frame)](https://solarcsystems.com/product/medium-frame-solrx-e-series-master/)
- Manufacturer: [Solarc Systems](https://solarcsystems.com/) (Canadian)
- Right to repair: [solarcsystems.com/right-to-repair](https://solarcsystems.com/right-to-repair/)

### Serviceability

Most connections between major internal components use **Wago** connectors, which makes diagnosis and rework straightforward. The one mild complaint: bolts use **Robertson** (square-drive) heads — unsurprising for a Canadian manufacturer, but worth having the right driver on hand.

### Diagnosis (failed stock timer)

While debugging the no-light condition:

1. **Fuse** — ruled out (not the failure).
2. **Lock on/off switch** — ruled out (not the failure).
3. **Timer circuit** — when isolated, it still did not light the lamps even with power applied in isolation. Fault localized to the timer path, not the lamp supply or those other safety/power parts.

## Controller hardware

The stock device is basic; the replacement needs **Wi‑Fi** (and preferably **Bluetooth**) for later smart therapy management and logging. Parts are chosen for breadboard/breakout convenience while wiring, and for simple digital or I²C control from the MCU.

| Role | Choice | Notes / link |
|------|--------|----------------|
| MCU | **ESP32** Type-C **38-pin narrow** + screw terminal breakout | Wi‑Fi + BT; DevKitC-style pinout. Details: [docs/esp32-board.md](docs/esp32-board.md). [Amazon B0C8DBN29X](https://www.amazon.com/dp/B0C8DBN29X) |
| Lamp drive | **SSR-25DA** solid-state relay | GPIO control → mains AC to **ballasts**. Docs: [docs/peripherals.md](docs/peripherals.md). [Amazon B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G) |
| Input | 16-key **4×4** membrane + **I²C** (PCF8574) adapter | Numeric + function keys. Docs: [docs/keypad-i2c.md](docs/keypad-i2c.md). [Amazon B0G2KZW8KX](https://www.amazon.com/dp/B0G2KZW8KX) |
| UI text | I²C **LCD1602** (16×2), blue backlight | **HD44780** + backpack **PCF8574AT** (A-variant); **5 V DC**. See [docs/lcd1602-i2c.md](docs/lcd1602-i2c.md). [Amazon B0FGD3V29S](https://www.amazon.com/dp/B0FGD3V29S) |
| Time / countdown | **TM1637** 4-digit clock module (preferred) | **Not I²C** — **CLK + DIO** only; colon layout; DPs not usable. Docs: [docs/seven-segment-display.md](docs/seven-segment-display.md). [Amazon B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71). Alternate bare tube with DPs: [B07GTRQYMV](https://www.amazon.com/dp/B07GTRQYMV). Idle → clock; therapy → countdown. |
| Audio | Piezo buzzer | Stock-style beeper; single GPIO. [docs/peripherals.md](docs/peripherals.md) |

**I²C bus (shared):** LCD backpack + keypad adapter only (scan addresses; avoid collision — LCD often **0x3F**).

**GPIO (simple):** SSR enable, piezo, **TM1637 CLK/DIO** (two pins); plus enable/reset as needed.

### ESP32 board (MCU)

Full pinout, power, programming notes, and product photos: **[docs/esp32-board.md](docs/esp32-board.md)**.

| Item | Detail |
|------|--------|
| Listing | DORHEA 3-pack: Type-C **38-pin narrow** ESP32 + **GPIO 1→2** screw terminal adapters |
| Module | ESP-WROOM-32 class; shield text **ESP-32**, FCC **2AB7T-ESP32-32X** |
| CPU / radio | Dual-core LX6 ≤240 MHz; Wi‑Fi + Bluetooth Classic/BLE |
| USB | Type-C; on-board USB-UART bridge (CH340 / CP210x — check IC on board) |
| Pin map | Matches Espressif **ESP32-DevKitC** 38-pin headers (verified vs adapter silk) |
| Default I²C | **SDA GPIO21**, **SCL GPIO22** |
| Avoid | GPIO 6–11 (flash); treat 0/2/12/15 as strapping; 34–39 input-only |
| Manuals | [DevKitC user guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html) · [WROOM-32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf) |

Breakout fits **this** 38-pin narrow form factor only — not classic DevKit V1.

### LCD1602 text display (I²C backpack)

Photo and full I²C notes: **[docs/lcd1602-i2c.md](docs/lcd1602-i2c.md)** · image: [docs/images/lcd1602-i2c-backpack.jpg](docs/images/lcd1602-i2c-backpack.jpg)

From the module photo and NXP datasheet:

| Detail | Value |
|--------|--------|
| Chip | **PCF8574AT** (Philips / NXP marking; topside includes `L21491` / `05` / `knM02143`) |
| Module | HW-061-style backpack; header **GND · VCC · SDA · SCL** |
| Address pads | **A0 · A1 · A2** open as shipped (solder to change address) |
| I²C class | Standard-mode, **100 kHz** max (datasheet); 8-bit port expander |
| Supply | Chip 2.5–6 V; module run at **5 V** |
| **A-variant address range** | 7-bit **0x38–0x3F** (all address bits HIGH → **0x3F**) |
| Non‑A range (for contrast) | PCF8574 / PCF8574T → **0x20–0x27** (all HIGH → **0x27**) — *not* this chip |

**Manual:** use the NXP product data sheet — [PCF8574; PCF8574A (PDF)](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf). Confirm the live 7-bit address with an ESP32 I²C scan (open pads often appear as **0x3F** on these clones, but board pull-up/down can differ).

### Keypad (I²C 4×4)

Full notes: **[docs/keypad-i2c.md](docs/keypad-i2c.md)**.

| Item | Detail |
|------|--------|
| Kit | LONELY BINARY soft 4×4 membranes + **PCF8574** I²C adapters |
| Host | **SDA/SCL** only (saves 6 GPIOs vs raw matrix) |
| Voltage | **3.3–5 V** |
| Address | Set A0–A2 so it **≠** LCD (often LCD at **0x3F**); confirm with bus scan |

### 4-digit clock / countdown display

Full notes: **[docs/seven-segment-display.md](docs/seven-segment-display.md)**.

| Preferred ([B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71)) | Alternate ([B07GTRQYMV](https://www.amazon.com/dp/B07GTRQYMV)) |
|----------------------------------------------------------------|----------------------------------------------------------------|
| **TM1637** module, clock colon, 0.56″, 5 colors in pack | Bare **5641BH** common-anode tube, red, **decimal points** |
| **CLK + DIO** (not on I²C bus) | Needs external driver / multiplexing |
| Listing: per-digit decimals **not** usable | More pins / driver complexity |

### SSR + piezo

See **[docs/peripherals.md](docs/peripherals.md)**. SSR-25DA: control **3–32 V DC**, load **24–380 V AC / 25 A** class; heat-sink for real ballast current; GPIO default **off**.

### Low-voltage power (5 V)

The stock timer module did more than time sessions: it also converted **AC → DC** for its own electronics. The custom controller does **not** reuse that internal supply.

Instead:

1. **Mains inside the housing** — a short **two-prong extension cord** is cannibalized so its receptacle sits inside the unit (wired to the device’s switched/available AC as appropriate).
2. **USB wall charger** — a common **5 V USB charger** plugs into that internal receptacle and feeds the ESP32, displays, keypad module, and other low-voltage logic.
3. **Lamp path stays separate** — ballasts still see **mains AC** switched by the SSR; 5 V logic only *controls* the SSR, it does not power the tubes.

This keeps low-voltage bring-up simple (any decent USB charger) and isolates “ubiquitous 5 V” from redesigning a custom AC–DC stage for the controller.

### Firmware toolchain preference

Arduino IDE *could* target this board, but the preferred workflow is a **Unix terminal + vim** stack:

- Cross-compiler for ESP32 firmware
- Flash / serial tooling over the board’s **USB** connector (e.g. `esptool` and a serial monitor)

Exact SDK (ESP-IDF, PlatformIO CLI, etc.) is TBD; keep builds and flash steps runnable from the shell.

## Status

Early stage — project scaffold only. Next work: capture and reimplement stock timer behavior; stand up ESP32 toolchain and bring-up of I/O.

## Remotes

- GitHub (canonical): `git@github.com:UserHackable/Phototherapy_Timer.git`
- GitLab (mirror): `git@gitlab.com:user-hackable/Phototherapy_Timer.git`

```bash
git push github && git push gitlab
```

## License

TBD
