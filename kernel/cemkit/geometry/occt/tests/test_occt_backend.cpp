#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/core/quantity.hpp"
#include "cemkit/core/sha256.hpp"
#include "cemkit/geometry/occt/occt_backend.hpp"
#include "cemkit/geometry/port/backend.hpp"

namespace port = cemkit::geometry::port;
namespace si = mp_units::si;
namespace isq = mp_units::isq;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using cemkit::core::ErrorCode;
using cemkit::geometry::occt::OcctBackend;

namespace {

constexpr auto k_mm = si::milli<si::metre>;

cemkit::core::Length length_mm(double value) { return value * isq::length[k_mm]; }
cemkit::core::Angle angle_rad(double value) { return value * isq::angular_measure[si::radian]; }

// Arbitrary test parameters (not a fan design and not sourced data): a 30 mm diameter, 20 mm long
// hub and a plate from r = 10 mm (inside the hub) to r = 50 mm, lofted through 3 sections whose
// chord, thickness and stagger all change, so the loft surfaces are genuinely curved.
port::TestSolidParams test_params() {
  return port::TestSolidParams{.hub_radius = length_mm(15.0),
                               .hub_length = length_mm(20.0),
                               .sections = {
                                   {.radius = length_mm(10.0),
                                    .chord = length_mm(18.0),
                                    .thickness = length_mm(2.0),
                                    .stagger = angle_rad(0.3)},
                                   {.radius = length_mm(30.0),
                                    .chord = length_mm(15.0),
                                    .thickness = length_mm(1.5),
                                    .stagger = angle_rad(0.5)},
                                   {.radius = length_mm(50.0),
                                    .chord = length_mm(12.0),
                                    .thickness = length_mm(1.2),
                                    .stagger = angle_rad(0.7)},
                               }};
}

port::Tessellation test_tessellation() {
  // Arbitrary test tolerances: 0.05 mm chordal deviation, 0.2 rad between neighbouring normals.
  return port::Tessellation{.linear_deflection = length_mm(0.05),
                            .angular_deflection = angle_rad(0.2)};
}

double in_mm3(const cemkit::core::Volume& v) { return v.numerical_value_in(mp_units::cubic(k_mm)); }
double in_mm2(const cemkit::core::Area& a) { return a.numerical_value_in(mp_units::square(k_mm)); }
double in_mm(const cemkit::core::Length& l) { return l.numerical_value_in(k_mm); }

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_{std::filesystem::temp_directory_path() /
              (std::string{name} + "_" + std::to_string(::getpid()))} {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  // operator<< on the rdbuf rather than istreambuf_iterator: GCC 13 at -O2 reports a false
  // -Wnull-dereference inside the iterator's inlined streambuf calls.
  std::ostringstream bytes;
  bytes << in.rdbuf();
  return std::move(bytes).str();
}

class ForeignSolid final : public port::Solid {
 public:
  [[nodiscard]] std::string_view backend() const noexcept override { return "other"; }
};

}  // namespace

TEST_CASE("the test solid is one valid solid: hub fused with a lofted plate",
          "[GEO-001][GEO-002]") {
  const OcctBackend backend;
  CHECK(backend.name() == "occt");
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());
  CHECK((*solid)->backend() == "occt");

  const auto report = backend.check_validity(**solid);
  REQUIRE(report.has_value());
  CHECK(report->brep_valid);
  CHECK(report->closed);
  CHECK(report->manifold);
  CHECK(report->self_intersection_free);
  CHECK(report->free_edges == 0);
  CHECK(report->non_manifold_edges == 0);

  const auto topology = backend.topology(**solid);
  REQUIRE(topology.has_value());
  CHECK(topology->solids == 1);
  CHECK(topology->shells == 1);
  // Hub: side, top and bottom (the seam is placed away from the plate). Plate: four lofted sides
  // and the tip cap; its root cap lies inside the hub and disappears in the fuse.
  CHECK(topology->faces == 8);
}

TEST_CASE("mass properties of the test solid are bounded by its parts", "[GEO-001]") {
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());
  const auto props = backend.mass_properties(**solid);
  REQUIRE(props.has_value());

  // Hub alone: V = pi r^2 L, A = 2 pi r L + 2 pi r^2 (r = 15 mm, L = 20 mm).
  const double hub_volume = std::numbers::pi * 15.0 * 15.0 * 20.0;
  const double hub_area =
      (2.0 * std::numbers::pi * 15.0 * 20.0) + (2.0 * std::numbers::pi * 15.0 * 15.0);
  // The plate adds less than its bounding slab outside the hub: 40 mm span x 18 mm x 2 mm.
  const double plate_bound = (50.0 - 10.0) * 18.0 * 2.0;
  CHECK(in_mm3(props->volume) > hub_volume);
  CHECK(in_mm3(props->volume) < hub_volume + plate_bound);
  CHECK(in_mm2(props->area) > hub_area);
  // The plate lies on the +x side: the centroid moves towards +x.
  CHECK(in_mm(props->centroid.at(0)) > 0.0);
  CHECK(std::abs(in_mm(props->centroid.at(2))) < 20.0 / 2.0);
}

TEST_CASE("the same parameters give identical volume, area and topology", "[GEO-001]") {
  const OcctBackend first_backend;
  const OcctBackend second_backend;
  const auto first = first_backend.build_test_solid(test_params());
  const auto second = second_backend.build_test_solid(test_params());
  const auto third = first_backend.build_test_solid(test_params());
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(third.has_value());

  const auto reference_props = first_backend.mass_properties(**first);
  const auto reference_topology = first_backend.topology(**first);
  REQUIRE(reference_props.has_value());
  REQUIRE(reference_topology.has_value());
  for (const auto* solid : {second->get(), third->get()}) {
    const auto props = first_backend.mass_properties(*solid);
    const auto topology = first_backend.topology(*solid);
    REQUIRE(props.has_value());
    REQUIRE(topology.has_value());
    // Exact equality, not a tolerance: GEO-001 asks for the same numbers.
    CHECK(props->volume == reference_props->volume);
    CHECK(props->area == reference_props->area);
    CHECK(props->centroid == reference_props->centroid);
    CHECK(*topology == *reference_topology);
  }
}

TEST_CASE("invalid parameters produce geometry_failed with a reason", "[GEO-002]") {
  const OcctBackend backend;
  const auto expect_failure = [&](const port::TestSolidParams& params, std::string_view subject,
                                  std::string_view reason) {
    const auto solid = backend.build_test_solid(params);
    REQUIRE_FALSE(solid.has_value());
    CHECK(solid.error().code() == ErrorCode::geometry_failed);
    CHECK(solid.error().subject() == subject);
    CHECK_THAT(solid.error().message(), ContainsSubstring(std::string{reason}));
  };

  auto params = test_params();
  params.hub_radius = length_mm(-1.0);
  expect_failure(params, "hub_radius", "must be positive");

  params = test_params();
  params.hub_length = length_mm(0.0);
  expect_failure(params, "hub_length", "must be positive");

  params = test_params();
  params.sections.at(1).chord = length_mm(std::numeric_limits<double>::quiet_NaN());
  expect_failure(params, "sections[1].chord", "must be finite");

  params = test_params();
  params.sections.at(2).thickness = length_mm(0.0);
  expect_failure(params, "sections[2].thickness", "must be positive");

  params = test_params();
  params.sections.at(0).stagger = angle_rad(std::numeric_limits<double>::infinity());
  expect_failure(params, "sections[0].stagger", "must be finite");

  params = test_params();
  params.sections.resize(1);
  expect_failure(params, "sections", "at least 2 sections");

  params = test_params();
  params.sections.at(2).radius = length_mm(30.0);
  expect_failure(params, "sections[2].radius", "strictly increasing");
}

TEST_CASE("a plate that does not meet the hub is rejected: the result is not one solid",
          "[GEO-002]") {
  auto params = test_params();
  params.sections.at(0).radius = length_mm(20.0);  // root outside the 15 mm hub
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(params);
  REQUIRE_FALSE(solid.has_value());
  CHECK(solid.error().code() == ErrorCode::geometry_failed);
  CHECK(solid.error().subject() == "test_solid");
  CHECK_THAT(solid.error().message(), ContainsSubstring("not one solid"));
  CHECK(solid.error().details().at("solids") == "2");
}

TEST_CASE("a solid from another backend is rejected", "[GEO-002]") {
  const OcctBackend backend;
  const ForeignSolid foreign;
  const auto report = backend.check_validity(foreign);
  REQUIRE_FALSE(report.has_value());
  CHECK(report.error().code() == ErrorCode::invalid_input);
  CHECK(report.error().details().at("backend") == "other");
  CHECK_FALSE(backend.topology(foreign).has_value());
  CHECK_FALSE(backend.mass_properties(foreign).has_value());
  CHECK_FALSE(backend.export_bytes(foreign, port::ExportRequest{}).has_value());
}

TEST_CASE("STEP export is deterministic and carries a fixed time stamp", "[GEO-004][GEO-001]") {
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());
  const port::ExportRequest request{.format = port::ExportFormat::step,
                                    .tessellation = std::nullopt};
  const auto first = backend.export_bytes(**solid, request);
  REQUIRE(first.has_value());
  CHECK(first->starts_with("ISO-10303-21;"));
  CHECK_THAT(*first, ContainsSubstring("1970-01-01T00:00:00"));
  CHECK_THAT(*first, ContainsSubstring("MANIFOLD_SOLID_BREP"));
  CHECK_THAT(*first, ContainsSubstring("PRODUCT('cemkit solid','cemkit solid'"));

  const auto other_solid = OcctBackend{}.build_test_solid(test_params());
  REQUIRE(other_solid.has_value());
  const auto second = backend.export_bytes(**other_solid, request);
  REQUIRE(second.has_value());
  CHECK(*second == *first);
}

TEST_CASE("STL export is binary, deterministic and leaves the solid unchanged",
          "[GEO-004][GEO-001]") {
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());
  const auto step_before = backend.export_bytes(**solid, port::ExportRequest{});
  const auto props_before = backend.mass_properties(**solid);
  REQUIRE(step_before.has_value());
  REQUIRE(props_before.has_value());

  const port::ExportRequest request{.format = port::ExportFormat::stl,
                                    .tessellation = test_tessellation()};
  const auto first = backend.export_bytes(**solid, request);
  const auto second = backend.export_bytes(**solid, request);
  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  CHECK(*first == *second);

  // Binary STL: 80-byte header, uint32 triangle count, 50 bytes per triangle.
  REQUIRE(first->size() > 84);
  std::uint32_t triangles = 0;
  std::memcpy(&triangles, first->data() + 80, sizeof triangles);
  CHECK(triangles > 0);
  CHECK(first->size() == 84 + (50 * std::size_t{triangles}));

  // Meshing for STL works on a copy: the solid itself is unchanged.
  const auto step_after = backend.export_bytes(**solid, port::ExportRequest{});
  const auto props_after = backend.mass_properties(**solid);
  REQUIRE(step_after.has_value());
  REQUIRE(props_after.has_value());
  CHECK(*step_after == *step_before);
  CHECK(props_after->volume == props_before->volume);
}

TEST_CASE("STL export needs a valid tessellation", "[GEO-004]") {
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());

  const auto missing = backend.export_bytes(
      **solid,
      port::ExportRequest{.format = port::ExportFormat::stl, .tessellation = std::nullopt});
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().code() == ErrorCode::invalid_input);
  CHECK(missing.error().subject() == "tessellation");

  auto tessellation = test_tessellation();
  tessellation.linear_deflection = length_mm(0.0);
  const auto zero = backend.export_bytes(
      **solid,
      port::ExportRequest{.format = port::ExportFormat::stl, .tessellation = tessellation});
  REQUIRE_FALSE(zero.has_value());
  CHECK(zero.error().code() == ErrorCode::invalid_input);
  CHECK(zero.error().subject() == "tessellation.linear_deflection");

  tessellation = test_tessellation();
  tessellation.angular_deflection = angle_rad(std::numeric_limits<double>::quiet_NaN());
  const auto nan = backend.export_bytes(
      **solid,
      port::ExportRequest{.format = port::ExportFormat::stl, .tessellation = tessellation});
  REQUIRE_FALSE(nan.has_value());
  CHECK(nan.error().subject() == "tessellation.angular_deflection");
}

TEST_CASE("STEP and STL files are named by the sha256 of their content", "[GEO-004]") {
  const TempDir dir{"cemkit_geometry_occt_export"};
  const OcctBackend backend;
  const auto solid = backend.build_test_solid(test_params());
  REQUIRE(solid.has_value());

  const auto step = port::export_to_directory(
      backend, **solid,
      port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt},
      dir.path());
  REQUIRE(step.has_value());
  CHECK(step->path.extension() == ".step");
  CHECK(step->path.stem() == step->sha256);
  CHECK(cemkit::core::sha256_hex(read_file(step->path)) == step->sha256);

  const auto stl = port::export_to_directory(
      backend, **solid,
      port::ExportRequest{.format = port::ExportFormat::stl, .tessellation = test_tessellation()},
      dir.path());
  REQUIRE(stl.has_value());
  CHECK(stl->path.extension() == ".stl");
  CHECK(cemkit::core::sha256_hex(read_file(stl->path)) == stl->sha256);

  // A rebuilt solid exports to the same names.
  const auto rebuilt = backend.build_test_solid(test_params());
  REQUIRE(rebuilt.has_value());
  const auto step_again = port::export_to_directory(
      backend, **rebuilt,
      port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt},
      dir.path());
  REQUIRE(step_again.has_value());
  CHECK(step_again->path == step->path);
}
