#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_id="$(date -u +%Y%m%d%H%M%S)"
backup_dir="/opt/remote-gateway/backups/${release_id}"

if [[ ${EUID} -ne 0 ]]; then echo "run as root" >&2; exit 1; fi
bash "${project_root}/scripts/copy-novnc-core.sh"
install -d -m 0755 "${backup_dir}"
cp -a /opt/remote-gateway/bin /opt/remote-gateway/lib /opt/remote-gateway/web "${backup_dir}/"
install -m 0755 "${project_root}/build/remote-gateway" /opt/remote-gateway/bin/remote-gateway
install -m 0755 "${project_root}/build/libremote_gateway_core.so" /opt/remote-gateway/lib/libremote_gateway_core.so
install -m 0755 "${project_root}/build/libremote_gateway_streaming.so" /opt/remote-gateway/lib/libremote_gateway_streaming.so
cp -a "${project_root}/web/." /opt/remote-gateway/web/
echo "${backup_dir}" > /opt/remote-gateway/previous-release
systemctl restart remote-gateway
systemctl is-active --quiet remote-gateway
