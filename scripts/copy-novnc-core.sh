#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_root="${project_root}/node_modules/@novnc/novnc"
destination_root="${1:-${project_root}/web/vendor/novnc}"

[[ -f "${source_root}/core/rfb.js" ]] || {
  echo "noVNC Core is missing; run npm ci first" >&2
  exit 1
}

case "${destination_root}" in
  "${project_root}"/*|/tmp/*) ;;
  *) echo "refusing to replace unexpected destination: ${destination_root}" >&2; exit 1 ;;
esac

rm -rf -- "${destination_root}"
install -d -m 0755 "${destination_root}"
cp -a "${source_root}/core" "${destination_root}/core"
cp -a "${source_root}/vendor" "${destination_root}/vendor"
install -m 0644 "${source_root}/LICENSE.txt" "${destination_root}/LICENSE.txt"

