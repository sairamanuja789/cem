cemkit_add_module(
  PATH cemkit/spec
  SOURCES units.cpp spec.cpp compiler.cpp
  DEPS cemkit_core nlohmann_json::nlohmann_json
  TESTS
    tests/test_units.cpp
    tests/test_compiler.cpp
)
