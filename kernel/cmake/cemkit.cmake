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

# cemkit_add_compile_fail_test(NAME <name> SOURCE <file> DEPS <targets...> LABEL <requirement ID>)
# A negative compile test with a paired control (COR-001, COR-003). <file> must build cleanly as
# written and must fail to build with CEMKIT_EXPECT_COMPILE_ERROR defined. Two ctest entries:
# compile_fail.<name>.control (build must succeed) and compile_fail.<name>.rejected (build must
# fail). Compiler message text is never matched: the control build is what shows the failure comes
# from the guarded line and not from a broken file. Both targets are outside "all"; the tests share a
# lock because they drive the same build tree.
function(cemkit_add_compile_fail_test)
  cmake_parse_arguments(C "" "NAME;SOURCE;LABEL" "DEPS" ${ARGN})
  foreach(variant control rejected)
    set(target compile_fail_${C_NAME}_${variant})
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${C_SOURCE})
    target_link_libraries(${target} PRIVATE ${C_DEPS} cemkit_warnings)
    if(variant STREQUAL "rejected")
      target_compile_definitions(${target} PRIVATE CEMKIT_EXPECT_COMPILE_ERROR)
    endif()
    add_test(NAME compile_fail.${C_NAME}.${variant}
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target})
    set_tests_properties(compile_fail.${C_NAME}.${variant} PROPERTIES
      LABELS "${C_LABEL};compile_fail" RESOURCE_LOCK cemkit_build_tree)
  endforeach()
  set_tests_properties(compile_fail.${C_NAME}.rejected PROPERTIES WILL_FAIL TRUE)
endfunction()

# Writes <build>/cemkit_modules.txt ("path=target" per line, sorted) for scripts/check_boundaries.py.
function(cemkit_write_module_manifest)
  get_property(modules GLOBAL PROPERTY CEMKIT_MODULES)
  list(SORT modules)
  list(JOIN modules "\n" text)
  file(WRITE "${CMAKE_BINARY_DIR}/cemkit_modules.txt" "${text}\n")
endfunction()

# cemkit_add_families(<out_var>): adds every family folder of the current product
# (<product>/families/<family>/ with a CMakeLists.txt), in sorted folder order, and returns their
# module targets in <out_var> for the product's registration module to link (ADR-009, ADR-012).
# Build discovery only: registration stays explicit, one call per family in the product's
# register.cpp. A new family folder therefore needs no CMake edit outside itself (FAM-002).
function(cemkit_add_families out)
  file(GLOB lists CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/families/*/CMakeLists.txt")
  list(SORT lists)
  set(targets "")
  foreach(list_file IN LISTS lists)
    get_filename_component(dir "${list_file}" DIRECTORY)
    file(RELATIVE_PATH rel "${CEMKIT_KERNEL_ROOT}" "${dir}")
    add_subdirectory("${dir}")
    cemkit_module_target(target "${rel}")
    list(APPEND targets ${target})
  endforeach()
  set(${out} "${targets}" PARENT_SCOPE)
endfunction()

# cemkit_add_family([SOURCES ...] [DEPS ...] [TESTS ...]): a family folder's whole CMakeLists.txt.
# The module path is the folder's own path under kernel/, so a family folder can be copied or renamed
# without editing its build file.
function(cemkit_add_family)
  cmake_parse_arguments(F "" "" "SOURCES;DEPS;TESTS;TEST_DEPS" ${ARGN})
  file(RELATIVE_PATH rel "${CEMKIT_KERNEL_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}")
  cemkit_add_module(PATH "${rel}" SOURCES ${F_SOURCES} DEPS cemkit_product ${F_DEPS}
    TESTS ${F_TESTS} TEST_DEPS ${F_TEST_DEPS})
endfunction()
