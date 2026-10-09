#!/usr/bin/env bash
# Prints every toolchain version and checks it against docker/pins.env.
# Run inside the dev container. Exit status is non-zero if any tool is missing or differs from its pin.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pins="${CEMKIT_PINS:-$here/../docker/pins.env}"
[ -f "$pins" ] || pins=/opt/cemkit/pins.env
# shellcheck disable=SC1090
. "$pins"

fail=0
row() { # name expected actual
  local status=OK
  [ "$2" = "$3" ] || { status=MISMATCH; fail=1; }
  printf '%-18s %-10s expected=%-36s actual=%s\n' "$1" "$status" "$2" "${3:-<missing>}"
}
apt_ver() { dpkg-query -W -f='${Version}' "$1" 2>/dev/null || true; }

for entry in \
  ca-certificates:APT_CA_CERTIFICATES curl:APT_CURL git:APT_GIT gnupg:APT_GNUPG \
  xz-utils:APT_XZ_UTILS zip:APT_ZIP unzip:APT_UNZIP tar:APT_TAR pkg-config:APT_PKG_CONFIG make:APT_MAKE \
  gcc-13:APT_GCC_13 g++-13:APT_GPP_13 clang-20:APT_CLANG_20 clang-format-20:APT_CLANG_FORMAT_20 \
  clang-tidy-20:APT_CLANG_TIDY_20 libclang-rt-20-dev:APT_LIBCLANG_RT_20_DEV llvm-20:APT_LLVM_20 cmake:APT_CMAKE ninja-build:APT_NINJA_BUILD ccache:APT_CCACHE \
  jq:APT_JQ \
 ; do
  pkg="${entry%%:*}"; var="${entry##*:}"
  row "$pkg" "${!var}" "$(apt_ver "$pkg")"
done

# Solver rows exist only in the dev image (CEMKIT_IMAGE_TARGET=dev); the ci image has no solvers.
if [ "${CEMKIT_IMAGE_TARGET:-dev}" = dev ]; then
for entry in calculix-ccx:APT_CALCULIX_CCX time:APT_TIME libglu1-mesa:APT_LIBGLU1_MESA libgl1:APT_LIBGL1 libxrender1:APT_LIBXRENDER1 libxcursor1:APT_LIBXCURSOR1 libxfixes3:APT_LIBXFIXES3 libxft2:APT_LIBXFT2 libfontconfig1:APT_LIBFONTCONFIG1 libxinerama1:APT_LIBXINERAMA1; do
  pkg="${entry%%:*}"; var="${entry##*:}"
  row "$pkg" "${!var}" "$(apt_ver "$pkg")"
done
row "openfoam${OPENFOAM_VERSION}" "$OPENFOAM_DEB_VERSION" "$(apt_ver "openfoam${OPENFOAM_VERSION}")"
row "gmsh (cli)" "$GMSH_VERSION" "$(command -v gmsh >/dev/null && gmsh -version 2>&1 | tail -1 | tr -d '\r')"
row "gmsh (python)" "$GMSH_VERSION" "$(/opt/gmsh-venv/bin/python -c 'import gmsh;gmsh.initialize();print(gmsh.__version__)' 2>/dev/null | tail -1)"
fi
row "gitleaks" "$GITLEAKS_VERSION" "$(gitleaks version 2>/dev/null | tr -d 'v')"
row "uv" "$UV_VERSION" "$(uv --version 2>/dev/null | awk '{print $2}')"
row "python" "$PYTHON_VERSION" "$("$(uv python find "$PYTHON_VERSION" 2>/dev/null)" -c 'import platform;print(platform.python_version())' 2>/dev/null || true)"
row "vcpkg commit" "$VCPKG_COMMIT" "$(git -C "${VCPKG_ROOT:-/opt/vcpkg}" rev-parse HEAD 2>/dev/null || true)"
row "snapshot" "$UBUNTU_SNAPSHOT" "$(grep -ho 'snapshot.ubuntu.com/ubuntu/[0-9TZ]*' /etc/apt/sources.list.d/ubuntu.sources 2>/dev/null | head -1 | sed 's#.*/##')"

if [ "$fail" -ne 0 ]; then echo "versions.sh: FAILED (see MISMATCH rows)" >&2; exit 1; fi
echo "versions.sh: all pins match"
