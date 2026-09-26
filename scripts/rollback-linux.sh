#!/usr/bin/env bash
set -euo pipefail
if [[ ${EUID} -ne 0 ]]; then echo "run as root" >&2; exit 1; fi
backup_dir="$(cat /opt/remotelink/previous-release)"
test -d "${backup_dir}/bin" && test -d "${backup_dir}/lib" && test -d "${backup_dir}/web"
cp -a "${backup_dir}/bin/." /opt/remotelink/bin/
cp -a "${backup_dir}/lib/." /opt/remotelink/lib/
cp -a "${backup_dir}/web/." /opt/remotelink/web/
systemctl restart remotelink
systemctl is-active --quiet remotelink
