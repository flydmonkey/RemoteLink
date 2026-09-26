#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${REMOTELINK_VERSION:-$(tr -d '[:space:]' < "${project_root}/VERSION")}"
version="${version#v}"
image="${REMOTELINK_IMAGE:-remotelink}"
platform="${REMOTELINK_PLATFORM:-linux/amd64}"
tag="${image}:${version}"

tags=(-t "${tag}")
if [[ "${REMOTELINK_TAG_LATEST:-1}" == "1" ]]; then tags+=(-t "${image}:latest"); fi
if docker --version 2>/dev/null | grep -qi podman; then
  docker build --platform "${platform}" --build-arg "REMOTELINK_VERSION=${version}" \
    "${tags[@]}" "${project_root}"
  if [[ "${REMOTELINK_PUSH:-0}" == "1" ]]; then
    docker push "${tag}"
    if [[ "${REMOTELINK_TAG_LATEST:-1}" == "1" ]]; then docker push "${image}:latest"; fi
  fi
else
  arguments=(build --platform "${platform}" --build-arg "REMOTELINK_VERSION=${version}" "${tags[@]}")
  if [[ "${REMOTELINK_PUSH:-0}" == "1" ]]; then arguments+=(--push); else arguments+=(--load); fi
  arguments+=("${project_root}")
  docker buildx inspect >/dev/null 2>&1 || docker buildx create --use >/dev/null
  docker buildx build "${arguments[@]}"
fi
printf 'Built %s\n' "${tag}"
