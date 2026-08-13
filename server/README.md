# PhototherapyServer

Rails 8 app on the LAN: device registry, household users, UV exposure log, and
UDP discovery for the ESP32 `session_timer`.

## Requirements

- Ruby (see `.ruby-version`; mise recommended)
- SQLite (dev/test)
- `bcrypt` (Rails 8 authentication)

```bash
cd server
bin/setup          # bundle + db:prepare
bin/rails db:seed  # Guest + users from db/data/users.yaml
bin/rails server -b 0.0.0.0 -p 3000
bin/rails test     # Minitest
```

Puma serves **HTTP on TCP 3000**. On boot it also starts **UDP discovery on
port 3000** (same number, different protocol). Time zone: **Mountain Time
(US & Canada)** (`config.time_zone`); pong includes `tz` / `tz_posix` for the module.

## Production host (`ami`)

LAN deploy target: **ami** at `192.168.1.202` (Arch + Docker + Kamal via
[arch-rails-server](https://github.com/Ruby-on-Rails-Wizardry/arch-rails-server)).

| | |
|--|--|
| UI | `http://phototherapy.ami.lan` |
| SSH | `rob@ami` (login), `deploy@ami` (Kamal) |
| Config | [`config/deploy.yml`](config/deploy.yml) |
| Guide | [`docs/deploy-ami.md`](docs/deploy-ami.md) |

```bash
# once DNS/hosts points phototherapy.ami.lan → 192.168.1.202
bin/kamal setup
bin/kamal deploy
```

## Authentication

Rails 8 `generate authentication`:

- `User` — `name`, `email_address`, `password_digest`
- `Session` — cookie `cookies.signed[:session_id]`
- Routes: `resource :session`, `resources :passwords`
- App requires login by default (`Authentication` concern)

Sign in: `/session/new` (then devices index).

### Seed users

| name | email | notes |
|------|--------|--------|
| **Guest** | `guest@ferney.org` | **id 0** — key **A** then **0**; last on list |
| rob … miriam | `<name>@ferney.org` | household; key **A** then **1–9** |

Source: [`db/data/users.yaml`](db/data/users.yaml) via **data_imp** (`UsersImporter`, upsert by email). Guest is always ensured in seeds. Re-seed updates names and does not reset passwords.

```bash
bin/rails db:seed
# optional: SEED_USER_PASSWORD=… bin/rails db:seed
```

Default seed password **`password`** (local/dev). Re-seed does not reset existing passwords.

### Seed skin types

Manual Table 1 (I–VI). Source: [`db/data/skin_types.yml`](db/data/skin_types.yml), loaded by **data_imp** (`SkinTypesImporter`, upsert by number). Re-seed updates descriptions and EGT durations (step / max / initial); it does not wipe or duplicate. Used for psoriasis EGT; not required for vitiligo or eczema.

### Seed therapy types

EGT skin-condition modes (psoriasis, vitiligo, atopic dermatitis) plus **Manual** (user-entered times, no skin type). Source: [`db/data/therapy_types.yml`](db/data/therapy_types.yml), loaded by **data_imp** (`TherapyTypesImporter`, upsert by slug). Re-seed updates names, descriptions, and durations; it does not wipe or duplicate. Psoriasis step / max / initial live on the skin type.

## Web routes (login required)

| Path | Purpose |
|------|---------|
| `/devices` | Discovered ESP32 boards |
| `/users` | Household + Guest |
| `/users/:id` | User detail |
| `/users/:user_id/exposures` | Exposure log (e.g. `/users/4/exposures`, `/users/0/exposures`) |
| `/users/:user_id/user_therapies` | Assign/remove therapy types (skin type on the join) |
| `/users/:user_id/exposures/new` | Manual exposure entry |

## Exposure model

| Column | Meaning |
|--------|---------|
| `user_id` | Household user or Guest (0) |
| `started_at` | When the light went on (end − duration from device log) |
| `duration_seconds` | How long the light stayed on |

## UDP protocol (summary)

Full wire format: [docs/device-discovery.md](../docs/device-discovery.md).

| type | Direction | Role |
|------|-----------|------|
| `ping` → `pong` | ESP ↔ server | Device upsert; wall clock **unix** + **tz** / **tz_posix** / **tz_offset**; ping **version** / **app**; optional **status**; pong **published_version** |
| `status` | ESP → server | UI snapshot (mode, LCD lines, LED, lamp/fan); stored on Device; no reply |
| `ota` | web → server → ESP | Immediate firmware check (`POST /devices/:id/ota_check` or key **B**) |
| `users` | ESP ↔ server | Key **A**: household ids 1–9, then **Guest id 0** |
| `therapy` | ESP ↔ server | Key **A** then digit: `recommended_seconds`, `step_seconds`, `max_seconds`, `initial_seconds`, `last_duration_seconds`; optional `message` |
| `therapies` | ESP ↔ server | Key **B**: keypad therapy list + skin types |
| `assign_therapy` | ESP ↔ server | Key **B** then digit(s): assign therapy / skin to the selected user |
| `exposure` | ESP → server | Lamp off: log `user_id`, `duration_seconds`, end `unix` |

### ENV

| Variable | Default | Meaning |
|----------|---------|---------|
| `UDP_DISCOVERY` | `1` | Set `0` to disable the listener |
| `UDP_DISCOVERY_PORT` | `3000` | UDP port |
| `UDP_DISCOVERY_IDENTITY` | hostname | Name in pong |
| `UDP_DISCOVERY_IP` | auto | Force IP in pong |
| `UDP_DISCOVERY_TZ_POSIX` | `MST7MDT,M3.2.0,M11.1.0` | POSIX TZ string for ESP |

### Firewall (Arch / UFW)

```bash
sudo ufw allow 3000/udp comment 'Phototherapy device discovery'
sudo ufw allow 3000/tcp comment 'Phototherapy Rails HTTP'
```

## Specs

| Layer | Location |
|-------|----------|
| Product contracts (Gherkin) | [`docs/features/`](../docs/features/) |
| Automated (Minitest) | `bin/rails test` |

Implementation: `app/services/udp_discovery_listener.rb` (started from `config/puma.rb`).
