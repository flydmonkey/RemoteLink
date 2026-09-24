#!/usr/bin/env bash
set -euo pipefail
if [[ ${EUID} -ne 0 ]]; then echo "run as root" >&2; exit 1; fi
backup_dir="$(cat /opt/remote-gateway/previous-release)"
test -d "${backup_dir}/bin" && test -d "${backup_dir}/lib" && test -d "${backup_dir}/web"
cp -a "${backup_dir}/bin/." /opt/remote-gateway/bin/
cp -a "${backup_dir}/lib/." /opt/remote-gateway/lib/
cp -a "${backup_dir}/web/." /opt/remote-gateway/web/
systemctl restart remote-gateway
systemctl is-active --quiet remote-gateway
