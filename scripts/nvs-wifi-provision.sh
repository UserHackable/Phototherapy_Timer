#!/usr/bin/env bash
# Write one Wi‑Fi network into the ESP32 NVS partition (namespace "wifi").
#
# Reads the *first* entry from secrets/wifi.yaml:
#   ---
#   - ssid: Ferney
#     password: "secret"
#
# Usage (repo root):
#   ./scripts/nvs-wifi-provision.sh
#   ./scripts/nvs-wifi-provision.sh /path/to/wifi.yaml
#
# Requires: ESP-IDF installed (./scripts/fw idf install), board on USB, secrets file.
# Artifacts under secrets/generated/ (gitignored).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
YAML="${1:-$REPO/secrets/wifi.yaml}"
GEN_DIR="$REPO/secrets/generated"
CSV="$GEN_DIR/wifi_nvs.csv"
BIN="$GEN_DIR/wifi_nvs.bin"
# Default single-app partition table: NVS at 0x9000, size 0x6000
NVS_OFFSET="${NVS_OFFSET:-0x9000}"
NVS_SIZE="${NVS_SIZE:-0x6000}"

IDF_PATH_DEFAULT="${PHOTOTHERAPY_IDF_PATH:-${HOME}/esp/esp-idf}"
export IDF_PATH="${IDF_PATH:-$IDF_PATH_DEFAULT}"

if [[ ! -f "$YAML" ]]; then
  echo "error: missing $YAML" >&2
  echo "  cp secrets/wifi.yaml.example secrets/wifi.yaml  # edit ssid/password" >&2
  exit 1
fi

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
  echo "error: ESP-IDF not found at IDF_PATH=$IDF_PATH" >&2
  echo "  ./scripts/fw idf install" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null

GEN_PY="$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
if [[ ! -f "$GEN_PY" ]]; then
  echo "error: nvs_partition_gen.py not found under IDF" >&2
  exit 1
fi

mkdir -p "$GEN_DIR"

# Parse first network from simple YAML (stdlib Python)
eval "$(python3 - "$YAML" <<'PY'
import re, sys, shlex
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8")
lines = [ln for ln in text.splitlines() if not re.match(r"^\s*#", ln)]
ssid = password = None
for ln in lines:
    m = re.match(r"^\s*-\s*ssid:\s*(.*)$", ln)
    if m:
        if ssid is not None:
            break  # first network only
        ssid = m.group(1).strip().strip("\"'")
        continue
    m = re.match(r"^\s*password:\s*(.*)$", ln)
    if m and ssid is not None and password is None:
        raw = m.group(1).strip()
        if (raw.startswith('"') and raw.endswith('"')) or (raw.startswith("'") and raw.endswith("'")):
            raw = raw[1:-1]
        password = raw
if not ssid:
    sys.stderr.write("error: no ssid in yaml\n")
    sys.exit(1)
if password is None:
    password = ""
print(f"SSID={shlex.quote(ssid)}")
print(f"PASSWORD={shlex.quote(password)}")
PY
)"

# NVS CSV: no spaces around commas (IDF requirement)
# Escape is limited — reject commas in ssid/password for safety
if [[ "$SSID" == *","* || "$PASSWORD" == *","* ]]; then
  echo "error: ssid/password must not contain commas (NVS CSV limitation)" >&2
  exit 1
fi

{
  echo "key,type,encoding,value"
  echo "wifi,namespace,,"
  echo "ssid,data,string,${SSID}"
  echo "password,data,string,${PASSWORD}"
} >"$CSV"

echo "Generating NVS image for SSID='${SSID}' (password not printed)"
python3 "$GEN_PY" generate "$CSV" "$BIN" "$NVS_SIZE"

PORT="${PORT:-$("$REPO/scripts/detect-esp-port.sh")}"
echo "Flashing NVS -> ${PORT} @ ${NVS_OFFSET} (${BIN})"

# Prefer esptool from IDF python env
python3 -m esptool --chip esp32 -p "$PORT" -b 460800 \
  write_flash "$NVS_OFFSET" "$BIN"

echo "OK: NVS namespace 'wifi' provisioned (ssid + password)."
echo "    Flash wifi_connect app if needed:  ./scripts/fw idf upload wifi_connect"
echo "    Monitor:                           ./scripts/fw idf monitor wifi_connect"
