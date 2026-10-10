# The stub family (FAM-002): a self-contained plugin folder. Its build file names nothing outside
# the folder, so it builds wherever the folder is placed under a product's families/.
cemkit_add_family(
  SOURCES stub_family.cpp
  TESTS tests/test_stub_family.cpp
  TEST_DEPS cemkit_spec
)
