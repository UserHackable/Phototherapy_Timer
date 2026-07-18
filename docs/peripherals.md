# Other peripherals (SSR, piezo)

Lighter notes for non-UI parts. Full bring-up detail lives with mains safety practice, not just this file.

## Solid-state relay (lamp / ballast control)

Product: [Amazon B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G) — BlueStars **SSR-25DA**, pack of 2.

| Item | Spec (listing) |
|------|----------------|
| Model | **SSR-25DA** (DC control → AC load) |
| Control input | **3–32 V DC**, ~3–35 mA |
| Load output | **24–380 V AC**, **25 A** rated |
| Isolation | Optocoupler; high insulation resistance (listing: 500 MΩ / 500 VDC) |
| Switching | Zero-cross style / semiconductor (no mechanical contact) |
| Off leakage | ≤ **2 mA** (listing) |
| Ambient | ~−30 … +70 °C (listing) |

### Control from ESP32

- Input is **DC 3–32 V** — ESP32 **3.3 V GPIO** is in range for many SSR-DA modules; verify LED/control threshold on the unit (some want ≥3 V cleanly; if marginal, drive via a small transistor from 5 V).
- Treat like an LED load: one GPIO, **active HIGH** typically turns load on (confirm polarity silk: **+** / **−** control terminals).
- **Default GPIO LOW / off** at boot, brown-out, and panic. Never leave lamps on across reset.

### Mains / thermal

- Output switches **mains AC** to fluorescent **ballasts**. High voltage hazard.
- **Heat sink** recommended for continuous 25 A class loads; derate for enclosed housing.
- Size conductors and fusing for ballast inrush and continuous current; 25 A is an upper rating, not a target load.
- Keep **control wiring** isolated from **load wiring**; use the metal base / mounting carefully (often tied to heatsink / chassis considerations).

### Suggested GPIO

| Role | Provisional pin |
|------|-----------------|
| SSR enable | **GPIO26** or **GPIO27** (see [esp32-board.md](esp32-board.md)) |

## Piezo buzzer

Stock unit used a piezo beeper. Project plan: same idea — **one GPIO**.

| Item | Detail |
|------|--------|
| Type | Passive buzzer (passive) or active beeper |
| Drive | GPIO → series resistor; optional transistor if current is high |
| Passive | Square wave / LEDC PWM for tone; active type: DC on/off |
| Use | Session start/end, key click, error chirp |

### Suggested GPIO

| Role | Provisional pin |
|------|-----------------|
| Buzzer | **GPIO25** or **GPIO4** |

No specific Amazon ASIN locked yet for the buzzer; document part number when purchased.

## Provisional pin budget (summary)

| Function | Interface | Pins |
|----------|-----------|------|
| LCD1602 | I²C (PCF8574AT) | SDA **21**, SCL **22** |
| Keypad | I²C (PCF8574) | shared SDA/SCL |
| TM1637 7-seg | CLK + DIO | two free GPIOs (e.g. 18 + 23) |
| SSR | GPIO | e.g. **26** |
| Piezo | GPIO | e.g. **25** |

Update this table when wiring is finalized.
