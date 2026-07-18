#!/usr/bin/env bash
# Install Arduino CLI (via mise) and the Espressif ESP32 core.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v mise >/dev/null 2>&1; then
  echo "mise not found. Install mise first: https://mise.jdx.dev/" >&2
  exit 1
fi

echo "==> mise install (arduino-cli from mise.toml)"
mise install

CLI=(mise exec -- arduino-cli)

echo "==> arduino-cli version"
"${CLI[@]}" version

if [[ ! -f "${HOME}/.arduino15/arduino-cli.yaml" ]] && [[ ! -f "${HOME}/Library/Arduino15/arduino-cli.yaml" ]]; then
  echo "==> arduino-cli config init"
  "${CLI[@]}" config init || true
fi

echo "==> Ensure Espressif board package index"
# Additional URLs are merged; safe to re-run.
"${CLI[@]}" config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json 2>/dev/null \
  || "${CLI[@]}" config set board_manager.additional_urls \
    https://espressif.github.io/arduino-esp32/package_esp32_index.json

echo "==> Update core index"
"${CLI[@]}" core update-index

echo "==> Install esp32:esp32 core (downloads toolchain + esptool; may take a while)"
"${CLI[@]}" core install esp32:esp32

echo "==> Installed cores"
"${CLI[@]}" core list

echo "==> Connected boards (plug USB if empty)"
"${CLI[@]}" board list || true

cat <<'EOF'

Setup complete. From the repo root:

  ./scripts/fw arduino list
  ./scripts/fw arduino build  blink
  ./scripts/fw arduino upload blink
  ./scripts/fw arduino monitor

FQBN default: esp32:esp32:esp32
See arduino_test_firmware/README.md and docs/toolchain.md
EOF
