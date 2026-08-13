# Phototherapy_Timer — therapy-last-exposure + E760 manual (resume)

**Saved:** 2026-08-12
**Purpose:** Resume household phototherapy work (ESP last-session UX, E760M dosing, keypad assign).

## Bookmark

| Item | Value |
|------|--------|
| Branch | `master` |
| Dev host | **niko** `~/UserHackable/Phototherapy_Timer` |
| Product module | Updates itself over LAN OTA — do **not** USB-flash from pi |
| Publish | `./scripts/fw idf ota-publish session_timer` then `/devices` **Check for update** |
| USB serial | Recovery only (partition table / brick). Not the routine path. |
| Git mirror | `git@ami:UserHackable/Phototherapy_Timer.git` |
| Prod | ami Kamal `phototherapy_server` + `udp_discovery` |
| Resume in repo | `docs/RESUME-therapy-last-exposure.md` |
| Local notes | `~/.grok/notes/phototherapy-e760-therapy-handoff.md` |

**Push policy:** github/gitlab only when user says **send it**.

## What was done

### 1. Last-session after A + user digit

- Therapy UDP message: two LCD lines — Last session / duration + age ago
- Age skips zero units (10h 8m not 0d10h8m)
- Hold 5s (aborts if more keys are typed); then entry UI
- Recommended seconds: last duration if >= 44h since last; else 0
- Untagged last exposures still count after a protocol is assigned

### 2. Deploy / OTA

- Kamal deploy + reboot udp_discovery on ami
- **Routine firmware:** `./scripts/fw idf ota-publish session_timer`; module pulls over LAN
- Product ESP: `esp32-b4bfe9e70e64`
- Do **not** `./scripts/fw idf upload session_timer` on pi for day-to-day updates

### 3. E760 manufacturer manual

- PDF: docs/E760-E-Series-UVBNB-Users-Manual-Rev3.3A-May2026.pdf
- Extract: docs/e760-users-manual/

### 4. Gherkin — E760M single panel, 6 bulbs, no add-ons

Path: docs/e760-users-manual/features/

### 5. Keypad assign (2026-08-12)

- **A1B4** = user 1 (Rob) + Eczema
- Keys typed during UDP waits and the last-session hold are queued
- Prod: Rob has eczema; last lamp-on **2:00** still used after the mode change

## Related notes

- phototherapy-timer-ota-handoff.md

## Not done yet

- Align session_timer more tightly with stock timer + EGT Gherkin
- OTA-publish the A1B4 key-queue firmware (needs an IDF host, then Check for update)
- send it to github/gitlab

## Quick resume

```bash
cd ~/UserHackable/Phototherapy_Timer
./scripts/fw idf ota-publish session_timer
# then http://phototherapy.ami.lan/devices → Check for update
```
