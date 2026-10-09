add_executable(fancem_tests
  test_toolchain.cpp
  test_version.cpp)
target_compile_definitions(fancem_tests PRIVATE FANCEM_EXPECTED_VERSION="${PROJECT_VERSION}")
target_link_libraries(fancem_tests
  PRIVATE fancem Catch2::Catch2WithMain fancem_warnings fancem_instrumentation)

include(Catch)
catch_discover_tests(fancem_tests)

# --- Negative tests: the quality gates must prove they work (MAINT-001) -----------------------------

# An unused variable must fail to compile under the project flags. The target is excluded from "all";
# the ctest entry builds it and passes only if the build fails with the unused-variable diagnostic.
add_executable(neg_unused_variable EXCLUDE_FROM_ALL negative/unused_variable.cpp)
target_link_libraries(neg_unused_variable PRIVATE fancem_warnings)
add_test(NAME negative_unused_variable_fails_to_compile
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target neg_unused_variable)
set_tests_properties(negative_unused_variable_fails_to_compile PROPERTIES
  PASS_REGULAR_EXPRESSION "unused-variable")

# Under the sanitizer preset a deliberate heap-buffer overflow must be caught.
if(FANCEM_ENABLE_SANITIZERS)
  add_executable(neg_heap_overflow negative/heap_overflow.cpp)
  target_link_libraries(neg_heap_overflow PRIVATE fancem_warnings fancem_instrumentation)
  add_test(NAME negative_heap_overflow_is_caught COMMAND neg_heap_overflow)
  set_tests_properties(negative_heap_overflow_is_caught PROPERTIES
    PASS_REGULAR_EXPRESSION "heap-buffer-overflow"
    # Exit normally (not by signal) so ctest evaluates the regular expression instead of flagging a crash.
    ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:symbolize=1")
endif()
