# fans.axial_ducted (T14): minimal family registration. Essential fields (ADR-003 D8) and the T08
# feasibility hooks only; every other Family operation returns not_implemented until the axial L1
# milestone. Its build file names nothing outside the folder except fans/common (FAM-002).
cemkit_add_family(
  SOURCES axial_ducted.cpp
  DEPS cemkit_fans_common
  TESTS tests/test_axial_ducted.cpp
  TEST_DEPS cemkit_spec
)
