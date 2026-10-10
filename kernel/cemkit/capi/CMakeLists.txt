# C ABI (T11; MAINT-005, PERF-002; ADR-013): JSON in and out, status codes, cemkit_free. The only
# module that may depend on every other module (ADR-009).
cemkit_add_module(
  PATH cemkit/capi
  SOURCES cemkit.cpp detail/handlers.cpp
  DEPS cemkit_core cemkit_spec cemkit_physics cemkit_product cemkit_fans_common cemkit_fans
       cemkit_geometry_occt
       nlohmann_json::nlohmann_json
  TESTS tests/test_capi.cpp)

# cemkit.h compiles as C11 and works from C.
enable_language(C)
add_executable(cemkit_capi_c_header_check tests/c_header_check.c)
set_target_properties(cemkit_capi_c_header_check PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
target_compile_options(cemkit_capi_c_header_check PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(cemkit_capi_c_header_check PRIVATE cemkit_capi cemkit_instrumentation)
add_test(NAME capi_c_header_check COMMAND cemkit_capi_c_header_check)
set_tests_properties(capi_c_header_check PROPERTIES LABELS "cemkit/capi;MAINT-005")

# PHY-005: the kernel's L0 models and feasibility gate against the Python reference, through
# cemkit_l0_batch, on every hand calculation and 10 000 seeded random in-range states plus 1 000
# out-of-range ones (scripts/crosscheck_l0.py; tests/python/test_kernel_bindings.py runs the same
# comparison through the Python binding).
add_executable(cemkit_l0_crosscheck tests/l0_crosscheck.cpp)
target_link_libraries(cemkit_l0_crosscheck PRIVATE cemkit_capi cemkit_warnings
  cemkit_instrumentation)

find_program(CEMKIT_UV uv REQUIRED)
add_test(NAME fans_l0_crosscheck
  COMMAND ${CEMKIT_UV} run --locked python scripts/crosscheck_l0.py
    --tool $<TARGET_FILE:cemkit_l0_crosscheck> --cases 10000
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(fans_l0_crosscheck PROPERTIES
  LABELS "cemkit/capi;PHY-005;SEL-001;SEL-004"
  ENVIRONMENT "PYTHONPATH=${PROJECT_SOURCE_DIR}/python")
