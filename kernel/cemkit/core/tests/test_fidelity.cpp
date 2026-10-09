#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "cemkit/core/fidelity.hpp"

using cemkit::core::Fidelity;

TEST_CASE("the five fidelity levels have fixed names and report labels", "[COR-003][REP-001]") {
  CHECK(cemkit::core::k_fidelity_levels.size() == 5);
  CHECK(cemkit::core::to_string(Fidelity::l0_predicted) == "l0_predicted");
  CHECK(cemkit::core::to_string(Fidelity::l1_predicted) == "l1_predicted");
  CHECK(cemkit::core::to_string(Fidelity::l2_simulated) == "l2_simulated");
  CHECK(cemkit::core::to_string(Fidelity::l2_verified) == "l2_verified");
  CHECK(cemkit::core::to_string(Fidelity::l3_validated) == "l3_validated");
  CHECK(cemkit::core::report_label(Fidelity::l0_predicted) == "L0 predicted");
  CHECK(cemkit::core::report_label(Fidelity::l1_predicted) == "L1 predicted");
  CHECK(cemkit::core::report_label(Fidelity::l2_simulated) == "L2 simulated");
  CHECK(cemkit::core::report_label(Fidelity::l2_verified) == "L2 verified");
  CHECK(cemkit::core::report_label(Fidelity::l3_validated) == "L3 validated");
}

TEST_CASE("fidelity names round-trip and unknown names are rejected", "[COR-003]") {
  for (const auto& info : cemkit::core::k_fidelity_levels) {
    CHECK(cemkit::core::parse_fidelity(info.name) == info.level);
  }
  CHECK_FALSE(cemkit::core::parse_fidelity("L2 verified").has_value());
  CHECK_FALSE(cemkit::core::parse_fidelity("l4").has_value());
  CHECK(cemkit::core::to_string(static_cast<Fidelity>(99)).empty());
  CHECK(cemkit::core::report_label(static_cast<Fidelity>(99)).empty());
}

TEST_CASE("only L2 verified and L3 validated require an uncertainty", "[COR-003]") {
  CHECK_FALSE(cemkit::core::requires_uncertainty(Fidelity::l0_predicted));
  CHECK_FALSE(cemkit::core::requires_uncertainty(Fidelity::l1_predicted));
  CHECK_FALSE(cemkit::core::requires_uncertainty(Fidelity::l2_simulated));
  CHECK(cemkit::core::requires_uncertainty(Fidelity::l2_verified));
  CHECK(cemkit::core::requires_uncertainty(Fidelity::l3_validated));
}
