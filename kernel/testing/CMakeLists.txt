# Cross-cutting kernel tests that belong to no single module (ADR-009 "testing/").

# Toolchain tests (MAINT-001): every preset must compile std::expected, mp-units and nlohmann-json.
add_executable(cemkit_testing_toolchain_tests toolchain/test_toolchain.cpp)
target_link_libraries(cemkit_testing_toolchain_tests PRIVATE
  mp-units::mp-units nlohmann_json::nlohmann_json Catch2::Catch2WithMain
  cemkit_warnings cemkit_instrumentation)
catch_discover_tests(cemkit_testing_toolchain_tests PROPERTIES LABELS "testing/toolchain")

# --- Compile-fail tests (COR-001, COR-003): misuse of units and labelled values must not compile ----
foreach(case pressure_plus_flow_rate length_assigned_to_pressure raw_double_as_pressure
             torque_as_energy absolute_pressure_plus_absolute_pressure angle_as_ratio)
  cemkit_add_compile_fail_test(NAME ${case} SOURCE compile_fail/${case}.cpp DEPS cemkit_core
    LABEL COR-001)
endforeach()
foreach(case labelled_without_fidelity labelled_arithmetic l2_verified_without_uncertainty)
  cemkit_add_compile_fail_test(NAME ${case} SOURCE compile_fail/${case}.cpp DEPS cemkit_core
    LABEL COR-003)
endforeach()

# --- Gate tests: the quality gates must prove they work (MAINT-001) -------------------------------

# An unused variable must fail to compile under the project flags. The target is excluded from "all";
# the ctest entry builds it and passes only if the build fails with the unused-variable diagnostic.
add_executable(neg_unused_variable EXCLUDE_FROM_ALL gates/unused_variable.cpp)
target_link_libraries(neg_unused_variable PRIVATE cemkit_warnings)
add_test(NAME negative_unused_variable_fails_to_compile
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target neg_unused_variable)
set_tests_properties(negative_unused_variable_fails_to_compile PROPERTIES
  PASS_REGULAR_EXPRESSION "unused-variable" LABELS "testing/gates" RESOURCE_LOCK cemkit_build_tree)

# Under the sanitizer preset a deliberate heap-buffer overflow must be caught.
if(CEMKIT_ENABLE_SANITIZERS)
  add_executable(neg_heap_overflow gates/heap_overflow.cpp)
  target_link_libraries(neg_heap_overflow PRIVATE cemkit_warnings cemkit_instrumentation)
  add_test(NAME negative_heap_overflow_is_caught COMMAND neg_heap_overflow)
  set_tests_properties(negative_heap_overflow_is_caught PROPERTIES
    PASS_REGULAR_EXPRESSION "heap-buffer-overflow"
    LABELS "testing/gates"
    # Exit normally (not by signal) so ctest evaluates the regular expression instead of flagging a crash.
    ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:symbolize=1")
endif()

# --- Family plugins (FAM-002, UC-09) ------------------------------------------------------------
# The stub product and its stub family: a product laid out like kernel/products/<product>/.
add_subdirectory(stub_product)

# Every folder under products/*/families/ (and testing/*/families/) must be in the registry built by
# the products' registration functions. KERNEL_ROOT tells the test where to look.
add_executable(cemkit_testing_family_registry_tests family_registry/test_family_folders.cpp)
target_link_libraries(cemkit_testing_family_registry_tests PRIVATE
  cemkit_fans cemkit_testing_stub_product Catch2::Catch2WithMain
  cemkit_warnings cemkit_instrumentation)
target_compile_definitions(cemkit_testing_family_registry_tests PRIVATE
  CEMKIT_KERNEL_ROOT="${CEMKIT_KERNEL_ROOT}")
catch_discover_tests(cemkit_testing_family_registry_tests
  PROPERTIES LABELS "testing/family_registry;FAM-002")
