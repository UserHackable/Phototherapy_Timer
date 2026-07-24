# AGENTS.md — Phototherapy_Timer

Project instructions for agents. Overrides global `~/.grok/AGENTS.md` on conflict.

Human overview: [README.md](README.md). Keep this file and the README aligned when product goals or git contracts change.

## Product

Custom **UserHackable** timer / controller for a failed stock timer on a SolRx E-Series medium-frame master phototherapy unit.

| Item | Detail |
|------|--------|
| Device family | SolRx E-Series Master (medium frame); model family includes **E760M-UVBNB** |
| Original product page | https://solarcsystems.com/product/medium-frame-solrx-e-series-master |
| Goal | First **duplicate stock timer behavior**, then add convenience (schedules, safety limits, status, session / health logging) |
| Use | Household UV phototherapy for eczema (several people) |
| Why this unit | Solarc [right to repair](https://solarcsystems.com/right-to-repair/); serviceable internals (mostly Wago connectors) |
| Status | Early product: `session_timer` UI + Rails discovery/exposures; fault was stock timer circuit |


Manufacturer timer replacement exists; this repo deliberately replaces the failed timer with a custom controller and extended features. Stock diagnosis and the full parts list live in [README.md](README.md).

## Hardware / firmware (agent-facing)

| Role | Part |
|------|------|
| MCU | ESP32 Type-C **38-pin narrow** + screw terminals ([B0C8DBN29X](https://www.amazon.com/dp/B0C8DBN29X)) — DevKitC pinout; docs: [docs/esp32-board.md](docs/esp32-board.md); default I²C **SDA=21 SCL=22** |
| Lamps + fan | SSR-25DA ×2 ([B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G)) — lamps **GPIO26**, fan **GPIO27** (on with lamps, **30 s** rundown); **mains hazard**; [docs/wiring.md](docs/wiring.md) |
| Input | 4×4 keypad + PCF8574 I²C ([B0G2KZW8KX](https://www.amazon.com/dp/B0G2KZW8KX)); [docs/keypad-i2c.md](docs/keypad-i2c.md); address ≠ LCD |
| Text UI | I²C LCD1602 — HD44780 + **PCF8574AT**; [docs/lcd1602-i2c.md](docs/lcd1602-i2c.md); often **0x3F** ([B0FGD3V29S](https://www.amazon.com/dp/B0FGD3V29S)) |
| Clock / countdown | Prefer **TM1637** 4-digit (**CLK+DIO**, not I²C) ([B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71)); [docs/seven-segment-display.md](docs/seven-segment-display.md); alt bare tube w/ DPs ([B07GTRQYMV](https://www.amazon.com/dp/B07GTRQYMV)) |
| Beep | Piezo on one GPIO; [docs/peripherals.md](docs/peripherals.md) |
| Front UI plate | New surface plate on unit face: LCD → TM1637 → keypad, centerline; [docs/front-panel.md](docs/front-panel.md) |
| Logic power | **5 V USB wall charger** (not the stock timer’s internal AC→DC). Internal **2-prong receptacle** from a short cannibalized extension cord; charger plugs in there. Lamps remain mains via SSR. |

### Toolchain

- Prefer **CLI + vim**, not Arduino IDE GUI. Full write-up: [docs/toolchain.md](docs/toolchain.md).
- **Two tracks (both intentional):**
  - [`arduino_test_firmware/`](arduino_test_firmware/) — multi-sketch (mise + arduino-cli).
  - [`esp32_firmware/apps/`](esp32_firmware/apps/) — multi-app **ESP-IDF** (prefer for product logic).
- **Single CLI:** [`scripts/fw`](scripts/fw) — `./scripts/fw arduino upload <sketch>`, `./scripts/fw idf upload <app>`. Do **not** add per-program scripts.
- Do not commit ESP-IDF clones or `~/.arduino15` cores.
- Session decision log: [conversation-with-grok.md](conversation-with-grok.md).

### Safety (hardware)

- SSRs switch **mains AC** (lamps and fan separately). Document isolation, fail-off defaults (both SSRs off on reset/boot/crash), and never treat GPIO experiments as safe near live wiring.
- Internal AC receptacle + USB charger: keep **mains wiring** and **5 V logic** clearly separated; do not assume the stock timer’s AC→DC is still present or reusable once that module is removed.
- Prefer default-off for lamp drive and clear timeout / max-session bounds before “smart” features.


## Git

| Setting | Value |
|---------|-------|
| Default branch | `master` |
| Canonical remote | `github` → `git@github.com:UserHackable/Phototherapy_Timer.git` |
| Mirror remote | `gitlab` → `git@gitlab.com:user-hackable/Phototherapy_Timer.git` |
| `origin` | Often also GitHub (from clone); prefer named remotes for push |

Dual-push after meaningful shared work:

```bash
git push github && git push gitlab
```

### Phrase shortcuts

| Phrase | Means |
|--------|--------|
| **send it** | Commit if needed (only when asked to commit), then **dual-push** `github` and `gitlab` (`git push github && git push gitlab`). Push tags only if the user also asked for a release/tag. |
| **ship it** / **cut a release** | Same as **send it** until a [docs/RELEASE.md](docs/RELEASE.md) exists; then follow that checklist end-to-end. |

**Push policy:** Do **not** push unless the user says **send it** (or **ship it** / **cut a release**). Commits, local edits, and tests are fine without that phrase; remotes stay local-only until they explicitly request a push.

Never force-push `master` unless the user explicitly asks.

## Safety / domain notes

- This controls **UV phototherapy** hardware. Prefer fail-safe defaults (timers that stop, hard max exposure limits, clear off/error states) over silent recovery.
- Do not invent medical dosing guidance; treat session times and limits as user-configured parameters with safe bounds, not clinical prescriptions.
- Never commit secrets, credentials, or private patient/session data.
- Wi‑Fi: real passwords only in `secrets/wifi.yaml` (gitignored). Provision device with `./scripts/fw idf nvs-wifi` (NVS namespace `wifi`). `wifi_connect` reads NVS, not committed secrets. Do not commit `secrets/generated/`, `*.generated.h`, `apps/*/build/`, or secret-bearing `*.bin`. See [docs/wifi-config.md](docs/wifi-config.md).

## Style

- Prefer **minimal, focused diffs**. No drive-by refactors.
- Match existing project style once code lands; MCU is **ESP32**, SDK still TBD (CLI-first).
- Prefer complete sentences in commit messages: *what* and *why*.

