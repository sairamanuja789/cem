cemkit_add_module(
  PATH products/fans/common
  SOURCES l0.cpp feasibility.cpp
  DEPS cemkit_core cemkit_physics
  TESTS
    tests/test_pressure.cpp
    tests/test_l0.cpp
    tests/test_feasibility.cpp)

foreach(case static_passed_as_total total_plus_static generic_pressure_as_total)
  cemkit_add_compile_fail_test(
    NAME fans_${case}
    SOURCE tests/compile_fail/${case}.cpp
    DEPS cemkit_fans_common
    LABEL COR-001)
endforeach()

# PHY-005: the kernel's L0 models and feasibility gate against the Python reference, on every
# verified hand calculation and 10 000 seeded random inputs (scripts/crosscheck_l0.py). The tool
# evaluates JSON cases with the kernel; the script runs the reference and compares (numbers to
# floating-point tolerance, error texts byte for byte). Same pattern as error_codes_generated_files_
# are_current in cemkit/core; the bindings (T11) do not exist yet.
add_executable(cemkit_fans_l0_crosscheck tests/l0_crosscheck.cpp)
target_link_libraries(cemkit_fans_l0_crosscheck PRIVATE
  cemkit_fans_common nlohmann_json::nlohmann_json cemkit_warnings cemkit_instrumentation)

find_program(CEMKIT_UV uv REQUIRED)
add_test(NAME fans_l0_crosscheck
  COMMAND ${CEMKIT_UV} run --locked python scripts/crosscheck_l0.py
    --tool $<TARGET_FILE:cemkit_fans_l0_crosscheck> --cases 10000
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(fans_l0_crosscheck PROPERTIES
  LABELS "products/fans/common;PHY-005;SEL-001;SEL-004")
