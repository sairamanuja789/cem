set(CEMKIT_KERNEL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/cemkit.cmake)

# Platform modules (ADR-009). Products are added under products/ as they arrive.
add_subdirectory(cemkit/core)

add_subdirectory(testing)

cemkit_write_module_manifest()
