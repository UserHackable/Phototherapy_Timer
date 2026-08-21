#!/usr/bin/env bash
# Build an ESP-IDF app, write OTA manifest + app.bin, install onto ami Rails volume.
#
# Usage (repo root):
#   ./scripts/fw idf ota-publish ota_smoke
#   ./scripts/fw idf ota-publish session_timer
#
# Env:
#   OTA_SSH    SSH host with docker access to the web container
#              (default: first that works among ami, gluttony, deploy@192.168.72.2)
#   OTA_FORCE  if 1, set "force": true in manifest (re-flash same version)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:-}"
[[ -n "$APP" ]] || { echo "usage: $0 <app>" >&2; exit 1; }

APP_DIR="$REPO/esp32_firmware/apps/$APP"
BIN="$APP_DIR/build/${APP}.bin"
[[ -f "$APP_DIR/CMakeLists.txt" ]] || { echo "error: unknown app $APP" >&2; exit 1; }

# Version string embedded in esp_app_desc (must match uh_ota running version).
app_bin_version() {
  python3 - "$1" <<'PY'
import sys
from pathlib import Path
d = Path(sys.argv[1]).read_bytes()
idx = d.find(b"\x32\x54\xCD\xAB")
if idx < 0:
    sys.exit(1)
ver = d[idx + 16 : idx + 48].split(b"\0", 1)[0].decode("ascii", "replace").strip()
if not ver:
    sys.exit(1)
print(ver)
PY
}

pick_ota_ssh() {
  if [[ -n "${OTA_SSH:-}" ]]; then
    echo "$OTA_SSH"
    return
  fi
  local host
  for host in ami gluttony deploy@192.168.72.2; do
    if ssh -o BatchMode=yes -o ConnectTimeout=4 "$host" "true" 2>/dev/null; then
      echo "$host"
      return
    fi
  done
  echo "ami"
}

echo "==> build $APP"
APP="$APP" "$REPO/esp32_firmware/scripts/build.sh" "$APP"
[[ -f "$BIN" ]] || { echo "error: missing $BIN" >&2; exit 1; }

VERSION="$(app_bin_version "$BIN" 2>/dev/null || true)"
if [[ -z "$VERSION" ]]; then
  VERSION="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
  echo "warning: could not read app_desc version from $BIN; using $VERSION" >&2
fi
SHA="$(sha256sum "$BIN" | awk '{print $1}')"
SIZE="$(wc -c <"$BIN" | tr -d ' ')"
FORCE="${OTA_FORCE:-0}"

STAGE="$REPO/tmp/ota-publish/$APP"
mkdir -p "$STAGE"
cp -f "$BIN" "$STAGE/app.bin"

FORCE_JSON="false"
[[ "$FORCE" == "1" || "$FORCE" == "true" ]] && FORCE_JSON="true"

cat >"$STAGE/manifest.json" <<EOF
{
  "v": 1,
  "app": "$APP",
  "version": "$VERSION",
  "sha256": "$SHA",
  "size": $SIZE,
  "url": "/firmware/$APP/app.bin",
  "force": $FORCE_JSON
}
EOF

echo "manifest:"
cat "$STAGE/manifest.json"
echo "bin: $STAGE/app.bin ($SIZE bytes)"

OTA_SSH="$(pick_ota_ssh)"
echo "==> publish host $OTA_SSH"
WEB="$(ssh -o BatchMode=yes -o ConnectTimeout=8 "$OTA_SSH" \
  "docker ps --filter label=service=phototherapy_server --filter label=role=web --format '{{.Names}}' | head -1")"
[[ -n "$WEB" ]] || { echo "error: no phototherapy web container on $OTA_SSH" >&2; exit 1; }

echo "==> copy into $WEB:/rails/storage/firmware/$APP/"
ssh -o BatchMode=yes "$OTA_SSH" "docker exec $WEB mkdir -p /rails/storage/firmware/$APP"
tar -C "$STAGE" -cf - app.bin manifest.json \
  | ssh -o BatchMode=yes "$OTA_SSH" \
    "docker exec -i $WEB tar -C /rails/storage/firmware/$APP -xf -"

echo "OK: published $APP version=$VERSION"
echo "    http://phototherapy.ami.lan/firmware/$APP/manifest.json"
echo "    http://phototherapy.lan/firmware/$APP/manifest.json"
