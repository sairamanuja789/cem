set(CEMKIT_KERNEL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/cemkit.cmake)

# Platform modules (ADR-009). Products are added under products/ as they arrive.
add_subdirectory(cemkit/core)
add_subdirectory(cemkit/spec)
add_subdirectory(cemkit/physics)
add_subdirectory(cemkit/geometry/port)
add_subdirectory(cemkit/geometry/occt)
add_subdirectory(cemkit/product)

# Product domains.
add_subdirectory(products/fans)

# The C ABI depends on every module above (ADR-009).
add_subdirectory(cemkit/capi)

add_subdirectory(testing)

cemkit_write_module_manifest()
