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
| Default session entry | **30 seconds** (`00:30`); `*` restores this |
| Therapy recommendation | **30 seconds** until per-user schedules exist |
| User list | Household **1–9**, then **0:Guest** |
| Select user | **A** then digit **0–9** |
| LCD entry / run / clock top | Name left, duration right (`Guest` if none) |
| LCD clock bottom | Calendar date |
| TM1637 clock mode | Wall clock HH:MM |
| Lamp SSR | **GPIO26** (+ LED GPIO2) |
| Fan SSR | **GPIO27** — on with lamp, **30 s** after lamp off |
| Exposure log | UDP on lamp off → `/users/:id/exposures` |

Protocol detail: [device-discovery.md](../device-discovery.md).  
Wiring: [wiring.md](../wiring.md).  
Rails: [server/README.md](../../server/README.md).
