#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
old_pid="$(pgrep -xo remote-gateway || true)"
if [[ -z "${old_pid}" ]]; then
  echo "remote-gateway must be running so its existing RDP credential can be migrated" >&2
  exit 1
fi
read_env() {
  tr '\0' '\n' < "/proc/${old_pid}/environ" | sed -n "s/^${1}=//p"
}
rdp_password="$(read_env RG_TARGET_SERVER_55_PASSWORD)"
certificate="$(read_env RG_TLS_CERTIFICATE)"
private_key="$(read_env RG_TLS_PRIVATE_KEY)"
if [[ -z "${rdp_password}" || -z "${certificate}" || -z "${private_key}" ]]; then
  echo "running process is missing a required deployment value" >&2
  exit 1
fi
access_token="$(openssl rand -hex 32)"

sudo -n true
if ! getent passwd remote-gateway >/dev/null; then
  sudo useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin remote-gateway
fi
sudo install -d -o root -g root -m 0755 /opt/remote-gateway/bin /opt/remote-gateway/lib /opt/remote-gateway/web
sudo install -d -o root -g remote-gateway -m 0750 /etc/remote-gateway /etc/remote-gateway/tls
sudo install -d -o root -g root -m 0700 /etc/credstore.encrypted
sudo install -o root -g root -m 0755 "${project_root}/build/remote-gateway" /opt/remote-gateway/bin/remote-gateway
sudo install -o root -g root -m 0644 "${project_root}/build/libremote_gateway_core.so" /opt/remote-gateway/lib/
sudo install -o root -g root -m 0644 "${project_root}/build/libremote_gateway_streaming.so" /opt/remote-gateway/lib/
sudo install -o root -g root -m 0644 "${project_root}/build/third_party/libdatachannel/libdatachannel.so.0.24" /opt/remote-gateway/lib/
sudo ln -sfn libdatachannel.so.0.24 /opt/remote-gateway/lib/libdatachannel.so.0
sudo install -o root -g root -m 0644 "${project_root}/web/index.html" /opt/remote-gateway/web/index.html
sudo install -o root -g remote-gateway -m 0640 "${project_root}/config/targets.systemd.local.json" /etc/remote-gateway/targets.json
sudo install -o root -g remote-gateway -m 0644 "${certificate}" /etc/remote-gateway/tls/fullchain.pem
sudo install -o root -g remote-gateway -m 0640 "${private_key}" /etc/remote-gateway/tls/privkey.pem

printf '%s' "${access_token}" | sudo systemd-creds encrypt --name=access-token - /etc/credstore.encrypted/remote-gateway-access-token >/dev/null
printf '%s' "${rdp_password}" | sudo systemd-creds encrypt --name=server-55-password - /etc/credstore.encrypted/remote-gateway-server-55-password >/dev/null
sudo chmod 0600 /etc/credstore.encrypted/remote-gateway-access-token /etc/credstore.encrypted/remote-gateway-server-55-password
sudo install -o root -g root -m 0644 "${project_root}/config/remote-gateway.service.local" /etc/systemd/system/remote-gateway.service

kill -TERM "${old_pid}"
while kill -0 "${old_pid}" 2>/dev/null; do sleep 0.1; done
sudo systemctl daemon-reload
sudo systemctl enable --now remote-gateway.service
unset access_token rdp_password
echo "installed; decrypt the new access token directly into your password manager or host clipboard"
