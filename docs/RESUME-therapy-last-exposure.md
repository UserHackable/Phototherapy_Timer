# Phototherapy_Timer — therapy-last-exposure + E760 manual (resume)

**Saved:** 2026-08-10  
**Purpose:** Resume household phototherapy work (ESP module last-session UX, E760M dosing contracts).

## Where the code lives

| Host | Path | Branch |
|------|------|--------|
| **niko** (dev) | `~/UserHackable/Phototherapy_Timer` | `therapy-last-exposure` |
| **pi** (IDF / USB / OTA build) | `~/User-Hackable/Phototherapy_Timer` | pull `ami/therapy-last-exposure` |
| **ami** (git mirror) | `git@ami:UserHackable/Phototherapy_Timer.git` | `therapy-last-exposure` |
| **ami** (prod app) | Kamal `phototherapy_server` + accessory `udp_discovery` | image from branch tip at last deploy |

**Do not force-push master.** Push policy: user must say **send it** for github/gitlab.

### Recent commits on `therapy-last-exposure` (high-water)

Look up with: `git log --oneline ami/therapy-last-exposure | head`

Key themes already on the branch:

1. **Last-session after A+digit (server + firmware)**  
   - Therapy UDP `message`: two lines — `Last session` / `{duration} {age} ago`  
   - Age skips zero units (`10h 8m` not `0d10h8m`)  
   - Hold **5 s**, then **top line only** refreshes to entry UI; **bottom keeps** last-exposure detail  
   - **Recommended seconds:** last exposure duration if **≥ 44 h** since last session; else **0** (accept 0 over UDP)  
   - Firmware: `THERAPY_MSG_HOLD_MS 5000`, sticky bottom, `recommended_seconds >= 0`

2. **Deploy / OTA (last known good from this track)**  
   - Server: Kamal deploy to ami + reboot `udp_discovery` host-network accessory  
   - Firmware OTA: from **pi** — `. ~/esp/esp-idf/export.sh && ./scripts/fw idf ota-publish session_timer`  
   - Module product identity seen: `esp32-b4bfe9e70e64` (~`192.168.1.243`)  
   - USB fallback on pi: `/dev/ttyUSB0`, `./scripts/fw idf upload session_timer`

3. **SolRx E760 manufacturer manual**  
   - PDF: `docs/E760-E-Series-UVBNB-Users-Manual-Rev3.3A-May2026.pdf`  
   - Extract: `docs/e760-users-manual/`  
     - chapters `00`–`20`, `full-text.md`, `pages/page-*.md`, `images/fig-*`  
   - **Gherkin (E760M single panel, 6 bulbs, no add-ons):**  
     `docs/e760-users-manual/features/`  
     - `device_configuration.feature`  
     - `timer_stock_behavior.feature`  
     - `treatment_procedure.feature`  
     - `body_positions_single_panel.feature`  
     - `therapy_psoriasis.feature` (skin types I–VI, single-panel EGT)  
     - `therapy_vitiligo.feature`  
     - `therapy_atopic_dermatitis.feature`  
     - `dose_adjustment_gaps.feature`  
     - `psoriasis_maintenance.feature`  
     - `README.md` (index)

## Household device assumption (for EGT / Gherkin)

- **E760M-UVBNB** MASTER only  
- **6 bulbs**, **no ADD-ON**, not MD66/MD666+  
- Use EGT row: **One (1) Single E760 MASTER Device Only (6 Bulbs Total)**  
- Nominal irradiance Table 2: **~6 mW/cm²** (1 device)

## Related notes

- `phototherapy-server-last-exposure.md` — earlier server orientation  
- `phototherapy-timer-ota-handoff.md` — OTA / UDP accessory layout  

## Suggested next steps (not done)

- Align custom `session_timer` UX more tightly with stock timer + EGT Gherkin (if desired)  
- Wire server recommendation / UI to skin-type + condition EGTs (manual contracts only today)  
- **send it** to github/gitlab when ready  
- Confirm module OTA version matches branch tip after next idle poll  
- Optional: drop accidental history of `server/vendor/bundle` if ever pushed to public remotes (was untracked again with gitignore)

## Quick resume commands

```bash
# niko
cd ~/UserHackable/Phototherapy_Timer
git fetch ami && git checkout therapy-last-exposure && git pull --ff-only ami therapy-last-exposure

# verify features
ls docs/e760-users-manual/features/

# network therapy smoke (Rob id=1)
python3 - <<'PY'
import json,socket
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(3)
s.sendto(json.dumps({"v":1,"type":"therapy","identity":"resume","user_id":1}).encode(),("192.168.1.202",3000))
print(s.recvfrom(2048)[0].decode())
PY

# pi OTA
cd ~/User-Hackable/Phototherapy_Timer
git fetch ami && git checkout therapy-last-exposure && git pull --ff-only ami therapy-last-exposure
. ~/esp/esp-idf/export.sh
./scripts/fw idf ota-publish session_timer
```
