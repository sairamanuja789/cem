add_library(fancem STATIC
  src/core/version.cpp)
target_include_directories(fancem PUBLIC include)
target_compile_definitions(fancem PRIVATE FANCEM_KERNEL_VERSION="${PROJECT_VERSION}")
target_link_libraries(fancem
  PUBLIC mp-units::mp-units nlohmann_json::nlohmann_json
  PRIVATE fancem_warnings fancem_instrumentation)

add_subdirectory(tests)
