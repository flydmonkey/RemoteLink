#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_id="$(date -u +%Y%m%d%H%M%S)"
backup_dir="/opt/remotelink/backups/${release_id}"

if [[ ${EUID} -ne 0 ]]; then echo "run as root" >&2; exit 1; fi
bash "${project_root}/scripts/copy-novnc-core.sh"
install -d -m 0755 "${backup_dir}"
cp -a /opt/remotelink/bin /opt/remotelink/lib /opt/remotelink/web "${backup_dir}/"
install -m 0755 "${project_root}/build/remotelink" /opt/remotelink/bin/remotelink
install -m 0755 "${project_root}/build/libremotelink_core.so" /opt/remotelink/lib/libremotelink_core.so
install -m 0755 "${project_root}/build/libremotelink_streaming.so" /opt/remotelink/lib/libremotelink_streaming.so
cp -a "${project_root}/web/." /opt/remotelink/web/
echo "${backup_dir}" > /opt/remotelink/previous-release
systemctl restart remotelink
systemctl is-active --quiet remotelink
