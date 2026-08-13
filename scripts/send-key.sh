#!/usr/bin/env bash
# Inject keypad keys or poll status on the product module over UDP (LAN test).
#
# Usage (repo root):
#   ./scripts/fw idf key A1B4
#   ./scripts/fw idf status          # start watching + snapshot
#   ./scripts/fw idf unwatch         # stop status echoes to this host
#   MODULE_IP=192.168.1.243 WATCH=20 ./scripts/fw idf key A
#
# This host is a watcher: the module echoes status here on state changes
# until unwatch. Injected keys set test=true (UV SSR stays off).
set -euo pipefail

WHAT="${1:-}"
[[ -n "$WHAT" ]] || { echo "usage: $0 <keys|status|unwatch>" >&2; exit 1; }

PORT="${MODULE_PORT:-3000}"
WATCH="${WATCH:-15}"

resolve_ip() {
  if [[ -n "${MODULE_IP:-}" ]]; then
    printf '%s\n' "$MODULE_IP"
    return
  fi
  local ip
  ip="$(ssh -o BatchMode=yes -o ConnectTimeout=4 ami \
      "docker exec phototherapy_server-udp_discovery bin/rails runner \
       'd = Device.order(updated_at: :desc).first; print d&.ip.to_s'" 2>/dev/null || true)"
  ip="${ip//[$'\t\r\n ']}"
  if [[ -n "$ip" ]]; then
    printf '%s\n' "$ip"
    return
  fi
  echo "error: set MODULE_IP (could not read Device.ip from ami)" >&2
  exit 1
}

IP="$(resolve_ip)"
[[ -n "$IP" ]] || { echo "error: empty module IP" >&2; exit 1; }

if [[ "$WHAT" == "status" || "$WHAT" == "check" || "$WHAT" == "watch" ]]; then
  echo "UDP watch → $IP:$PORT (listen ${WATCH}s)"
  KIND=status
  KEYS=""
elif [[ "$WHAT" == "unwatch" || "$WHAT" == "stop" ]]; then
  echo "UDP unwatch → $IP:$PORT"
  KIND=unwatch
  KEYS=""
  WATCH=3
else
  echo "UDP key → $IP:$PORT keys=$WHAT (watch ${WATCH}s)"
  KIND=key
  KEYS="$WHAT"
fi

python3 - "$IP" "$PORT" "$KIND" "$KEYS" "$WATCH" <<'PY'
import json, socket, sys, time

ip, port, kind, keys, watch = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], float(sys.argv[5])
if kind == "status":
    payload = json.dumps({"v": 1, "type": "watch"})
elif kind == "unwatch":
    payload = json.dumps({"v": 1, "type": "unwatch"})
else:
    payload = json.dumps({"v": 1, "type": "key", "keys": keys})

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)
sock.sendto(payload.encode(), (ip, port))
deadline = time.time() + max(watch, 1.0)
got = False
while time.time() < deadline:
    try:
        data, src = sock.recvfrom(4096)
    except (TimeoutError, socket.timeout):
        continue
    got = True
    print(src[0], data.decode())
if not got:
    sys.exit("error: no reply (module off, wrong IP, or firmware without type key/status)")
PY
