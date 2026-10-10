# Configuration for the Python wheel / editable build (scikit-build-core, T11, ADR-012). Included
# by the top-level CMakeLists.txt before project() when SKBUILD is set, so that the module is
# built with the same toolchain as the "release" preset: vcpkg (with the OCCT overlay port from
# vcpkg.json), GCC 13 chainloaded, -O2, ccache.
if(NOT DEFINED ENV{VCPKG_ROOT})
  message(FATAL_ERROR "cemkit Python build: VCPKG_ROOT is not set (build inside the dev container)")
endif()
set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/toolchains/gcc-13.cmake"
    CACHE FILEPATH "")
set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "")
set(VCPKG_INSTALL_OPTIONS
    "--x-buildtrees-root=${CMAKE_SOURCE_DIR}/.cache/vcpkg/buildtrees;--downloads-root=${CMAKE_SOURCE_DIR}/.cache/vcpkg/downloads;--x-packages-root=${CMAKE_SOURCE_DIR}/.cache/vcpkg/packages"
    CACHE STRING "")
set(CMAKE_CXX_COMPILER_LAUNCHER "ccache" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE "-O2" CACHE STRING "")
# CLAUDE.md: never more than 6 compile jobs on the 16 GB machine. Ninja ignores make-style -j
# limits from pip/uv, so the limit is a job pool; CEMKIT_JOBS lowers it further.
if(DEFINED ENV{CEMKIT_JOBS})
  set(CEMKIT_JOBS "$ENV{CEMKIT_JOBS}")
else()
  set(CEMKIT_JOBS 6)
endif()
set_property(GLOBAL PROPERTY JOB_POOLS cemkit_compile=${CEMKIT_JOBS} cemkit_link=2)
set(CMAKE_JOB_POOL_COMPILE cemkit_compile)
set(CMAKE_JOB_POOL_LINK cemkit_link)
