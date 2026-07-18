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
| Status | Early stage — scaffold only; fault localized to timer circuit |

Manufacturer timer replacement exists; this repo deliberately replaces the failed timer with a custom controller and extended features. Stock diagnosis and the full parts list live in [README.md](README.md).

## Hardware / firmware (agent-facing)

| Role | Part |
|------|------|
| MCU | ESP32 Type-C **38-pin narrow** + screw terminals ([B0C8DBN29X](https://www.amazon.com/dp/B0C8DBN29X)) — DevKitC pinout; docs: [docs/esp32-board.md](docs/esp32-board.md); default I²C **SDA=21 SCL=22** |
| Lamps | SSR ([B0CBS8817G](https://www.amazon.com/dp/B0CBS8817G)) — GPIO → AC ballasts; **mains hazard** |
| Input | 4×4 keypad + I²C ([B0G2KZW8KX](https://www.amazon.com/dp/B0G2KZW8KX)) |
| Text UI | I²C LCD1602 16×2, blue backlight — HD44780 + **PCF8574AT** (A-variant) backpack, 5 V; docs: [docs/lcd1602-i2c.md](docs/lcd1602-i2c.md); expect I²C **0x38–0x3F** (often **0x3F** with open A0–A2) ([B0FGD3V29S](https://www.amazon.com/dp/B0FGD3V29S)) |
| Clock / countdown | Prefer I²C 4-digit 7-seg clock module ([B0F8PWZK71](https://www.amazon.com/dp/B0F8PWZK71)); alt with DPs ([B07GTRQYMV](https://www.amazon.com/dp/B07GTRQYMV)) |
| Beep | Piezo on one GPIO |
| Logic power | **5 V USB wall charger** (not the stock timer’s internal AC→DC). Internal **2-prong receptacle** from a short cannibalized extension cord; charger plugs in there. Lamps remain mains via SSR. |

### Toolchain

- Prefer **CLI + vim**, not Arduino IDE GUI: cross-compile + USB flash/serial.
- Do not assume Arduino-only project layout until the user picks an SDK (ESP-IDF / PlatformIO / other).
- Keep flash/build steps shell-scriptable when scaffolding.

### Safety (hardware)

- SSR switches **mains AC** to ballasts. Document isolation, fail-off defaults (SSR off on reset/boot/crash), and never treat GPIO experiments as safe near live wiring.
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

## Style

- Prefer **minimal, focused diffs**. No drive-by refactors.
- Match existing project style once code lands; MCU is **ESP32**, SDK still TBD (CLI-first).
- Prefer complete sentences in commit messages: *what* and *why*.

