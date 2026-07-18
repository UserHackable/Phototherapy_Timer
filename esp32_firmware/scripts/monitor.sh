#!/usr/bin/env bash
# Serial monitor for an IDF app directory (sdkconfig baud). Usage: APP=wifi_scan ./scripts/monitor.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$ROOT/.." && pwd)"
APP="${APP:-${1:-}}"
if [[ -z "$APP" ]]; then
  echo "usage: APP=<name> $0   or   $0 <name>" >&2
  exit 1
fi
APP_DIR="$ROOT/apps/$APP"
if [[ ! -d "$APP_DIR" ]]; then
  echo "error: unknown app '$APP'" >&2
  exit 1
fi

PORT="${PORT:-$("$REPO/scripts/detect-esp-port.sh")}"
echo "Using PORT=$PORT app=$APP"

if command -v mise >/dev/null 2>&1; then
  (cd "$ROOT" && mise install >/dev/null)
  eval "$(mise activate bash)"
fi

if ! command -v idf.py >/dev/null 2>&1; then
  # shellcheck disable=SC1091
  source "$ROOT/scripts/export-idf.sh"
fi

cd "$APP_DIR"
idf.py -p "$PORT" monitor
