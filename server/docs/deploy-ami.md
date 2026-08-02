# Deploy PhototherapyServer to `ami`

LAN host for Kamal/Docker Rails apps (this server and others).

| Item | Value |
|------|--------|
| Hostname | `ami` / `ami.lan` |
| IP | `192.168.1.202` |
| OS | Arch Linux (`arch-rails-server` bootstrap) |
| SSH users | `root` (bootstrap/admin), `rob` (login + docker), `deploy` (Kamal) |
| Docker | Engine + buildx + compose; `docker.service` enabled |
| Firewall | nftables table `arch_rails_server`: TCP 22/80/443, UDP **3000** (ESP discovery), Syncthing |
| App HTTP | `http://phototherapy.ami.lan` → kamal-proxy → app container |
| App UDP | host `3000/udp` published into the app container |
| Docker UDP | **`userland-proxy: false`** (iptables DNAT) so ESP source IPs are preserved |

Host kit (on the server): `/opt/arch-rails-server`  
Upstream: [Ruby-on-Rails-Wizardry/arch-rails-server](https://github.com/Ruby-on-Rails-Wizardry/arch-rails-server)

## UDP discovery / Docker

ESP modules talk **UDP 3000** JSON (`UdpDiscoveryListener`). This is **not** via kamal-proxy.

### Why a host-network accessory

Bridge-published UDP (`-p 3000:3000/udp`) has two failure modes on this host:

| Mode | Client IP | Broadcast from ESP |
|------|-----------|--------------------|
| userland-proxy (default) | rewritten to `172.18.0.1` | often works |
| iptables DNAT (`userland-proxy: false`) | real LAN IP | **not delivered** to the container |

Zero-config discovery needs **broadcast** (no hard-coded server IP in firmware). Real
`devices.ip` needs the **true peer** (or a client-reported `ip` field on the ping).

**Layout in `config/deploy.yml`:**

| Process | Role |
|---------|------|
| `web` | HTTP via kamal-proxy; `UDP_DISCOVERY=0` |
| accessory `udp_discovery` | **`network: host`**, binds UDP 3000, shared SQLite volume |

```bash
# after bin/kamal deploy
bin/kamal accessory boot udp_discovery     # first time
bin/kamal accessory reboot udp_discovery   # after image changes
bin/kamal accessory logs udp_discovery
```

Firmware still has **no hard-coded LAN IP**. Optional NVS hint `discovery/server_ip`
(from `secrets/wifi.yaml` `server_ip:`) speeds first unicast; broadcast finds the host
listener without that hint.

Self-check (unicast **and** broadcast should both pong):

```bash
python3 - <<'PY'
import json, socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.settimeout(2)
msg = json.dumps({"v":1,"type":"ping","identity":"manual-test"}).encode()
for dest in [("192.168.1.202", 3000), ("192.168.1.255", 3000)]:
    s.sendto(msg, dest)
    try:
        print(dest, s.recvfrom(1024))
    except Exception as e:
        print(dest, "FAIL", e)
PY
```

## Prerequisites (laptop)

- SSH key accepted for `deploy@ami` (and ideally `rob@ami` / `root@ami`)
- Optional `~/.ssh/config`:

  ```
  Host ami
    HostName 192.168.1.202
    User rob

  Host ami-deploy
    HostName 192.168.1.202
    User deploy
  ```

- Name resolution for the proxy host (pick one):

  ```bash
  # /etc/hosts on clients that should open the UI
  192.168.1.202 phototherapy.ami.lan ami.lan ami
  ```

- `config/master.key` present (read by `.kamal/secrets` as `RAILS_MASTER_KEY`)

## First-time setup + deploy

From `server/`:

```bash
bin/kamal setup    # once: kamal-proxy + host prep
bin/kamal deploy
```

Useful:

```bash
bin/kamal logs
bin/kamal console
bin/kamal app exec 'bin/rails db:seed'   # if you want seed users on the volume
```

## Multi-app notes

- Each Rails app needs a unique `service:` name and proxy `host:` (or hosts).
- Disk is small (~27 GiB root on this box) — prune unused images carefully; do not `docker system prune -a` while live volumes matter.
- UDP discovery is **app-specific** (publish + firewall). Other apps normally only need 80/443 via kamal-proxy.

## ESP32

Firmware should reach the server at **192.168.1.202** for discovery/SNTP as configured. UDP **3000** must stay open on the host and published into the container (`config/deploy.yml`).

## Host health

On `ami`:

```bash
ssh ami-deploy 'docker ps'
ssh root@ami '/opt/arch-rails-server/bin/doctor'
ssh root@ami '/opt/arch-rails-server/bin/verify'
```
