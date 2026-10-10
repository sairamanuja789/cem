# Family plugin interface, registry and parameter spaces (FAM-001 to FAM-003, SPEC-006; ADR-009,
# ADR-012). Platform module: it never names a product.
cemkit_add_module(
  PATH cemkit/product
  SOURCES parameter_space.cpp registry.cpp
  DEPS cemkit_core cemkit_spec
  TESTS
    tests/test_parameter_space.cpp
    tests/test_registry.cpp
)
