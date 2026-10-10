set(CEMKIT_KERNEL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/cemkit.cmake)

# Platform modules (ADR-009). Products are added under products/ as they arrive.
add_subdirectory(cemkit/core)
add_subdirectory(cemkit/spec)
add_subdirectory(cemkit/physics)
add_subdirectory(cemkit/product)

# Product domains.
add_subdirectory(products/fans)

add_subdirectory(testing)

cemkit_write_module_manifest()
