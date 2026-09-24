#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then exec sudo --preserve-env=PATH bash "$0" "$@"; fi

repository_url="${REMOTELINK_REPOSITORY_URL:-https://github.com/flydmonkey/RemoteLink.git}"
freerdp_version="${REMOTELINK_FREERDP_VERSION:-3.32.0}"
script_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." 2>/dev/null && pwd || true)"
temporary_checkout=""
freerdp_source=""
cleanup() {
  case "${temporary_checkout}" in
    /tmp/remotelink-install.*) rm -rf -- "${temporary_checkout}" ;;
  esac
  case "${freerdp_source}" in
    /tmp/remotelink-freerdp.*) rm -rf -- "${freerdp_source}" ;;
  esac
}
trap cleanup EXIT
service_name="remote-gateway"
install_root="/opt/remote-gateway"
config_root="/etc/remote-gateway"
state_root="/var/lib/remote-gateway"
credential_root="/etc/credstore.encrypted"

if [[ ! -f /etc/os-release ]]; then echo "Unsupported Linux distribution" >&2; exit 1; fi
. /etc/os-release
if [[ "${ID_LIKE:-} ${ID:-}" != *debian* && "${ID:-}" != "ubuntu" ]]; then
  echo "This installer currently supports Ubuntu and Debian." >&2; exit 1
fi

if [[ ! -r /dev/tty ]]; then
  echo "An interactive terminal is required for installation." >&2; exit 1
fi
exec 3</dev/tty

if [[ ! -f "${script_root}/CMakeLists.txt" ]]; then
  if ! command -v git >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y ca-certificates git
  fi
  temporary_checkout="$(mktemp -d /tmp/remotelink-install.XXXXXXXX)"
  echo "Downloading RemoteLink from ${repository_url} ..."
  git clone --depth 1 --recurse-submodules "${repository_url}" "${temporary_checkout}/RemoteLink"
  project_root="${temporary_checkout}/RemoteLink"
else
  project_root="${script_root}"
fi

echo "RemoteLink one-click installer"
echo "Project: ${project_root}"
if [[ -e "${config_root}/targets.json" ]]; then
  read -r -u 3 -p "An existing installation was found. Update binaries and keep its configuration? [Y/n]: " keep_existing
  case "${keep_existing:-Y}" in [Nn]*) echo "Installation cancelled"; exit 0;; esac
  preserve_config=1
else
  preserve_config=0
  read -r -u 3 -p "Windows RDP host or IP: " target_host
  [[ -n "${target_host}" ]] || { echo "RDP host is required" >&2; exit 1; }
  read -r -u 3 -p "Windows username [administrator]: " target_user
  target_user="${target_user:-administrator}"
  read -r -u 3 -p "Windows domain (optional): " target_domain
  read -r -s -u 3 -p "Windows password: " target_password; echo
  [[ -n "${target_password}" ]] || { echo "Windows password is required" >&2; exit 1; }
  read -r -u 3 -p "Allowed host/CIDR list [* = allow all]: " allowed_hosts
  allowed_hosts="${allowed_hosts:-*}"
  read -r -s -u 3 -p "Initial RemoteLink admin password: " admin_password; echo
  [[ ${#admin_password} -ge 4 ]] || { echo "Admin password must contain at least 4 characters" >&2; exit 1; }
  [[ "${allowed_hosts}" != *$'\n'* && "${allowed_hosts}" != *'"'* ]] || {
    echo "Allowed hosts must not contain quotes or newlines" >&2; exit 1;
  }
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y build-essential cmake ninja-build pkg-config git python3 curl openssl \
  ca-certificates libopenh264-dev libopus-dev libyuv-dev libssl-dev nlohmann-json3-dev

if apt-cache show freerdp3-dev >/dev/null 2>&1 && apt-cache show libwinpr3-dev >/dev/null 2>&1; then
  apt-get install -y freerdp3-dev libwinpr3-dev
else
  echo "FreeRDP 3 development packages are unavailable; building FreeRDP ${freerdp_version} ..."
  apt-get install -y libasound2-dev libcups2-dev libicu-dev libjpeg-dev libkrb5-dev \
    libpam0g-dev libpcsclite-dev libpulse-dev libsystemd-dev libusb-1.0-0-dev \
    libx11-dev libxcursor-dev libxdamage-dev libxext-dev libxfixes-dev \
    libxi-dev libxinerama-dev libxkbcommon-dev libxkbfile-dev libxrandr-dev \
    libxrender-dev libxtst-dev uuid-dev
  freerdp_source="$(mktemp -d /tmp/remotelink-freerdp.XXXXXXXX)"
  git clone --depth 1 --branch "${freerdp_version}" --recurse-submodules \
    https://github.com/FreeRDP/FreeRDP.git "${freerdp_source}/FreeRDP"
  cmake -S "${freerdp_source}/FreeRDP" -B "${freerdp_source}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF -DWITH_SERVER=OFF \
    -DWITH_SAMPLE=OFF -DWITH_MANPAGES=OFF -DWITH_X11=OFF -DWITH_WAYLAND=OFF
  cmake --build "${freerdp_source}/build" --parallel "$(nproc)"
  cmake --install "${freerdp_source}/build"
  ldconfig
fi
pkg-config --exists freerdp3 freerdp-client3 winpr3 || {
  echo "FreeRDP 3 installation completed but its pkg-config modules were not found" >&2; exit 1;
}

cmake -S "${project_root}" -B "${project_root}/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DREMOTE_GATEWAY_ENABLE_STREAMING=ON
cmake --build "${project_root}/build" --parallel "$(nproc)"
ctest --test-dir "${project_root}/build" --output-on-failure

getent passwd remote-gateway >/dev/null || \
  useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin remote-gateway
install -d -o root -g root -m 0755 "${install_root}/bin" "${install_root}/lib" "${install_root}/web"
install -d -o root -g remote-gateway -m 0750 "${config_root}" "${config_root}/tls"
install -d -o root -g root -m 0700 "${credential_root}"
install -d -o remote-gateway -g remote-gateway -m 0750 "${state_root}"
install -m 0755 "${project_root}/build/remote-gateway" "${install_root}/bin/remote-gateway"
install -m 0644 "${project_root}/build/libremote_gateway_streaming.so" "${install_root}/lib/"
if [[ -f "${project_root}/build/libremote_gateway_core.so" ]]; then
  install -m 0644 "${project_root}/build/libremote_gateway_core.so" "${install_root}/lib/"
fi
shopt -s nullglob
mapfile -t datachannel_libraries < <(find "${project_root}/build" -name 'libdatachannel.so*' -print)
(( ${#datachannel_libraries[@]} > 0 )) || { echo "libdatachannel was not built" >&2; exit 1; }
cp -a "${datachannel_libraries[@]}" "${install_root}/lib/"
shopt -u nullglob
cp -a "${project_root}/web/." "${install_root}/web/"

certificate_path="${config_root}/tls/fullchain.pem"
private_key_path="${config_root}/tls/privkey.pem"
provided_certificate="${REMOTELINK_TLS_CERTIFICATE:-}"
provided_private_key="${REMOTELINK_TLS_PRIVATE_KEY:-}"
tls_mode="${REMOTELINK_TLS_MODE:-auto}"
certificate_status="existing"

case "${tls_mode}" in
  auto|off) ;;
  *) echo "REMOTELINK_TLS_MODE must be 'auto' or 'off'" >&2; exit 1 ;;
esac

if [[ "${tls_mode}" == "off" ]]; then
  certificate_path=""
  private_key_path=""
  certificate_status="proxy"
elif [[ -n "${provided_certificate}" || -n "${provided_private_key}" ]]; then
  [[ -f "${provided_certificate}" && -f "${provided_private_key}" ]] || {
    echo "REMOTELINK_TLS_CERTIFICATE and REMOTELINK_TLS_PRIVATE_KEY must both name readable files" >&2
    exit 1
  }
  openssl x509 -in "${provided_certificate}" -noout >/dev/null
  openssl pkey -in "${provided_private_key}" -check -noout >/dev/null
  certificate_public_key="$(openssl x509 -in "${provided_certificate}" -pubkey -noout | openssl sha256)"
  private_public_key="$(openssl pkey -in "${provided_private_key}" -pubout | openssl sha256)"
  [[ "${certificate_public_key}" == "${private_public_key}" ]] || {
    echo "The supplied TLS certificate and private key do not match" >&2; exit 1;
  }
  install -o root -g remote-gateway -m 0644 "${provided_certificate}" "${certificate_path}"
  install -o root -g remote-gateway -m 0640 "${provided_private_key}" "${private_key_path}"
  certificate_status="provided"
elif [[ ! -s "${certificate_path}" || ! -s "${private_key_path}" ]]; then
  gateway_name="$(hostname -f 2>/dev/null || hostname)"
  gateway_ip="$(hostname -I | awk '{print $1}')"
  if [[ "${gateway_name}" =~ ^[0-9.]+$ ]]; then san="IP:${gateway_name}"; else san="DNS:${gateway_name}"; fi
  [[ -z "${gateway_ip}" || "${gateway_ip}" == "${gateway_name}" ]] || san="${san},IP:${gateway_ip}"
  openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 825 \
    -subj "/CN=${gateway_name}" -addext "subjectAltName=${san},IP:127.0.0.1,DNS:localhost" \
    -keyout "${private_key_path}" -out "${certificate_path}"
  chown root:remote-gateway "${certificate_path}" "${private_key_path}"
  chmod 0644 "${certificate_path}"; chmod 0640 "${private_key_path}"
  certificate_status="generated"
fi

if [[ ${preserve_config} -eq 0 ]]; then
  TARGET_HOST="${target_host}" TARGET_USER="${target_user}" TARGET_DOMAIN="${target_domain}" \
  python3 - "${config_root}/targets.json" <<'PY'
import json, os, sys
data={"targets":[{"id":"primary","name":os.environ["TARGET_HOST"],"host":os.environ["TARGET_HOST"],
"port":3389,"username":os.environ["TARGET_USER"],"passwordFile":"/run/credentials/remote-gateway.service/rdp-password",
"domain":os.environ["TARGET_DOMAIN"],"width":1920,"height":1080,"ignoreCertificate":True}]}
with open(sys.argv[1],"w",encoding="utf-8") as stream: json.dump(data,stream,ensure_ascii=False,indent=2)
PY
  chown root:remote-gateway "${config_root}/targets.json"; chmod 0640 "${config_root}/targets.json"

  access_token="$(openssl rand -hex 32)"
  printf '%s' "${access_token}" | systemd-creds encrypt --name=access-token - \
    "${credential_root}/remote-gateway-access-token" >/dev/null
  printf '%s' "${target_password}" | systemd-creds encrypt --name=rdp-password - \
    "${credential_root}/remote-gateway-rdp-password" >/dev/null
  printf '%s' "${admin_password}" | systemd-creds encrypt --name=admin-password - \
    "${credential_root}/remote-gateway-admin-password" >/dev/null
  chmod 0600 "${credential_root}/remote-gateway-"*
fi

if [[ "${tls_mode}" == "off" ]]; then
  tls_environment="Environment=RG_TLS_CERTIFICATE=
Environment=RG_TLS_PRIVATE_KEY=
Environment=RG_BEHIND_TLS_PROXY=1"
  public_scheme="http"
else
  tls_environment="Environment=RG_BEHIND_TLS_PROXY=
Environment=RG_TLS_CERTIFICATE=${certificate_path}
Environment=RG_TLS_PRIVATE_KEY=${private_key_path}"
  public_scheme="https"
fi

install -d -o root -g root -m 0755 "/etc/systemd/system/${service_name}.service.d"
cat > "/etc/systemd/system/${service_name}.service.d/10-remotelink-tls.conf" <<EOF
[Service]
${tls_environment}
EOF

if [[ ${preserve_config} -eq 1 ]]; then
  systemctl daemon-reload
  systemctl restart "${service_name}.service"
  sleep 2
  systemctl is-active --quiet "${service_name}.service"
  unset target_password admin_password
  echo "RemoteLink binaries were updated; existing configuration and credentials were preserved."
  [[ "${certificate_status}" != "generated" ]] || echo "A missing TLS certificate was replaced with a self-signed certificate."
  [[ "${certificate_status}" != "proxy" ]] || echo "RemoteLink now serves HTTP on port 18080 behind the TLS proxy."
  exit 0
fi

cat > "/etc/systemd/system/${service_name}.service" <<EOF
[Unit]
Description=RemoteLink remote connection service
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=remote-gateway
Group=remote-gateway
WorkingDirectory=${install_root}
ExecStart=${install_root}/bin/remote-gateway
Restart=on-failure
RestartSec=2
UMask=0077
StateDirectory=remote-gateway
StateDirectoryMode=0750
Environment=LD_LIBRARY_PATH=${install_root}/lib
Environment=RG_WEB_ROOT=${install_root}/web
Environment=RG_STATE_DIR=${state_root}
Environment=RG_ACCESS_TOKEN_FILE=%d/access-token
Environment=RG_INITIAL_ADMIN_PASSWORD_FILE=%d/admin-password
Environment=RG_TARGETS_FILE=${config_root}/targets.json
Environment="RG_ALLOWED_HOSTS=${allowed_hosts}"
${tls_environment}
LoadCredentialEncrypted=access-token:${credential_root}/remote-gateway-access-token
LoadCredentialEncrypted=admin-password:${credential_root}/remote-gateway-admin-password
LoadCredentialEncrypted=rdp-password:${credential_root}/remote-gateway-rdp-password
NoNewPrivileges=true
PrivateTmp=true
PrivateDevices=true
ProtectSystem=strict
ProtectHome=true
RestrictSUIDSGID=true
RestrictNamespaces=true
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK
SystemCallArchitectures=native
LockPersonality=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable --now "${service_name}.service"
systemctl restart "${service_name}.service"
sleep 2
systemctl is-active --quiet "${service_name}.service"
unset target_password admin_password access_token
echo
echo "RemoteLink is running: ${public_scheme}://$(hostname -I | awk '{print $1}'):18080"
echo "Login username: admin"
if [[ "${certificate_status}" == "generated" ]]; then
  echo "A self-signed TLS certificate was generated; trust it on client devices or replace it with your certificate."
elif [[ "${certificate_status}" == "provided" ]]; then
  echo "The supplied TLS certificate and private key were installed."
elif [[ "${certificate_status}" == "proxy" ]]; then
  echo "RemoteLink is serving HTTP on port 18080; the reverse proxy must provide HTTPS/WSS."
fi
