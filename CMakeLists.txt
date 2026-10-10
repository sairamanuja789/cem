cmake_minimum_required(VERSION 3.28)

# scikit-build-core sets SKBUILD when it builds the Python module (pyproject.toml, ADR-013).
if(SKBUILD)
  include(cmake/python-build.cmake)
endif()

project(cemkit VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CEMKIT_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (clang-asan preset)" OFF)
option(CEMKIT_ENABLE_COVERAGE "Build with --coverage for gcovr (gcc-coverage preset)" OFF)
# ON for the wheel (scikit-build-core) and the "release" preset, which builds the module the tests
# import into python/cemkit/ (ADR-013).
option(CEMKIT_BUILD_PYTHON "Build the nanobind module cemkit._kernel (bindings/python)" OFF)
if(SKBUILD)
  set(CEMKIT_BUILD_PYTHON ON)
endif()

# The kernel's static libraries are linked into the Python extension module (a shared object).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(mp-units CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(OpenCASCADE CONFIG REQUIRED)  # ADR-004: cmake/vcpkg-ports/opencascade

enable_testing()
add_subdirectory(kernel)
if(CEMKIT_BUILD_PYTHON)
  add_subdirectory(bindings/python)
endif()
