cemkit_add_module(
  PATH products/fans/common
  SOURCES l0.cpp feasibility.cpp
  DEPS cemkit_core cemkit_physics
  TESTS
    tests/test_pressure.cpp
    tests/test_l0.cpp
    tests/test_feasibility.cpp)

foreach(case static_passed_as_total total_plus_static generic_pressure_as_total)
  cemkit_add_compile_fail_test(
    NAME fans_${case}
    SOURCE tests/compile_fail/${case}.cpp
    DEPS cemkit_fans_common
    LABEL COR-001)
endforeach()
