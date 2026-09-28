#!/usr/bin/env bash
set -euo pipefail

state_dir="${REMOTELINK_STATE_DIR:-/var/lib/remotelink}"
umask 077
mkdir -p "${state_dir}"
/usr/local/bin/remotelink-generate-certificate

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

guacd_host="${REMOTELINK_GUACD_HOST:-127.0.0.1}"
guacd_port="${REMOTELINK_GUACD_PORT:-4822}"
if [[ "${REMOTELINK_START_GUACD:-1}" != "1" || "${guacd_host}" != "127.0.0.1" ]]; then
  exec "$@"
fi

guacd_log_level="${REMOTELINK_GUACD_LOG_LEVEL:-info}"
/usr/sbin/guacd -f -L "${guacd_log_level}" -b 127.0.0.1 -l "${guacd_port}" &
guacd_pid=$!
printf '%s\n' "${guacd_pid}" > /tmp/remotelink-guacd.pid
"$@" &
remotelink_pid=$!

cleanup() {
  trap - EXIT TERM INT
  kill -TERM "${remotelink_pid}" "${guacd_pid}" 2>/dev/null || true
  wait "${remotelink_pid}" 2>/dev/null || true
  wait "${guacd_pid}" 2>/dev/null || true
  rm -f /tmp/remotelink-guacd.pid
}
trap cleanup EXIT TERM INT

set +e
wait -n "${remotelink_pid}" "${guacd_pid}"
status=$?
set -e
if kill -0 "${remotelink_pid}" 2>/dev/null && ! kill -0 "${guacd_pid}" 2>/dev/null; then
  echo "guacd exited unexpectedly" >&2
  status=1
fi
exit "${status}"
