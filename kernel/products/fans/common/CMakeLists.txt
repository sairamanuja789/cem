cemkit_add_module(
  PATH products/fans/common
  DEPS cemkit_core
  TESTS tests/test_pressure.cpp)

foreach(case static_passed_as_total total_plus_static generic_pressure_as_total)
  cemkit_add_compile_fail_test(
    NAME fans_${case}
    SOURCE tests/compile_fail/${case}.cpp
    DEPS cemkit_fans_common
    LABEL COR-001)
endforeach()
