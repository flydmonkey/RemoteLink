#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
old_pid="$(pgrep -xo remotelink || true)"
if [[ -z "${old_pid}" ]]; then
  echo "remotelink must be running so its existing RDP credential can be migrated" >&2
  exit 1
fi
read_env() {
  tr '\0' '\n' < "/proc/${old_pid}/environ" | sed -n "s/^${1}=//p"
}
rdp_password="$(read_env REMOTELINK_TARGET_SERVER_55_PASSWORD)"
certificate="$(read_env REMOTELINK_TLS_CERTIFICATE)"
private_key="$(read_env REMOTELINK_TLS_PRIVATE_KEY)"
if [[ -z "${rdp_password}" || -z "${certificate}" || -z "${private_key}" ]]; then
  echo "running process is missing a required deployment value" >&2
  exit 1
fi
access_token="$(openssl rand -hex 32)"

sudo -n true
if ! getent passwd remotelink >/dev/null; then
  sudo useradd --system --user-group --home-dir /nonexistent --shell /usr/sbin/nologin remotelink
fi
sudo install -d -o root -g root -m 0755 /opt/remotelink/bin /opt/remotelink/lib /opt/remotelink/web
sudo install -d -o root -g remotelink -m 0750 /etc/remotelink /etc/remotelink/tls
sudo install -d -o root -g root -m 0700 /etc/credstore.encrypted
sudo install -o root -g root -m 0755 "${project_root}/build/remotelink" /opt/remotelink/bin/remotelink
sudo install -o root -g root -m 0644 "${project_root}/build/libremotelink_core.so" /opt/remotelink/lib/
sudo install -o root -g root -m 0644 "${project_root}/build/libremotelink_streaming.so" /opt/remotelink/lib/
sudo install -o root -g root -m 0644 "${project_root}/build/third_party/libdatachannel/libdatachannel.so.0.24" /opt/remotelink/lib/
sudo ln -sfn libdatachannel.so.0.24 /opt/remotelink/lib/libdatachannel.so.0
sudo install -o root -g root -m 0644 "${project_root}/web/index.html" /opt/remotelink/web/index.html
sudo install -o root -g remotelink -m 0640 "${project_root}/config/targets.systemd.local.json" /etc/remotelink/targets.json
sudo install -o root -g remotelink -m 0644 "${certificate}" /etc/remotelink/tls/fullchain.pem
sudo install -o root -g remotelink -m 0640 "${private_key}" /etc/remotelink/tls/privkey.pem

printf '%s' "${access_token}" | sudo systemd-creds encrypt --name=access-token - /etc/credstore.encrypted/remotelink-access-token >/dev/null
printf '%s' "${rdp_password}" | sudo systemd-creds encrypt --name=server-55-password - /etc/credstore.encrypted/remotelink-server-55-password >/dev/null
sudo chmod 0600 /etc/credstore.encrypted/remotelink-access-token /etc/credstore.encrypted/remotelink-server-55-password
sudo install -o root -g root -m 0644 "${project_root}/config/remotelink.service.local" /etc/systemd/system/remotelink.service

kill -TERM "${old_pid}"
while kill -0 "${old_pid}" 2>/dev/null; do sleep 0.1; done
sudo systemctl daemon-reload
sudo systemctl enable --now remotelink.service
unset access_token rdp_password
echo "installed; decrypt the new access token directly into your password manager or host clipboard"
