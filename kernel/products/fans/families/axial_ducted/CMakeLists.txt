# fans.axial_ducted (T14): minimal family registration. Essential fields (ADR-003 D8) and the T08
# feasibility hooks only; every other Family operation returns not_implemented until the axial L1
# milestone. Its build file names nothing outside the folder except fans/common (FAM-002).
cemkit_add_family(
  SOURCES axial_ducted.cpp cfd_case.cpp free_vortex.cpp geometry.cpp mean_line.cpp operations.cpp
          vortex.cpp
  DEPS cemkit_fans_common cemkit_geometry_port cemkit_simulation_port
  TESTS tests/test_axial_ducted.cpp tests/test_spec_compiler.cpp tests/test_mean_line.cpp tests/test_free_vortex.cpp tests/test_vortex.cpp
        tests/test_cfd_metrics.cpp
  TEST_DEPS cemkit_spec
)
