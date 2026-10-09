cemkit_add_module(
  PATH cemkit/core
  SOURCES version.cpp
  DEPS mp-units::mp-units
  DEFINES CEMKIT_KERNEL_VERSION="${PROJECT_VERSION}"
  TESTS tests/test_version.cpp
  TEST_DEFINES CEMKIT_EXPECTED_VERSION="${PROJECT_VERSION}")
