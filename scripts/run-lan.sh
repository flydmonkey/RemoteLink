#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 /path/to/remotelink.env" >&2
  exit 2
fi

config_file=$1
if [[ ! -r $config_file ]]; then
  echo "configuration is not readable: $config_file" >&2
  exit 2
fi

set -a
# The file is administrator-controlled and must contain shell-compatible KEY=VALUE lines.
source "$config_file"
set +a

read -r -s -p "Gateway access token: " REMOTELINK_ACCESS_TOKEN
echo
read -r -s -p "RDP password: " REMOTELINK_RDP_PASSWORD
echo
export REMOTELINK_ACCESS_TOKEN REMOTELINK_RDP_PASSWORD

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_dir=$(cd -- "$script_dir/.." && pwd)
exec "$project_dir/build/remotelink"
