#!/usr/bin/env bash
# Builds the dev image from docker/pins.env. Usage: scripts/docker-build.sh [extra docker build args]
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pins="$root/docker/pins.env"
args=()
while IFS='=' read -r key value; do
  case "$key" in ''|\#*) continue ;; esac
  args+=(--build-arg "$key=$value")
done < <(grep -E '^[A-Z_0-9]+=' "$pins")
exec docker build --target "${FANCEM_TARGET:-dev}" --progress=plain "${args[@]}" -f "$root/docker/Dockerfile" -t "fancem-${FANCEM_TARGET:-dev}:latest" "$@" "$root"
