# Fan product domain (ADR-009): fan physics in common/, families in families/<name>/ (added by
# folder discovery), and the product's registration function in register.cpp.
add_subdirectory(common)

cemkit_add_families(fan_family_targets)
cemkit_add_module(
  PATH products/fans
  SOURCES register.cpp
  DEPS cemkit_product ${fan_family_targets}
  TESTS tests/test_register.cpp
)
