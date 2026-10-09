# Sourced by scripts/check.sh and tests/python/test_check_script.py (MAINT-003).
# coverage_floor_args [root]: prints the gcovr floor arguments for that tree, or nothing when none of
# kernel/src/{core,spec,physics} contains a .cpp file yet. Directories may be missing individually
# (spec and physics do not exist until T07 and T08); a missing directory must never switch the floor off
# while another of the three contains code.
coverage_floor_args() {
  local root="${1:-.}" found
  found="$(find "$root/kernel/src" -type f -name '*.cpp' \
    \( -path '*/core/*' -o -path '*/spec/*' -o -path '*/physics/*' \) 2>/dev/null | head -n 1)"
  if [ -n "$found" ]; then
    echo "--fail-under-line 90"
  fi
}
