#!/usr/bin/env bash
# Build an ESP-IDF app, write OTA manifest + app.bin, install onto ami Rails volume.
#
# Usage (repo root):
#   ./scripts/fw idf ota-publish ota_smoke
#   ./scripts/fw idf ota-publish session_timer
#
# Env:
#   OTA_HOST   SSH host for ami (default: ami / deploy@192.168.1.202)
#   OTA_FORCE  if 1, set "force": true in manifest
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:-}"
[[ -n "$APP" ]] || { echo "usage: $0 <app>" >&2; exit 1; }

APP_DIR="$REPO/esp32_firmware/apps/$APP"
BIN="$APP_DIR/build/${APP}.bin"
[[ -f "$APP_DIR/CMakeLists.txt" ]] || { echo "error: unknown app $APP" >&2; exit 1; }

echo "==> build $APP"
APP="$APP" "$REPO/esp32_firmware/scripts/build.sh" "$APP"
[[ -f "$BIN" ]] || { echo "error: missing $BIN" >&2; exit 1; }

VERSION="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)"
# Prefer IDF-stamped version from binary header if available later; git short is fine.
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

# Install into running web container storage volume on ami.
OTA_SSH="${OTA_SSH:-deploy@192.168.1.202}"
WEB="$(ssh -o BatchMode=yes -o ConnectTimeout=8 "$OTA_SSH" \
  "docker ps --filter label=service=phototherapy_server --filter label=role=web --format '{{.Names}}' | head -1")"
[[ -n "$WEB" ]] || { echo "error: no phototherapy web container on $OTA_SSH" >&2; exit 1; }

echo "==> copy into $WEB:/rails/storage/firmware/$APP/"
ssh -o BatchMode=yes "$OTA_SSH" "docker exec $WEB mkdir -p /rails/storage/firmware/$APP"
# docker cp via remote: stream tar
tar -C "$STAGE" -cf - app.bin manifest.json \
  | ssh -o BatchMode=yes "$OTA_SSH" \
    "docker exec -i $WEB tar -C /rails/storage/firmware/$APP -xf -"

echo "OK: published $APP version=$VERSION"
echo "    http://192.168.1.202/firmware/$APP/manifest.json"
echo "    http://phototherapy.ami.lan/firmware/$APP/manifest.json  (with Host/DNS)"
