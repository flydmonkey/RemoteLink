#!/usr/bin/env bash
set -euo pipefail

tls_dir="${1:-/etc/remotelink/tls}"
certificate="${tls_dir}/fullchain.pem"
private_key="${tls_dir}/privkey.pem"
marker="${tls_dir}/.generated"
mkdir -p "${tls_dir}"

san="DNS:localhost,IP:127.0.0.1"
advertised="${REMOTELINK_ICE_ADVERTISED_ADDRESS:-}"
if [[ "${advertised}" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ || "${advertised}" == *:* ]]; then
  san="${san},IP:${advertised}"
fi

if [[ -s "${certificate}" && -s "${private_key}" && ! -f "${marker}" ]]; then
  exit 0
fi
if [[ -f "${marker}" && "$(cat "${marker}")" == "${san}" && -s "${certificate}" && -s "${private_key}" ]]; then
  exit 0
fi

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 825 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=${san}" \
  -keyout "${private_key}" -out "${certificate}"
chmod 0644 "${certificate}"
chmod 0640 "${private_key}"
printf '%s\n' "${san}" > "${marker}"
chmod 0644 "${marker}"
