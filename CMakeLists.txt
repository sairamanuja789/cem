cmake_minimum_required(VERSION 3.28)

project(fancem VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FANCEM_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (clang-asan preset)" OFF)
option(FANCEM_ENABLE_COVERAGE "Build with --coverage for gcovr (gcc-coverage preset)" OFF)

# Project warning flags (ADR-007). Applied to project targets only, through this interface target;
# third-party headers arrive through IMPORTED targets and are therefore SYSTEM includes.
add_library(fancem_warnings INTERFACE)
target_compile_options(fancem_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
  -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough)

add_library(fancem_instrumentation INTERFACE)
if(FANCEM_ENABLE_SANITIZERS)
  target_compile_options(fancem_instrumentation INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(fancem_instrumentation INTERFACE -fsanitize=address,undefined)
endif()
if(FANCEM_ENABLE_COVERAGE)
  target_compile_options(fancem_instrumentation INTERFACE --coverage -O0 -g)
  target_link_options(fancem_instrumentation INTERFACE --coverage)
endif()

find_package(mp-units CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

enable_testing()
add_subdirectory(kernel)
