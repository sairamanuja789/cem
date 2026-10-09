# OpenCascade adapter (ADR-004): the only module that links OCCT. The toolkits are the ones built by
# the overlay port in cmake/vcpkg-ports/opencascade; OCCT's CMake package adds their dependencies.
cemkit_add_module(
  PATH cemkit/geometry/occt
  SOURCES occt_backend.cpp shape_checks.cpp
  DEPS cemkit_geometry_port TKPrim TKOffset TKBO TKBool TKMesh TKDESTEP TKDESTL
  TESTS
    tests/test_guard.cpp
    tests/test_occt_backend.cpp
    tests/test_shape_checks.cpp)
