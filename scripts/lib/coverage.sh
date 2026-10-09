# Sourced by scripts/check.sh and tests/python/test_check_script.py (MAINT-003, spec v1.2).
# coverage_floor_args [root]: prints the gcovr floor arguments for that tree, or nothing when no covered
# module contains a .cpp file yet. Covered: kernel/cemkit/{core,spec,physics}, kernel/products/*/common
# and each family's L1 model (kernel/products/*/families/*/l1_model*); never tests/.
# Directories may be missing individually; a missing one must never switch the floor off while another
# covered module contains code.
coverage_floor_args() {
  local root="${1:-.}" found
  found="$(find "$root/kernel" -type f -name '*.cpp' -not -path '*/tests/*' \( \
      -path "$root/kernel/cemkit/core/*" -o -path "$root/kernel/cemkit/spec/*" \
      -o -path "$root/kernel/cemkit/physics/*" -o -path "$root/kernel/products/*/common/*" \
      -o -path "$root/kernel/products/*/families/*/l1_model*" \) 2>/dev/null | head -n 1)"
  if [ -n "$found" ]; then
    echo "--fail-under-line 90"
  fi
}
