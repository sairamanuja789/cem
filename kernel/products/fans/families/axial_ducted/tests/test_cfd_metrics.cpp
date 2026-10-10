// rotor_cfd_metrics (SIM-004, TRUST-001, TRUST-004): numbers only for a trusted result, with the
// stated definitions. Synthetic monitor values, worked by hand below.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cemkit/product/json_io.hpp"
#include "products/fans/families/axial_ducted/geometry.hpp"

using Catch::Matchers::WithinRel;
using cemkit::product::Json;

namespace {
Json args(const char* status) {
  return Json{{"args",
               {{"status", status},
                {"sectors", 7},
                {"density", 1.2},
                {"omega", 200.0},
                {"outlet_flow", 0.004},
                {"p_in", 0.5},
                {"p_out", 6.0},
                {"pt_in", 4.0},
                {"pt_out", 9.0},
                {"moment_z", 1e-3}}}};
}
}  // namespace

TEST_CASE("CFD metrics of a converged result are L2 simulated", "[SIM-004][TRUST-004]") {
  // Q = 7 x 0.004 = 0.028; dp_t = 1.2 x (9 - 4) = 6 Pa; dp_ts = 1.2 x (6 - 4) = 2.4 Pa;
  // T = 7e-3 N m; P = 1.4 W; eta_t = 0.028 x 6 / 1.4 = 0.12; eta_ts = 0.028 x 2.4 / 1.4 = 0.048.
  const auto r = cemkit::fans::axial_ducted::rotor_cfd_metrics(args("CONVERGED").at("args"));
  REQUIRE(r.has_value());
  CHECK_THAT(r->at("flow").at("value").get<double>(), WithinRel(0.028, 1e-12));
  CHECK_THAT(r->at("total_pressure_rise").at("value").get<double>(), WithinRel(6.0, 1e-12));
  CHECK_THAT(r->at("total_to_static_pressure_rise").at("value").get<double>(),
             WithinRel(2.4, 1e-12));
  CHECK_THAT(r->at("shaft_power").at("value").get<double>(), WithinRel(1.4, 1e-12));
  CHECK_THAT(r->at("total_efficiency").at("value").get<double>(), WithinRel(0.12, 1e-12));
  CHECK_THAT(r->at("total_to_static_efficiency").at("value").get<double>(),
             WithinRel(0.048, 1e-12));
  CHECK(r->at("flow").at("fidelity") == "l2_simulated");
}

TEST_CASE("an untrusted result gives no number", "[TRUST-001]") {
  for (const char* status : {"UNCONVERGED", "MESH_INVALID", "RESOURCE_LIMIT", "SOLVER_FAILED"}) {
    const auto r = cemkit::fans::axial_ducted::rotor_cfd_metrics(args(status).at("args"));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code() == cemkit::core::ErrorCode::sim_untrusted);
  }
}

TEST_CASE("a verified case labels only quantities with an uncertainty L2 verified", "[TRUST-002]") {
  Json a = args("VERIFIED").at("args");
  a["uncertainty"] = Json{{"torque", 1e-4}};
  const auto r = cemkit::fans::axial_ducted::rotor_cfd_metrics(a);
  REQUIRE(r.has_value());
  CHECK(r->at("torque").at("fidelity") == "l2_verified");
  CHECK(r->at("torque").at("uncertainty").get<double>() == 1e-4);
  CHECK(r->at("total_pressure_rise").at("fidelity") == "l2_simulated");
  CHECK_FALSE(r->at("total_pressure_rise").contains("uncertainty"));
}

TEST_CASE("no efficiency is reported for a pressure rise that is not positive", "[SIM-004]") {
  Json a = args("CONVERGED").at("args");
  a["pt_out"] = 3.0;
  a["p_out"] = 2.0;
  const auto r = cemkit::fans::axial_ducted::rotor_cfd_metrics(a);
  REQUIRE(r.has_value());
  CHECK_FALSE(r->contains("total_efficiency"));
  CHECK_FALSE(r->contains("total_to_static_efficiency"));
  CHECK(r->at("notes").size() == 2);
}
