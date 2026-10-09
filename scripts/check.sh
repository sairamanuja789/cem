#!/usr/bin/env bash
# Everything CI runs (MAINT-001..003, SEC-001, REPRO-002). Run inside the dev or ci container:
#   scripts/dev.sh scripts/check.sh        (set CEMKIT_TARGET=ci to use the ci image)
# Every stage runs even after a failure; the summary lists each result; exit status is non-zero if any
# stage failed.
set -uo pipefail

if [ ! -f /opt/cemkit/pins.env ] || [ -z "${CEMKIT_IMAGE_TARGET:-}" ]; then
  echo "check.sh: refusing to run outside the pinned container. Use scripts/dev.sh scripts/check.sh" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$root/.cache/vcpkg/binary}"
export CCACHE_DIR="${CCACHE_DIR:-$root/.cache/ccache}"
export UV_CACHE_DIR="${UV_CACHE_DIR:-$root/.cache/uv}"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"

. "$root/scripts/lib/coverage.sh"
jobs=6   # CLAUDE.md: never more than -j 6 (16 GB machine)
declare -a names=() results=()

stage() { # name command...
  local name="$1"; shift
  echo; echo "=== [$name] $*"
  if "$@"; then results+=("PASS"); else results+=("FAIL"); fi
  names+=("$name")
}

preset_stage() { # preset: configure, build, ctest
  local p="$1"
  cmake --preset "$p" && cmake --build --preset "$p" -j "$jobs" && ctest --preset "$p"
}


kernel_sources() { # project C++ sources; the gate sources are meant not to compile cleanly
  find kernel/cemkit kernel/products kernel/testing -name "$1" -not -path '*/testing/gates/*' \
    2>/dev/null | sort
}

clang_tidy() {
  # shellcheck disable=SC2046
  run-clang-tidy -p build/clang-debug -quiet $(kernel_sources '*.cpp')
}

boundaries() { # ADR-009 dependency rules: include scan, link graph, Python imports
  mkdir -p build/gcc-debug/graph
  cmake --graphviz=build/gcc-debug/graph/cemkit.dot build/gcc-debug >/dev/null \
    && uv run python scripts/check_boundaries.py --kernel kernel --python python/cemkit \
      --manifest build/gcc-debug/cemkit_modules.txt --dot build/gcc-debug/graph/cemkit.dot
}

clang_format_check() {
  { kernel_sources '*.cpp'; kernel_sources '*.hpp'; find kernel/testing/gates -name '*.cpp'; } \
    | xargs clang-format --dry-run --Werror
}

gcovr_report() {
  local floor=()
  # shellcheck disable=SC2207
  floor=($(coverage_floor_args "$root"))
  if [ "${#floor[@]}" -eq 0 ]; then
    echo "note: no code yet in the MAINT-003 scope; coverage floor not applied"
  fi
  mkdir -p build/coverage
  uv run gcovr --config gcovr.cfg --object-directory build/gcc-coverage \
    --txt build/coverage/summary.txt --xml build/coverage/coverage.xml "${floor[@]}" \
    && cat build/coverage/summary.txt
}

gitleaks_scan() {
  gitleaks dir . --config .gitleaks.toml --no-banner --redact \
    && { [ ! -d .git ] || gitleaks git . --config .gitleaks.toml --no-banner --redact; }
}

negative_tests() {
  ctest --preset gcc-debug -R '^negative_' --no-tests=error \
    && ctest --preset clang-asan -R '^negative_' --no-tests=error \
    && uv run pytest -q tests/python/test_gitleaks.py
}

repro_002() {
  local pin baseline
  pin="$(grep '^VCPKG_COMMIT=' docker/pins.env | cut -d= -f2)"
  baseline="$(jq -r '."builtin-baseline"' vcpkg.json)"
  if [ "$pin" != "$baseline" ]; then
    echo "vcpkg builtin-baseline ($baseline) differs from VCPKG_COMMIT in docker/pins.env ($pin)" >&2
    return 1
  fi
  echo "vcpkg baseline matches pin: $pin"
  uv lock --check && uv sync --locked
}

stage "toolchain pins"        scripts/versions.sh
stage "REPRO-002"             repro_002
stage "gcc-debug"             preset_stage gcc-debug
stage "clang-debug"           preset_stage clang-debug
stage "clang-asan"            preset_stage clang-asan
stage "gcc-coverage"          preset_stage gcc-coverage
stage "release"               preset_stage release
stage "boundaries (ADR-009)"  boundaries
stage "clang-tidy"            clang_tidy
stage "clang-format"          clang_format_check
stage "ruff check"            uv run ruff check .
stage "ruff format"           uv run ruff format --check .
stage "mypy --strict"         uv run mypy --strict python/ tests/python scripts
stage "pytest + coverage"     uv run pytest -q --cov --cov-report=term-missing
stage "gcovr (MAINT-003)"     gcovr_report
stage "gitleaks (SEC-001)"    gitleaks_scan
stage "negative tests"        negative_tests

echo; echo "================ check.sh summary ================"
failed=0
for i in "${!names[@]}"; do
  printf '%-24s %s\n' "${names[$i]}" "${results[$i]}"
  [ "${results[$i]}" = "PASS" ] || failed=1
done
if [ "$failed" -ne 0 ]; then echo "check.sh: FAILED"; exit 1; fi
echo "check.sh: all stages passed"
