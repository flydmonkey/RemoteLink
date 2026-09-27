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

# Migrate print jobs created before per-user print directories were introduced.
# Only jobs with an explicit numeric owner marker are moved; unowned jobs stay
# quarantined in the legacy directory instead of being exposed to any user.
legacy_print_directory=/var/lib/remotelink/print-jobs
if [[ -d "${legacy_print_directory}" ]]; then
    shopt -s nullglob
    for owner_file in "${legacy_print_directory}"/*.pdf.owner; do
        owner_id="$(tr -d '[:space:]' < "${owner_file}")"
        [[ "${owner_id}" =~ ^[0-9]+$ ]] || continue
        pdf_file="${owner_file%.owner}"
        [[ -f "${pdf_file}" ]] || continue
        user_print_directory="/var/lib/remotelink/users/${owner_id}/print-jobs"
        install -d -o remotelink -g remotelink -m 0750 "${user_print_directory}"
        destination="${user_print_directory}/$(basename "${pdf_file}")"
        if [[ ! -e "${destination}" ]]; then
            mv -- "${pdf_file}" "${destination}"
            chown remotelink:remotelink "${destination}"
            rm -- "${owner_file}"
        fi
    done
    shopt -u nullglob
fi

if command -v podman >/dev/null 2>&1; then
    remotelink_uid="$(id -u remotelink)"
    remotelink_gid="$(id -g remotelink)"
    sed -e "s/@REMOTELINK_UID@/${remotelink_uid}/g" \
        -e "s/@REMOTELINK_GID@/${remotelink_gid}/g" \
        "${project_root}/deploy/remotelink-guacd.service" \
        > /etc/systemd/system/remotelink-guacd.service
    chmod 0644 /etc/systemd/system/remotelink-guacd.service
    systemctl daemon-reload
    systemctl enable remotelink-guacd.service
    systemctl restart remotelink-guacd.service
fi
echo "${backup_dir}" > /opt/remotelink/previous-release
systemctl restart remotelink
systemctl is-active --quiet remotelink
