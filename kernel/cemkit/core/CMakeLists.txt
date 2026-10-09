cemkit_add_module(
  PATH cemkit/core
  SOURCES error.cpp version.cpp
  DEPS mp-units::mp-units
  DEFINES CEMKIT_KERNEL_VERSION="${PROJECT_VERSION}"
  TESTS
    tests/test_error.cpp
    tests/test_fidelity.cpp
    tests/test_labelled.cpp
    tests/test_provenance.cpp
    tests/test_units.cpp
    tests/test_version.cpp
  TEST_DEFINES CEMKIT_EXPECTED_VERSION="${PROJECT_VERSION}")

# REL-002: the error-code table is the single source of docs/models/platform/error-codes.md and
# schemas/cemkit/v1/error-codes.json. This tool prints it as JSON; the drift test fails if the
# committed files differ from what scripts/gen_error_codes.py renders from it.
add_executable(cemkit_core_print_error_codes tools/print_error_codes.cpp)
target_link_libraries(cemkit_core_print_error_codes PRIVATE
  cemkit_core nlohmann_json::nlohmann_json cemkit_warnings cemkit_instrumentation)

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
add_test(NAME error_codes_generated_files_are_current
  COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/gen_error_codes.py --check
    --root ${PROJECT_SOURCE_DIR} --tool $<TARGET_FILE:cemkit_core_print_error_codes>)
set_tests_properties(error_codes_generated_files_are_current PROPERTIES LABELS "cemkit/core;REL-002")
