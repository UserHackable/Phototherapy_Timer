# Other peripherals (SSR, piezo)

Lighter notes for non-UI parts. **System wiring map:** [wiring.md](wiring.md). Full bring-up detail lives with mains safety practice, not just this file.

## Solid-state relays (lamps + fan)

Product: [Amazon B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G) — BlueStars **SSR-25DA**, pack of 2.

Use **two** modules: one for **UV lamps / ballasts**, one for the **cooling fan**. Firmware turns the fan on with the lamps and leaves it on for **30 seconds** after lamp-off.

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
- Treat like an LED load: one GPIO per SSR, **active HIGH** typically turns load on (confirm polarity silk: **+** / **−** control terminals).
- **Default both GPIOs LOW / off** at boot, brown-out, and panic. Never leave lamps on across reset. Fan also fails off on reset (rundown only while the MCU is still running the session end path).

### Mains / thermal

- Outputs switch **mains AC**. High voltage hazard.
- **Heat sink** recommended for continuous high current; derate for enclosed housing.
- Size conductors and fusing for ballast inrush and continuous current; 25 A is an upper rating, not a target load.
- Keep **control wiring** isolated from **load wiring**; use the metal base / mounting carefully (often tied to heatsink / chassis considerations).

### GPIO (locked)

| Role | Pin | Intent |
|------|-----|--------|
| SSR **lamps** | **GPIO26** | UV ballasts; active high; fail-off |
| SSR **fan** | **GPIO27** | Cooling fan; active high; fail-off; **30 s** after lamp-off |

See [wiring.md](wiring.md) for the full harness diagram.

## Piezo buzzer

Stock unit used a piezo beeper. Project plan: same idea — **one GPIO**.

| Item | Detail |
|------|--------|
| Type | Passive buzzer (passive) or active beeper |
| Drive | GPIO → series resistor; optional transistor if current is high |
| Audio | Square wave / LEDC PWM for tone; active type: DC on/off |
| Use | Session start/end, key click, error chirp |

### GPIO (locked)

| Role | Pin |
|------|-----|
| Piezo | **GPIO25** (LEDC tone; brief beep at end of session) |

No specific Amazon ASIN locked yet for the buzzer; document part number when purchased.

## Pin budget (locked for this bench)

| Function | Interface | Pins |
|----------|-----------|------|
| LCD1602 | I²C (PCF8574) | SDA **21**, SCL **22** |
| Keypad | I²C (PCF8574) | shared SDA/SCL (**0x20**) |
| TM1637 7-seg | CLK + DIO | **18** / **23** |
| SSR **lamps** | GPIO | **26** |
| SSR **fan** | GPIO | **27** |
| Piezo | GPIO | **25** |
| Status / lamp mirror LED | GPIO | **2** (optional; ON with **lamps**, not fan) |

Canonical diagram: [wiring.md](wiring.md).
