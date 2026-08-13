# Product contracts (Gherkin)

Behavioral specs for the phototherapy timer and LAN server. Not automated via
Cucumber yet — they document intended UX and protocol so firmware and Rails
stay aligned.

| File | Scope |
|------|--------|
| [session_timer.feature](session_timer.feature) | ESP32 product UI: entry, run, clock, users, therapy, fan rundown, exposure send |
| [device_discovery.feature](device_discovery.feature) | UDP JSON: ping/pong/users/therapy/exposure + time zone |
| [exposures.feature](exposures.feature) | Rails nested exposure log + device auto-log |
| [authentication.feature](authentication.feature) | Sign-in, protect routes, password reset |
| [devices.feature](devices.feature) | Devices web registry |

### Current defaults (session_timer + server)

| Item | Value |
|------|--------|
| Default session entry | `initial_seconds` (EGT listed initial; Manual / none **30 s**); `*` restores this |
| Therapy recommendation | Last exposure after 44h, **0** if more recent, else `initial_seconds` |
| User list | Household **1–9**, then **0:Guest** |
| Select user | **A** then digit **0–9** |
| Select therapy | **B** then digit **1–4** (Manual / Psoriasis / Vitiligo / Eczema); psoriasis then skin **1–6** |
| Initial dose | `initial_seconds` from assignment (EGT listed initial; Manual / none **30 s**) |
| Step-up (C) | `recommended_seconds` + `step_seconds` (**10 s** if no therapy, **15 s** Manual, else EGT). Stays **0** if recommended is 0 |
| Max exposure | `max_seconds` from assignment (EGT listed max; Manual / none **20:00**) |
| Step-down (D) | `recommended_seconds` − `step_seconds` (floors at **0:00**). Stays **0** if recommended is 0 |
| LCD entry / run / clock top | Name left, duration right (`Guest` if none) |
| LCD clock bottom | Calendar date |
| TM1637 clock mode | Wall clock HH:MM |
| Lamp SSR | **GPIO26** (+ LED GPIO2) |
| Fan SSR | **GPIO27** — on with lamp, **30 s** after lamp off |
| Exposure log | UDP on lamp off → `/users/:id/exposures` |
| Status report | UDP `status` + ping: mode, LCD 2×16, LED, lamp/fan → `/devices` |
| OTA now | `/devices` **Check for update** (UDP `type:ota`) |

Protocol detail: [device-discovery.md](../device-discovery.md).  
Wiring: [wiring.md](../wiring.md).  
Rails: [server/README.md](../../server/README.md).
