# nanobind module cemkit._kernel (T11; PERF-002; ADR-012): a thin wrapper over the C ABI
# (kernel/cemkit/capi/cemkit.h). No logic: each function passes a JSON string in and returns the
# status code and the JSON string out; python/cemkit/kernel.py decodes them.
find_package(Python 3.13 REQUIRED COMPONENTS Interpreter Development.Module)
if(NOT SKBUILD)
  # Preset build: nanobind comes from the project venv (dev group, locked in uv.lock).
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
                  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT
                  COMMAND_ERROR_IS_FATAL ANY)
endif()
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_kernel NB_STATIC module.cpp)
target_link_libraries(_kernel PRIVATE cemkit_capi)
install(TARGETS _kernel LIBRARY DESTINATION cemkit COMPONENT python)

if(NOT SKBUILD)
  # In-place copy for the tests (gitignored): python/cemkit/_kernel.<abi>.so.
  add_custom_command(TARGET _kernel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:_kernel>
            ${PROJECT_SOURCE_DIR}/python/cemkit/$<TARGET_FILE_NAME:_kernel>
    COMMENT "Copying cemkit._kernel into python/cemkit/")
endif()
