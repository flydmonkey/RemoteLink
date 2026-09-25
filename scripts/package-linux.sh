#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-${project_root}/build}"
output_dir="${project_root}/dist"
version="${PACKAGE_VERSION:-$(tr -d '[:space:]' < "${project_root}/VERSION")}"
version="${version#v}"
version="${version//\//-}"
version="${version//[^a-zA-Z0-9._-]/-}"
stage="$(mktemp -d)"
cleanup() {
  case "${stage}" in /tmp/*) rm -rf -- "${stage}" ;; esac
}
trap cleanup EXIT

cmake --build "${build_dir}" -j"$(nproc)"
ctest --test-dir "${build_dir}" --output-on-failure
bash "${project_root}/scripts/copy-novnc-core.sh"
package_root="${stage}/RemoteLink"
install -D -m 0755 "${build_dir}/remote-gateway" "${package_root}/bin/remote-gateway"
install -D -m 0755 "${build_dir}/libremote_gateway_streaming.so" "${package_root}/lib/libremote_gateway_streaming.so"
if [[ -f "${build_dir}/libremote_gateway_core.so" ]]; then
  install -m 0755 "${build_dir}/libremote_gateway_core.so" "${package_root}/lib/libremote_gateway_core.so"
fi
shopt -s nullglob
mapfile -t datachannel_libraries < <(find "${build_dir}" -name 'libdatachannel.so*' -print)
(( ${#datachannel_libraries[@]} > 0 )) || { echo "libdatachannel was not built" >&2; exit 1; }
cp -a "${datachannel_libraries[@]}" "${package_root}/lib/"
shopt -u nullglob
cp -a "${project_root}/web" "${package_root}/web"
find "${package_root}/web" -type f -name '*.html' -exec \
  sed -i "s/__REMOTELINK_VERSION__/${version}/g" {} +
install -D -m 0644 "${project_root}/config/remote-gateway.service.example" \
  "${package_root}/config/remote-gateway.service.example"
install -m 0644 "${project_root}/config/remote-gateway.env.example" \
  "${project_root}/config/targets.example.json" \
  "${project_root}/config/targets.systemd.example.json" "${package_root}/config/"
cp -a "${project_root}/scripts" "${package_root}/scripts"
install -m 0644 "${project_root}/README.md" "${project_root}/README.zh-CN.md" \
  "${project_root}/CHANGELOG.md" "${package_root}/"
printf '%s\n' "${version}" > "${package_root}/VERSION"
cp -a "${project_root}/docs" "${package_root}/docs"
mkdir -p "${output_dir}"
archive="${output_dir}/RemoteLink-linux-x86_64-${version}.tar.gz"
tar -C "${stage}" -czf "${archive}" RemoteLink
sha256sum "${archive}" > "${archive}.sha256"
printf '%s\n' "${archive}"
