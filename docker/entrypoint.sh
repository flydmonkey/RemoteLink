#!/usr/bin/env bash
set -euo pipefail

state_dir="${RG_STATE_DIR:-/var/lib/remote-gateway}"
umask 077
mkdir -p "${state_dir}"

if [[ -z "${RG_ACCESS_TOKEN:-}" && -z "${RG_ACCESS_TOKEN_FILE:-}" ]]; then
  token_file="${state_dir}/access-token"
  if [[ ! -s "${token_file}" ]]; then
    head -c 32 /dev/urandom | base64 | tr -d '\n' > "${token_file}"
  fi
  export RG_ACCESS_TOKEN_FILE="${token_file}"
fi

if [[ -n "${REMOTELINK_ADMIN_PASSWORD:-}" && -z "${RG_INITIAL_ADMIN_PASSWORD_FILE:-}" ]]; then
  password_file="${state_dir}/initial-admin-password"
  if [[ ! -s "${password_file}" ]]; then
    printf '%s' "${REMOTELINK_ADMIN_PASSWORD}" > "${password_file}"
  fi
  export RG_INITIAL_ADMIN_PASSWORD_FILE="${password_file}"
fi

exec "$@"
