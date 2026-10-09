#!/usr/bin/env bash
# Opens a shell (or runs a command) in the pinned dev container with the repo mounted at /workspace.
# Usage: scripts/dev.sh [command...]
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tty_args=(); [ -t 0 ] && tty_args=(-it)
exec docker run --rm "${tty_args[@]}" \
  --user "$(id -u):$(id -g)" \
  -v "$root":/workspace -w /workspace \
  -e HOME=/home/dev -e CCACHE_DIR=/workspace/.cache/ccache -e VCPKG_DEFAULT_BINARY_CACHE=/workspace/.cache/vcpkg/binary -e UV_CACHE_DIR=/workspace/.cache/uv \
  "cemkit-${CEMKIT_TARGET:-dev}:latest" "${@:-bash}"
