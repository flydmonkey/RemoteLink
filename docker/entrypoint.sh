#!/usr/bin/env bash
set -euo pipefail

state_dir="${REMOTELINK_STATE_DIR:-/var/lib/remotelink}"
umask 077
mkdir -p "${state_dir}"

if [[ -z "${REMOTELINK_ACCESS_TOKEN:-}" && -z "${REMOTELINK_ACCESS_TOKEN_FILE:-}" ]]; then
  token_file="${state_dir}/access-token"
  if [[ ! -s "${token_file}" ]]; then
    head -c 32 /dev/urandom | base64 | tr -d '\n' > "${token_file}"
  fi
  export REMOTELINK_ACCESS_TOKEN_FILE="${token_file}"
fi

if [[ -n "${REMOTELINK_ADMIN_PASSWORD:-}" && -z "${REMOTELINK_INITIAL_ADMIN_PASSWORD_FILE:-}" ]]; then
  password_file="${state_dir}/initial-admin-password"
  if [[ ! -s "${password_file}" ]]; then
    printf '%s' "${REMOTELINK_ADMIN_PASSWORD}" > "${password_file}"
  fi
  export REMOTELINK_INITIAL_ADMIN_PASSWORD_FILE="${password_file}"
fi

exec "$@"
