# A minimal product used only by tests (FAM-002, UC-09). It is laid out exactly like a real product
# (kernel/products/<product>/): families/<family>/ folders found by cemkit_add_families() and one
# register.cpp. Nothing outside kernel/testing names it.
cemkit_add_families(stub_family_targets)
cemkit_add_module(
  PATH testing/stub_product
  SOURCES register.cpp
  DEPS cemkit_product ${stub_family_targets}
  TESTS tests/test_stub_product.cpp
  TEST_DEPS cemkit_spec
)
