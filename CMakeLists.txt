cmake_minimum_required(VERSION 3.28)

project(cemkit VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CEMKIT_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (clang-asan preset)" OFF)
option(CEMKIT_ENABLE_COVERAGE "Build with --coverage for gcovr (gcc-coverage preset)" OFF)

find_package(mp-units CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_package(OpenCASCADE CONFIG REQUIRED)  # ADR-004: cmake/vcpkg-ports/opencascade

enable_testing()
add_subdirectory(kernel)
