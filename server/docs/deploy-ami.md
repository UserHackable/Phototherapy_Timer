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

ESP modules talk **UDP 3000** JSON to the Rails app (`UdpDiscoveryListener`). That port is
published on the web container (`config/deploy.yml`), not via kamal-proxy.

**Problem:** Docker’s default **userland-proxy** (`docker-proxy`) rewrites the client address to
the bridge gateway (e.g. `172.18.0.1`). The app then stores the wrong `devices.ip` and unicasts
pongs at the proxy instead of the module. Discovery can look “dead” on the ESP even while
pings appear in app logs.

**Fix (host-wide, once):** disable userland-proxy so published ports use iptables DNAT and keep
the real LAN source IP:

```bash
# as root@ami
mkdir -p /etc/docker
cat >/etc/docker/daemon.json <<'EOF'
{
  "userland-proxy": false
}
EOF
systemctl restart docker
```

Confirm:

```bash
docker info | grep -i userland
# Userland Proxy: false
```

Containers with `restart: unless-stopped` (kamal-proxy + app) come back after the restart.
Stale `*_replaced_*` / exited app containers can be removed if they fight for UDP 3000:

```bash
docker container prune -f   # only stopped/created leftovers
```

After a healthy deploy, a laptop ping should log the **LAN** client IP (e.g. `192.168.1.x`),
not `172.18.0.1`:

```bash
python3 - <<'PY'
import json, socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(2)
s.sendto(json.dumps({"v":1,"type":"ping","identity":"manual-test"}).encode(), ("192.168.1.202", 3000))
print(s.recvfrom(1024))
PY
# then: docker logs <web> 2>&1 | grep udp_discovery | tail
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
