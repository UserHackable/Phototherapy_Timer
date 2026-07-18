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
| MCU | **ESP32** on a breakout board | Wi‑Fi + Bluetooth; USB for programming. [Amazon B0C8DBN29X](https://www.amazon.com/dp/B0C8DBN29X) |
| Lamp drive | Solid-state relay (SSR) | One GPIO like an LED; switches mains AC to the fluorescent **ballasts**. [Amazon B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G) |
| Input | 16-key **4×4 keypad** + I²C driver | Fast numeric entry plus spare keys for functions. [Amazon B0G2KZW8KX](https://www.amazon.com/dp/B0G2KZW8KX) |
| UI text | I²C **16×2** character LCD | Prompts and feedback. [Amazon B0FGD3V29S](https://www.amazon.com/dp/B0FGD3V29S) |
| Time / countdown | I²C **4-digit 7-segment** (clock-style) | Preferred: [B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71) (I²C, clock layout). Alternate: [B07GTRQYMV](https://www.amazon.com/dp/B07GTRQYMV) (decimal-point dots). Idle → wall clock; therapy → exposure / countdown. |
| Audio | Piezo buzzer | Stock-style beeper; single GPIO. |

**I²C bus (shared):** keypad driver, 16×2 LCD, and 7-segment module (addresses TBD once boards are on the bus).

**GPIO (simple):** SSR enable, piezo; plus any board-level enable/reset lines as needed.

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
