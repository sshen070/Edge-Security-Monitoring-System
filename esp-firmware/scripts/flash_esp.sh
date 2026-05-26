#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESP32_PACKAGE_URL="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

BOARD="${BOARD:-c3-register-test}"
PORT="${PORT:-}"
FQBN="${FQBN:-}"
SKETCH="${SKETCH:-}"
MONITOR="${MONITOR:-0}"
INSTALL_ARDUINO_CLI="${INSTALL_ARDUINO_CLI:-1}"
INSTALL_ESP32_CORE="${INSTALL_ESP32_CORE:-1}"
BAUD="${BAUD:-115200}"

export PATH="${HOME}/.local/bin:${PATH}"

usage() {
  cat <<'EOF'
Flash ESP32 sketches from a Jetson or Linux machine using arduino-cli.

Usage:
  BOARD=c3-register-test ./esp-firmware/scripts/flash_esp.sh
  ./esp-firmware/scripts/flash_esp.sh c3-register-test
  BOARD=s3-camera PORT=/dev/ttyACM0 ./esp-firmware/scripts/flash_esp.sh
  SKETCH=/path/to/sketch_dir FQBN=esp32:esp32:XIAO_ESP32C3 ./esp-firmware/scripts/flash_esp.sh

Environment:
  BOARD                Named target. Default: c3-register-test
  PORT                 Serial port. Auto-detects /dev/ttyACM* or /dev/ttyUSB* when omitted.
  FQBN                 Arduino board ID override.
  SKETCH               Sketch directory override.
  MONITOR=1            Open Serial Monitor after upload.
  BAUD=115200          Serial Monitor baud rate.
  INSTALL_ARDUINO_CLI=0  Skip arduino-cli auto-install.
  INSTALL_ESP32_CORE=0   Skip esp32 board core install/update.

Known BOARD values:
  c3-register-test
  c3-sensor-node
  c3-output-node
  c3-dashboard-node
  c3-hotspot-test
  s3-camera
  s3-school-wifi-test
EOF
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

ensure_arduino_cli() {
  if command -v arduino-cli >/dev/null 2>&1; then
    return
  fi

  if [[ "$INSTALL_ARDUINO_CLI" != "1" ]]; then
    fail "arduino-cli is not installed. Install it or set INSTALL_ARDUINO_CLI=1."
  fi

  echo "Installing arduino-cli into ${HOME}/.local/bin..."
  mkdir -p "${HOME}/.local/bin"
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR="${HOME}/.local/bin" sh
  command -v arduino-cli >/dev/null 2>&1 || fail "arduino-cli install completed but command is still unavailable."
}

ensure_esp32_core() {
  [[ "$INSTALL_ESP32_CORE" == "1" ]] || return

  if ! arduino-cli config dump >/dev/null 2>&1; then
    arduino-cli config init
  fi

  if ! arduino-cli config dump | grep -Fq "$ESP32_PACKAGE_URL"; then
    arduino-cli config add board_manager.additional_urls "$ESP32_PACKAGE_URL"
  fi

  arduino-cli core update-index
  arduino-cli core install esp32:esp32
}

target_defaults() {
  case "$BOARD" in
    c3-register-test)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-c3-sensors/examples/jetson_register_test}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32C3}"
      ;;
    c3-sensor-node)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-c3-sensors/examples/jetson_sensor_node_demo}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32C3}"
      ;;
    c3-output-node)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-c3-sensors/examples/jetson_output_node_demo}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32C3}"
      ;;
    c3-dashboard-node)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-c3-sensors/examples/jetson_gateway_dashboard_demo}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32C3}"
      ;;
    c3-hotspot-test)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-c3-sensors/examples/hotspot_wifi_test}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32C3}"
      ;;
    s3-camera)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-s3-camera/firmware/camera_stream_portal}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32S3}"
      ;;
    s3-school-wifi-test)
      SKETCH="${SKETCH:-${ROOT_DIR}/esp32-s3-camera/examples/school_wifi_test}"
      FQBN="${FQBN:-esp32:esp32:XIAO_ESP32S3}"
      ;;
    -h|--help|help)
      usage
      exit 0
      ;;
    *)
      if [[ -z "$SKETCH" || -z "$FQBN" ]]; then
        usage
        fail "Unknown BOARD '${BOARD}'. Set SKETCH and FQBN for custom targets."
      fi
      ;;
  esac
}

detect_port() {
  if [[ -n "$PORT" ]]; then
    [[ -e "$PORT" ]] || fail "PORT does not exist: $PORT"
    return
  fi

  local detected=""
  detected="$(find /dev -maxdepth 1 \( -name 'ttyACM*' -o -name 'ttyUSB*' \) 2>/dev/null | sort | head -n 1 || true)"
  [[ -n "$detected" ]] || {
    arduino-cli board list || true
    fail "No /dev/ttyACM* or /dev/ttyUSB* port found. Connect the ESP over USB or set PORT=/dev/ttyACM0."
  }
  PORT="$detected"
}

warn_if_placeholder_secrets() {
  if grep -R "change-this-password\\|YOUR_.*PASSWORD\\|YOUR_WIFI" "$SKETCH" >/dev/null 2>&1; then
    echo "WARNING: Sketch appears to contain placeholder WiFi credentials."
    echo "         Update the sketch/secrets before flashing if this board needs real network access."
  fi
}

main() {
  if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
  fi

  if [[ $# -gt 0 ]]; then
    BOARD="$1"
  fi

  target_defaults
  [[ -d "$SKETCH" ]] || fail "Sketch directory not found: $SKETCH"

  ensure_arduino_cli
  ensure_esp32_core
  detect_port
  warn_if_placeholder_secrets

  echo "Board target: $BOARD"
  echo "Sketch:       $SKETCH"
  echo "FQBN:         $FQBN"
  echo "Port:         $PORT"

  arduino-cli compile --fqbn "$FQBN" "$SKETCH"
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"

  echo "Upload complete."
  if [[ "$MONITOR" == "1" ]]; then
    echo "Opening Serial Monitor at ${BAUD} baud. Press Ctrl+C to exit."
    arduino-cli monitor -p "$PORT" -c "baudrate=${BAUD}"
  fi
}

main "$@"
