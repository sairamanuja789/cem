#!/usr/bin/env bash
# Opens a shell (or runs a command) in the pinned dev container with the repo mounted at /workspace.
# Usage: scripts/dev.sh [command...]
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tty_args=(); [ -t 0 ] && tty_args=(-it)
image="cemkit-${CEMKIT_TARGET:-dev}:latest"
# STORE-002: the store records the image ID (sha256:...) of the container it ran in. Empty if the
# image cannot be inspected; the store then records the digest as "unknown".
digest="$(docker image inspect --format '{{.Id}}' "$image" 2>/dev/null || true)"
exec docker run --rm "${tty_args[@]}" \
  --user "$(id -u):$(id -g)" \
  -e CEMKIT_CONTAINER_DIGEST="$digest" \
  -v "$root":/workspace -w /workspace \
  -e HOME=/home/dev -e CCACHE_DIR=/workspace/.cache/ccache -e VCPKG_DEFAULT_BINARY_CACHE=/workspace/.cache/vcpkg/binary -e UV_CACHE_DIR=/workspace/.cache/uv \
  "$image" "${@:-bash}"
