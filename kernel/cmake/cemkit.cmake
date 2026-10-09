# cemkit kernel build helpers (ADR-007 warnings, ADR-009 module structure).

# Project warning flags (ADR-007). Applied to project targets only, through this interface target;
# third-party headers arrive through IMPORTED targets and are therefore SYSTEM includes.
add_library(cemkit_warnings INTERFACE)
target_compile_options(cemkit_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
  -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)

add_library(cemkit_instrumentation INTERFACE)
if(CEMKIT_ENABLE_SANITIZERS)
  target_compile_options(cemkit_instrumentation INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(cemkit_instrumentation INTERFACE -fsanitize=address,undefined)
endif()
if(CEMKIT_ENABLE_COVERAGE)
  target_compile_options(cemkit_instrumentation INTERFACE --coverage -O0 -g)
  target_link_options(cemkit_instrumentation INTERFACE --coverage)
endif()

include(Catch)

# Target name for a module path relative to kernel/: drop a leading "cemkit/" or "products/",
# replace "/" with "_", prefix "cemkit_". cemkit/core -> cemkit_core,
# products/fans/common -> cemkit_fans_common. scripts/check_boundaries.py applies the same rule.
function(cemkit_module_target out path)
  string(REGEX REPLACE "^(cemkit|products)/" "" rest "${path}")
  string(REPLACE "/" "_" rest "${rest}")
  set(${out} "cemkit_${rest}" PARENT_SCOPE)
endfunction()

# cemkit_add_module(PATH <path under kernel/> [SOURCES ...] [DEPS ...] [DEFINES ...]
#                   [TESTS ...] [TEST_DEPS ...] [TEST_DEFINES ...])
# One CMake target per module (ADR-009). DEPS are the module's public dependencies; they are what
# the boundary check reads from the link graph. Module tests live in <module>/tests/.
function(cemkit_add_module)
  cmake_parse_arguments(M "" "PATH" "SOURCES;DEPS;DEFINES;TESTS;TEST_DEPS;TEST_DEFINES" ${ARGN})
  if(NOT M_PATH)
    message(FATAL_ERROR "cemkit_add_module: PATH is required")
  endif()
  cemkit_module_target(target "${M_PATH}")

  if(M_SOURCES)
    add_library(${target} STATIC ${M_SOURCES})
    target_include_directories(${target} PUBLIC ${CEMKIT_KERNEL_ROOT})
    target_link_libraries(${target} PUBLIC ${M_DEPS} PRIVATE cemkit_warnings cemkit_instrumentation)
    target_compile_definitions(${target} PRIVATE ${M_DEFINES})
  else()
    add_library(${target} INTERFACE)
    target_include_directories(${target} INTERFACE ${CEMKIT_KERNEL_ROOT})
    target_link_libraries(${target} INTERFACE ${M_DEPS})
  endif()
  set_property(GLOBAL APPEND PROPERTY CEMKIT_MODULES "${M_PATH}=${target}")

  if(M_TESTS)
    add_executable(${target}_tests ${M_TESTS})
    target_link_libraries(${target}_tests
      PRIVATE ${target} ${M_TEST_DEPS} Catch2::Catch2WithMain cemkit_warnings cemkit_instrumentation)
    target_compile_definitions(${target}_tests PRIVATE ${M_TEST_DEFINES})
    catch_discover_tests(${target}_tests PROPERTIES LABELS "${M_PATH}")
  endif()
endfunction()

# Writes <build>/cemkit_modules.txt ("path=target" per line, sorted) for scripts/check_boundaries.py.
function(cemkit_write_module_manifest)
  get_property(modules GLOBAL PROPERTY CEMKIT_MODULES)
  list(SORT modules)
  list(JOIN modules "\n" text)
  file(WRITE "${CMAKE_BINARY_DIR}/cemkit_modules.txt" "${text}\n")
endfunction()
