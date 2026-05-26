#!/usr/bin/env bash
set -euo pipefail

AP_SSID="${AP_SSID:-ESP-NET}"
AP_PASSWORD="${AP_PASSWORD:-change-this-password}"
AP_IP="${AP_IP:-10.42.0.1}"
AP_CIDR="${AP_CIDR:-10.42.0.1/24}"
AP_IFACE="${AP_IFACE:-}"
INSTALL_REALTEK_DRIVER="${INSTALL_REALTEK_DRIVER:-1}"
REALTEK_DRIVER_SOURCE="${REALTEK_DRIVER_SOURCE:-aircrack}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

log() {
  printf '\n==> %s\n' "$*"
}

run() {
  printf '+ %s\n' "$*"
  "$@"
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Missing required command: %s\n' "$1" >&2
    exit 1
  fi
}

install_base_packages() {
  log "Installing base packages"
  run sudo apt update
  run sudo apt install -y dkms git build-essential nvidia-l4t-kernel-headers network-manager curl
}

install_realtek_driver() {
  if [[ "${INSTALL_REALTEK_DRIVER}" != "1" ]]; then
    log "Skipping Realtek driver install"
    return
  fi

  log "Installing Realtek USB WiFi driver (${REALTEK_DRIVER_SOURCE})"

  if [[ "${REALTEK_DRIVER_SOURCE}" == "apt" ]]; then
    run sudo apt install -y rtl8812au-dkms
    sudo modprobe 8812au || true
    return
  fi

  run sudo apt remove -y rtl8812au-dkms || true
  if [[ ! -d "${HOME}/rtl8812au/.git" ]]; then
    run git clone https://github.com/aircrack-ng/rtl8812au.git "${HOME}/rtl8812au"
  fi
  pushd "${HOME}/rtl8812au" >/dev/null
  run git pull --ff-only
  run sudo make dkms_install
  popd >/dev/null
  sudo modprobe 88XXau || true
}

detect_ap_iface() {
  if [[ -n "${AP_IFACE}" ]]; then
    printf '%s\n' "${AP_IFACE}"
    return
  fi

  nmcli -t -f DEVICE,TYPE,STATE dev status \
    | awk -F: '$2 == "wifi" && $1 !~ /^p2p-/ && $3 != "connected" {print $1; exit}'
}

configure_hotspot() {
  local iface="$1"
  if [[ -z "${iface}" ]]; then
    cat >&2 <<'EOF'
No disconnected USB WiFi interface was found for the ESP access point.

Check:
  lsusb
  iw dev
  nmcli dev status

If the interface exists, rerun with:
  AP_IFACE=<interface> ./jetson-device-gateway/scripts/setup_jetson_orin.sh
EOF
    exit 1
  fi

  log "Configuring ${AP_SSID} on ${iface}"
  if nmcli -t -f NAME con show | grep -qx "Hotspot"; then
    run sudo nmcli con modify Hotspot connection.interface-name "${iface}"
    run sudo nmcli con modify Hotspot 802-11-wireless.ssid "${AP_SSID}"
    run sudo nmcli con modify Hotspot 802-11-wireless-security.psk "${AP_PASSWORD}"
    run sudo nmcli con modify Hotspot ipv4.addresses "${AP_CIDR}" ipv4.method shared
  else
    run sudo nmcli dev wifi hotspot ifname "${iface}" ssid "${AP_SSID}" password "${AP_PASSWORD}"
    run sudo nmcli con modify Hotspot connection.interface-name "${iface}"
    run sudo nmcli con modify Hotspot ipv4.addresses "${AP_CIDR}" ipv4.method shared
  fi
  run sudo nmcli con up Hotspot
}

start_gateway() {
  log "Starting Jetson device gateway"
  pushd "${REPO_ROOT}" >/dev/null
  run docker compose -f docker-compose.jetson.yml up -d --build
  popd >/dev/null
}

print_status() {
  log "Status"
  nmcli dev status || true
  ip addr | grep -A3 -E "^[0-9]+: .*:|inet ${AP_IP}/" || true
  curl -fsS "http://${AP_IP}:8080/health" && printf '\n' || true

  cat <<EOF

ESP settings:
  WIFI_SSID: ${AP_SSID}
  WIFI_PASSWORD: ${AP_PASSWORD}
  Gateway API: http://${AP_IP}:8080

Useful checks:
  curl http://${AP_IP}:8080/api/devices
  curl http://${AP_IP}:8080/api/cameras
  curl http://${AP_IP}:8080/api/sensors
EOF
}

main() {
  require_command nmcli
  require_command docker
  require_command curl

  install_base_packages
  install_realtek_driver

  log "Current WiFi devices"
  iw dev || true
  nmcli dev status || true

  local iface
  iface="$(detect_ap_iface)"
  configure_hotspot "${iface}"
  start_gateway
  print_status
}

main "$@"
