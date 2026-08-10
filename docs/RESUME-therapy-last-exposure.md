# Phototherapy_Timer — therapy-last-exposure + E760 manual (resume)

**Saved:** 2026-08-10
**Purpose:** Resume household phototherapy work (ESP last-session UX, E760M dosing contracts, manufacturer manual extract).

## Bookmark

| Item | Value |
|------|--------|
| Branch | `therapy-last-exposure` |
| Tip at save | run `git log -1` on ami remote (was f8523c7+ later commits) |
| Dev host | **niko** `~/UserHackable/Phototherapy_Timer` |
| IDF / OTA host | **pi** `~/User-Hackable/Phototherapy_Timer` |
| Git mirror | `git@ami:UserHackable/Phototherapy_Timer.git` |
| Prod | ami Kamal `phototherapy_server` + `udp_discovery` |
| Resume in repo | `docs/RESUME-therapy-last-exposure.md` |
| Local notes | `~/.grok/notes/phototherapy-e760-therapy-handoff.md` |

**Push policy:** github/gitlab only when user says **send it**.

## What was done

### 1. Last-session after A + user digit

- Therapy UDP message: two LCD lines — Last session / duration + age ago
- Age skips zero units (10h 8m not 0d10h8m)
- Hold 5s; then only top line becomes entry; bottom keeps last-exposure detail
- Recommended seconds: last duration if >= 44h since last; else 0
- Code: server/app/models/exposure.rb, udp_discovery_listener.rb, session_timer main.c

### 2. Deploy / OTA

- Kamal deploy + reboot udp_discovery on ami
- OTA from pi: scripts/fw idf ota-publish session_timer
- Product ESP often esp32-b4bfe9e70e64 ~192.168.1.243
- USB on pi: /dev/ttyUSB0, scripts/fw idf upload session_timer

### 3. E760 manufacturer manual

- PDF: docs/E760-E-Series-UVBNB-Users-Manual-Rev3.3A-May2026.pdf
- Extract: docs/e760-users-manual/ (chapters, full-text, pages/, images/)

### 4. Gherkin — E760M single panel, 6 bulbs, no add-ons

Path: docs/e760-users-manual/features/

- device_configuration.feature
- timer_stock_behavior.feature
- treatment_procedure.feature
- body_positions_single_panel.feature
- therapy_psoriasis.feature
- therapy_vitiligo.feature
- therapy_atopic_dermatitis.feature
- dose_adjustment_gaps.feature
- psoriasis_maintenance.feature
- README.md

## Related notes

- phototherapy-server-last-exposure.md
- phototherapy-timer-ota-handoff.md

## Not done yet

- Align session_timer more tightly with stock timer + EGT Gherkin
- Server skin-type/condition EGT recommendations
- Confirm module OTA version equals branch tip
- send it to github/gitlab

## Quick resume

```bash
cd ~/UserHackable/Phototherapy_Timer
git fetch ami && git checkout therapy-last-exposure && git pull --ff-only ami therapy-last-exposure
cat docs/RESUME-therapy-last-exposure.md
ls docs/e760-users-manual/features/
```

