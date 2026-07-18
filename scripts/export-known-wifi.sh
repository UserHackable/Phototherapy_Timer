#!/usr/bin/env bash
# Export known Wi‑Fi SSIDs from this computer into a YAML list.
#
# Usage:
#   ./scripts/export-known-wifi.sh
#   ./scripts/export-known-wifi.sh path/to/out.yaml
#   OUT=wifi.yaml ./scripts/export-known-wifi.sh
#
# Default output: known_wifi.yaml in the repo root.
# SSIDs only — never passwords.
#
# Sources (merged, de-duplicated):
#   1. iwd:      iwctl known-networks list
#   2. NetworkManager: nmcli -t -f NAME,TYPE connection show
#   3. NetworkManager keyfiles (if readable)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-${OUT:-$REPO/known_wifi.yaml}}"

# Strip ANSI CSI sequences (iwctl often emits them).
strip_ansi() {
  sed -E 's/\x1B\[[0-9;]*[A-Za-z]//g'
}

collect_from_iwd() {
  command -v iwctl >/dev/null 2>&1 || return 0
  # Parse: "  SSID…  psk|open|…  …"
  iwctl known-networks list 2>/dev/null | strip_ansi | awk '
    BEGIN { IGNORECASE = 1 }
    {
      line = $0
      gsub(/\r/, "", line)
      # trim for classification
      t = line
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", t)
      if (t == "") next
      if (t ~ /^-+$/) next
      if (t ~ /Known Networks/) next
      if (t ~ /^Name[[:space:]]+Security/) next
      # SSID ends before 2+ spaces + security type
      if (match(line, /[[:space:]]{2,}(open|psk|wep|8021x|sae|none)\>/)) {
        ssid = substr(line, 1, RSTART - 1)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", ssid)
        if (ssid != "") print ssid
      }
    }
  '
}

collect_from_nmcli() {
  command -v nmcli >/dev/null 2>&1 || return 0
  local name typ
  while IFS=: read -r name typ; do
    [[ "$typ" == "802-11-wireless" || "$typ" == "wifi" ]] || continue
    [[ -n "$name" ]] && printf '%s\n' "$name"
  done < <(nmcli -t -f NAME,TYPE connection show 2>/dev/null || true)
}

collect_from_nm_keyfiles() {
  local dir f name
  for dir in /etc/NetworkManager/system-connections \
             "${HOME}/.local/share/networkmanagement"; do
    [[ -d "$dir" ]] || continue
    shopt -s nullglob
    for f in "$dir"/*; do
      [[ -r "$f" ]] || continue
      if grep -qE '^type=wifi$|^type=802-11-wireless$' "$f" 2>/dev/null; then
        name=$(grep -E '^ssid=' "$f" 2>/dev/null | head -1 | cut -d= -f2- || true)
        [[ -n "$name" ]] && printf '%s\n' "$name"
      fi
    done
    shopt -u nullglob
  done
}

mapfile -t RAW < <(
  {
    collect_from_iwd
    collect_from_nmcli
    collect_from_nm_keyfiles
  } | sed '/^$/d'
)

mapfile -t SSIDS < <(printf '%s\n' "${RAW[@]+"${RAW[@]}"}" | awk 'NF && !seen[$0]++')

if [[ ${#SSIDS[@]} -eq 0 ]]; then
  echo "warning: no known Wi‑Fi SSIDs found" >&2
fi

{
  echo "---"
  for ssid in "${SSIDS[@]+"${SSIDS[@]}"}"; do
    # Prefer plain scalars when safe; quote otherwise.
    if [[ "$ssid" =~ ^[A-Za-z0-9_./@+-]+$ ]]; then
      printf -- '- ssid: %s\n' "$ssid"
    else
      esc=${ssid//\\/\\\\}
      esc=${esc//\"/\\\"}
      printf -- '- ssid: "%s"\n' "$esc"
    fi
  done
} >"$OUT"

echo "Wrote ${#SSIDS[@]} SSID(s) -> $OUT"
cat "$OUT"
