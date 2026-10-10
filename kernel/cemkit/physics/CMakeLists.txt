cemkit_add_module(
  PATH cemkit/physics
  SOURCES validity.cpp air.cpp
  DEPS cemkit_core
  TESTS
    tests/test_validity.cpp
    tests/test_air.cpp
)
