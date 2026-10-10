cemkit_add_module(
  PATH cemkit/simulation/openfoam
  SOURCES writer.cpp
  DEPS cemkit_core cemkit_simulation_port
  TESTS tests/test_writer.cpp)
