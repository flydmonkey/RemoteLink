#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 OUTPUT_DIRECTORY GATEWAY_IP [DNS_NAME]" >&2
  exit 2
fi

output_dir=$1
gateway_ip=$2
dns_name=${3:-localhost}
mkdir -p -- "$output_dir"

openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 30 \
  -keyout "$output_dir/gateway-key.pem" \
  -out "$output_dir/gateway-cert.pem" \
  -subj "/CN=$dns_name" \
  -addext "subjectAltName=DNS:$dns_name,DNS:localhost,IP:127.0.0.1,IP:$gateway_ip"
chmod 600 "$output_dir/gateway-key.pem"

echo "Created test certificate: $output_dir/gateway-cert.pem"
echo "Created private key:      $output_dir/gateway-key.pem"
echo "Trust the certificate only on explicitly managed LAN clients."
