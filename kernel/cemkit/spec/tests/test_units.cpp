#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cemkit/core/error.hpp"
#include "cemkit/spec/units.hpp"

using Catch::Matchers::WithinRel;
using namespace cemkit::spec;

TEST_CASE("unit table converts length units to SI metres", "[SPEC-002]") {
  const auto mm = convert_to_si(QuantityKind::length, "mm", 120.0, "nominal_size");
  REQUIRE(mm.has_value());
  CHECK_THAT(*mm, WithinRel(0.12, 1e-12));

  const auto cm = convert_to_si(QuantityKind::length, "cm", 12.0, "nominal_size");
  REQUIRE(cm.has_value());
  CHECK_THAT(*cm, WithinRel(0.12, 1e-12));

  const auto in = convert_to_si(QuantityKind::length, "in", 4.7244094488, "nominal_size");
  REQUIRE(in.has_value());
  CHECK_THAT(*in, WithinRel(0.12, 1e-6));

  const auto m = convert_to_si(QuantityKind::length, "m", 0.12, "nominal_size");
  REQUIRE(m.has_value());
  CHECK(*m == 0.12);
}

TEST_CASE("unit table converts angular velocity rpm to rad/s", "[SPEC-002]") {
  const auto rpm = convert_to_si(QuantityKind::angular_velocity, "rpm", 2000.0, "rotational_speed");
  REQUIRE(rpm.has_value());
  // 2000 * pi / 30 = 209.4395102393...
  CHECK_THAT(*rpm, WithinRel(209.43951023931953, 1e-12));

  const auto rad_s =
      convert_to_si(QuantityKind::angular_velocity, "rad/s", 209.4395102, "rotational_speed");
  REQUIRE(rad_s.has_value());
  CHECK_THAT(*rad_s, WithinRel(209.4395102, 1e-12));
}

TEST_CASE("unit table converts volume flow rates to m3/s", "[SPEC-002]") {
  const auto m3_s = convert_to_si(QuantityKind::volume_flow_rate, "m3/s", 0.05, "duty.flow");
  REQUIRE(m3_s.has_value());
  CHECK(*m3_s == 0.05);

  const auto m3_min = convert_to_si(QuantityKind::volume_flow_rate, "m3/min", 3.0, "duty.flow");
  REQUIRE(m3_min.has_value());
  CHECK_THAT(*m3_min, WithinRel(0.05, 1e-12));

  const auto m3_h = convert_to_si(QuantityKind::volume_flow_rate, "m3/h", 180.0, "duty.flow");
  REQUIRE(m3_h.has_value());
  CHECK_THAT(*m3_h, WithinRel(0.05, 1e-12));

  const auto l_s = convert_to_si(QuantityKind::volume_flow_rate, "L/s", 50.0, "duty.flow");
  REQUIRE(l_s.has_value());
  CHECK_THAT(*l_s, WithinRel(0.05, 1e-12));

  // 1 CFM = (0.3048)^3 / 60 m3/s = 0.0004719474432 m3/s
  const auto cfm = convert_to_si(QuantityKind::volume_flow_rate, "CFM", 100.0, "duty.flow");
  REQUIRE(cfm.has_value());
  CHECK_THAT(*cfm, WithinRel(0.04719474432, 1e-10));
}

TEST_CASE("unit table converts pressures to Pa including water columns", "[SPEC-002]") {
  const auto pa = convert_to_si(QuantityKind::pressure, "Pa", 150.0, "duty.pressure");
  REQUIRE(pa.has_value());
  CHECK(*pa == 150.0);

  const auto kpa = convert_to_si(QuantityKind::pressure, "kPa", 1.5, "duty.pressure");
  REQUIRE(kpa.has_value());
  CHECK(*kpa == 1500.0);

  // 1 mmH2O = 9.80665 Pa
  const auto mmh2o = convert_to_si(QuantityKind::pressure, "mmH2O", 10.0, "duty.pressure");
  REQUIRE(mmh2o.has_value());
  CHECK_THAT(*mmh2o, WithinRel(98.0665, 1e-12));

  const auto mmh2o_unicode = convert_to_si(QuantityKind::pressure, "mmH₂O", 10.0, "duty.pressure");
  REQUIRE(mmh2o_unicode.has_value());
  CHECK_THAT(*mmh2o_unicode, WithinRel(98.0665, 1e-12));

  // 1 inH2O = 25.4 * 9.80665 Pa = 249.08891 Pa
  const auto inh2o = convert_to_si(QuantityKind::pressure, "inH2O", 1.0, "duty.pressure");
  REQUIRE(inh2o.has_value());
  CHECK_THAT(*inh2o, WithinRel(249.08891, 1e-10));

  const auto inh2o_unicode = convert_to_si(QuantityKind::pressure, "inH₂O", 1.0, "duty.pressure");
  REQUIRE(inh2o_unicode.has_value());
  CHECK_THAT(*inh2o_unicode, WithinRel(249.08891, 1e-10));
}

TEST_CASE("unit table converts power and other platform quantities", "[SPEC-002]") {
  const auto w = convert_to_si(QuantityKind::power, "W", 50.0, "power");
  REQUIRE(w.has_value());
  CHECK(*w == 50.0);

  const auto kw = convert_to_si(QuantityKind::power, "kW", 1.2, "power");
  REQUIRE(kw.has_value());
  CHECK(*kw == 1200.0);

  const auto rho = convert_to_si(QuantityKind::density, "kg/m3", 1.18, "air.density");
  REQUIRE(rho.has_value());
  CHECK(*rho == 1.18);

  const auto ratio = convert_to_si(QuantityKind::ratio, "1", 0.1, "margin");
  REQUIRE(ratio.has_value());
  CHECK(*ratio == 0.1);

  const auto pct = convert_to_si(QuantityKind::ratio, "%", 10.0, "margin");
  REQUIRE(pct.has_value());
  CHECK_THAT(*pct, WithinRel(0.1, 1e-12));

  const auto temp_k = convert_to_si(QuantityKind::temperature, "K", 298.15, "air.temperature");
  REQUIRE(temp_k.has_value());
  CHECK(*temp_k == 298.15);

  const auto temp_c = convert_to_si(QuantityKind::temperature, "degC", 25.0, "air.temperature");
  REQUIRE(temp_c.has_value());
  CHECK_THAT(*temp_c, WithinRel(298.15, 1e-12));

  // Temperature tolerance (delta) in degC converts without offset
  const auto tol_c =
      convert_tolerance_to_si(QuantityKind::temperature, "degC", 5.0, "air.temperature");
  REQUIRE(tol_c.has_value());
  CHECK(*tol_c == 5.0);
}

TEST_CASE("dimensionally inconsistent unit is rejected naming the field", "[SPEC-003]") {
  const auto res = convert_to_si(QuantityKind::pressure, "m3/s", 100.0, "product.duty.pressure");
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::unit_mismatch);
  CHECK(res.error().subject() == "product.duty.pressure");
  CHECK(res.error().details().at("unit") == "m3/s");
}

TEST_CASE("unknown unit is rejected naming the field", "[SPEC-003]") {
  const auto res = convert_to_si(QuantityKind::length, "furlongs", 5.0, "envelope.width");
  REQUIRE(!res.has_value());
  CHECK(res.error().code() == cemkit::core::ErrorCode::unknown_unit);
  CHECK(res.error().subject() == "envelope.width");
  CHECK(res.error().details().at("unit") == "furlongs");
}

TEST_CASE("quantity kind to_string and parsing roundtrip", "[SPEC-002]") {
  const std::vector<QuantityKind> kinds = {
      QuantityKind::length,      QuantityKind::volume_flow_rate,
      QuantityKind::pressure,    QuantityKind::angular_velocity,
      QuantityKind::power,       QuantityKind::density,
      QuantityKind::temperature, QuantityKind::dynamic_viscosity,
      QuantityKind::ratio,
  };

  for (const auto k : kinds) {
    const auto str = to_string(k);
    CHECK(!str.empty());
    const auto parsed = parse_quantity_kind(str);
    REQUIRE(parsed.has_value());
    if (parsed) {
      CHECK(*parsed == k);
    }
  }

  CHECK(!parse_quantity_kind("invalid_kind").has_value());
}

TEST_CASE("coherent SI units for all quantity kinds", "[SPEC-002]") {
  CHECK(coherent_si_unit(QuantityKind::length) == "m");
  CHECK(coherent_si_unit(QuantityKind::volume_flow_rate) == "m3/s");
  CHECK(coherent_si_unit(QuantityKind::pressure) == "Pa");
  CHECK(coherent_si_unit(QuantityKind::angular_velocity) == "rad/s");
  CHECK(coherent_si_unit(QuantityKind::power) == "W");
  CHECK(coherent_si_unit(QuantityKind::density) == "kg/m3");
  CHECK(coherent_si_unit(QuantityKind::temperature) == "K");
  CHECK(coherent_si_unit(QuantityKind::dynamic_viscosity) == "Pa*s");
  CHECK(coherent_si_unit(QuantityKind::ratio) == "1");
}

TEST_CASE("unit inspection functions is_known_unit and kind_of_unit", "[SPEC-002]") {
  CHECK(is_known_unit("mm"));
  CHECK(is_known_unit("Pa"));
  CHECK(!is_known_unit("nonexistent_unit_123"));

  const auto k = kind_of_unit("mm");
  REQUIRE(k.has_value());
  if (k) {
    CHECK(*k == QuantityKind::length);
  }
  CHECK(!kind_of_unit("nonexistent_unit_123").has_value());
}

TEST_CASE("dynamic viscosity unit conversion", "[SPEC-002]") {
  const auto pas =
      convert_to_si(QuantityKind::dynamic_viscosity, "Pa*s", 1.8e-5, "air.dynamic_viscosity");
  REQUIRE(pas.has_value());
  CHECK(*pas == 1.8e-5);

  const auto mpas =
      convert_to_si(QuantityKind::dynamic_viscosity, "mPa*s", 0.018, "air.dynamic_viscosity");
  REQUIRE(mpas.has_value());
  CHECK_THAT(*mpas, WithinRel(1.8e-5, 1e-12));
}

TEST_CASE("convert_tolerance_to_si error paths", "[SPEC-003]") {
  const auto unk =
      convert_tolerance_to_si(QuantityKind::length, "nonexistent_unit", 1.0, "tolerance_field");
  REQUIRE(!unk.has_value());
  CHECK(unk.error().code() == cemkit::core::ErrorCode::unknown_unit);

  const auto mismatch = convert_tolerance_to_si(QuantityKind::length, "Pa", 1.0, "tolerance_field");
  REQUIRE(!mismatch.has_value());
  CHECK(mismatch.error().code() == cemkit::core::ErrorCode::unit_mismatch);
}
