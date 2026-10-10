set(CEMKIT_KERNEL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(cmake/cemkit.cmake)

# Platform modules (ADR-009). Products are added under products/ as they arrive.
add_subdirectory(cemkit/core)
add_subdirectory(cemkit/spec)
add_subdirectory(cemkit/physics)
add_subdirectory(cemkit/product)
add_subdirectory(cemkit/geometry/port)
add_subdirectory(cemkit/geometry/occt)
add_subdirectory(cemkit/capi)

# Product domains: every kernel/products/<product>/, and the generated register_products() entry
# point the C ABI calls (ADR-015 rule 5).
cemkit_add_products()

add_subdirectory(testing)

cemkit_write_module_manifest()
