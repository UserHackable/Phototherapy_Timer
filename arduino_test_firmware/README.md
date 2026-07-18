# Arduino test firmware (CLI bring-up)

Multi-sketch experiments with **arduino-cli** + **mise**.  
All compile/upload goes through the **repo-root** CLI — no per-sketch scripts.

Product firmware: [`../esp32_firmware/`](../esp32_firmware/). Tooling: [`../docs/toolchain.md`](../docs/toolchain.md).

## Commands (from repo root)

```bash
./scripts/fw arduino setup              # once: install ESP32 core
./scripts/fw arduino list               # sketches here
./scripts/fw arduino build  blink       # compile only
./scripts/fw arduino upload blink       # compile + flash (auto port)
./scripts/fw arduino upload led_off
./scripts/fw arduino upload hello_serial
./scripts/fw arduino monitor            # serial 115200
```

```bash
PORT=/dev/ttyUSB1 ./scripts/fw arduino upload blink
FQBN=esp32:esp32:esp32 ./scripts/fw arduino build blink
```

Port detection: [`../scripts/detect-esp-port.sh`](../scripts/detect-esp-port.sh).

## Prerequisites

- **mise**
- USB; user in `uucp` / `dialout`
- Network once for `setup`

## Sketches

| Sketch | Role |
|--------|------|
| `blink` | LED blink GPIO 2, 500 ms (default if name omitted) |
| `hello_serial` | Blink + USB serial ticks |
| `led_off` | Hold LED off |

SSR stays off. Add a sketch as `name/name.ino`.

## Verbose compile

```bash
cd arduino_test_firmware
mise exec -- arduino-cli compile -v -b esp32:esp32:esp32 blink
```
