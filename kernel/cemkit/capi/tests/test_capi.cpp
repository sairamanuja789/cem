#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "cemkit/capi/cemkit.h"

using Catch::Matchers::WithinRel;
using json = nlohmann::json;

namespace {

struct Response {
  cemkit_status status;
  json body;  // null when the call returned no string
};

using Entry = cemkit_status (*)(const char*, char**);

Response call(Entry entry, const std::string& request) {
  char* out = nullptr;
  const cemkit_status status = entry(request.c_str(), &out);
  const std::unique_ptr<char, decltype(&cemkit_free)> owned{out, &cemkit_free};
  return Response{.status = status, .body = out != nullptr ? json::parse(out) : json(nullptr)};
}

Response call(Entry entry, const json& request) { return call(entry, request.dump()); }

// Arbitrary geometry test parameters (not a fan design), as in the T10 OCCT tests, in metres.
json smoke_request() {
  return json{{"hub_radius", 0.015},
              {"hub_length", 0.020},
              {"sections",
               {{{"radius", 0.010}, {"chord", 0.018}, {"thickness", 0.002}, {"stagger", 0.3}},
                {{"radius", 0.030}, {"chord", 0.015}, {"thickness", 0.0015}, {"stagger", 0.5}},
                {{"radius", 0.050}, {"chord", 0.012}, {"thickness", 0.0012}, {"stagger", 0.7}}}},
              {"exports", {{{"format", "step"}}}}};
}

json minimal_spec() {
  return json{
      {"schema_version", "1.0.0"},
      {"spec_id", "capi-test"},
      {"revision", 1},
      {"family", "fans.axial_ducted"},
      {"product", {{"nominal_size", {{"value", 120.0}, {"unit", "mm"}, {"provenance", "user"}}}}}};
}

}  // namespace

TEST_CASE("the ABI reports its semantic version and the kernel version", "[MAINT-005]") {
  uint32_t major = 99;
  uint32_t minor = 99;
  uint32_t patch = 99;
  cemkit_abi_version(&major, &minor, &patch);
  CHECK(major == CEMKIT_ABI_VERSION_MAJOR);
  CHECK(minor == CEMKIT_ABI_VERSION_MINOR);
  CHECK(patch == CEMKIT_ABI_VERSION_PATCH);
  cemkit_abi_version(nullptr, nullptr, nullptr);  // NULL pointers are allowed
  CHECK(std::string{cemkit_kernel_version()}.find('.') != std::string::npos);
}

TEST_CASE("bad arguments return status codes, never exceptions", "[MAINT-005]") {
  char* out = nullptr;
  CHECK(cemkit_l0_batch(nullptr, &out) == CEMKIT_INVALID_ARGUMENT);
  CHECK(out == nullptr);
  CHECK(cemkit_l0_batch("{}", nullptr) == CEMKIT_INVALID_ARGUMENT);
  cemkit_free(nullptr);

  const auto malformed = call(&cemkit_l0_batch, std::string{"{not json"});
  CHECK(malformed.status == CEMKIT_FAILED);
  CHECK(malformed.body["error"]["code"] == "invalid_input");
  CHECK(malformed.body["error"]["subject"] == "request");

  const auto missing = call(&cemkit_l0_batch, json{{"casez", json::array()}});
  CHECK(missing.status == CEMKIT_FAILED);
  CHECK(missing.body["error"]["subject"] == "cases");

  const auto wrong_type =
      call(&cemkit_l0_batch,
           json{{"cases",
                 {{{"function", "tip_speed"}, {"args", {{"omega", "fast"}, {"d_tip", 0.1}}}}}}});
  CHECK(wrong_type.status == CEMKIT_FAILED);
  CHECK(wrong_type.body["error"]["subject"] == "cases[0].omega");

  const auto unknown = call(
      &cemkit_l0_batch, json{{"cases", {{{"function", "warp_drive"}, {"args", json::object()}}}}});
  CHECK(unknown.status == CEMKIT_FAILED);
  CHECK(unknown.body["error"]["code"] == "invalid_input");
}

TEST_CASE("a batch evaluates every case in one call, errors per case", "[PERF-002][PHY-005]") {
  const json request{
      {"cases",
       {{{"id", "a"},
         {"function", "specific_speed"},
         {"args", {{"omega", 209.43951023931953}, {"flow", 0.05}, {"fan_total_pressure", 150.0}}}},
        {{"id", "b"},
         {"function", "specific_speed"},
         {"args", {{"omega", 209.43951023931953}, {"flow", 0.0}, {"fan_total_pressure", 150.0}}}},
        {{"function", "tip_speed"}, {"args", {{"omega", 200.0}, {"d_tip", 0.1}}}}}}};
  const auto r = call(&cemkit_l0_batch, request);
  REQUIRE(r.status == CEMKIT_OK);
  const auto& results = r.body["results"];
  REQUIRE(results.size() == 3);
  CHECK(results[0]["id"] == "a");
  CHECK_THAT(results[0]["value"].get<double>(), WithinRel(1.2370483575363835, 1e-12));
  CHECK(results[0]["fidelity"] == "l0_predicted");
  CHECK(results[0]["model"] == "fans.l0@1.0.0");
  CHECK(results[1]["error"]["describe"] ==
        "out_of_validity at flow: input must be finite and strictly positive (bounds=(0, inf), "
        "model=fans.l0@1.0.0, value=0)");
  CHECK(results[2]["id"] == 2);  // no id: the position
  CHECK(r.body["abi_version"] == "0.2.0");
}

TEST_CASE("feasibility through the ABI reports range unsourced", "[SEL-004][SEL-002]") {
  const json request{{"flow", 0.05},
                     {"fan_total_pressure", 150.0},
                     {"omega", 209.43951023931953},
                     {"d_tip", 0.119},
                     {"range",
                      {{"family", "fans.axial_ducted"},
                       {"specific_speed_min", nullptr},
                       {"specific_speed_max", nullptr},
                       {"source", "UNSOURCED"}}}};
  const auto r = call(&cemkit_feasibility, request);
  REQUIRE(r.status == CEMKIT_OK);
  CHECK(r.body["report"]["verdict"] == "unconfirmed");
  CHECK(r.body["report"]["checks"][2]["status"] == "range_unsourced");

  json bad = request;
  bad["flow"] = -1.0;
  const auto e = call(&cemkit_feasibility, bad);
  CHECK(e.status == CEMKIT_FAILED);
  CHECK(e.body["error"]["code"] == "out_of_validity");
  CHECK(e.body["error"]["subject"] == "flow");
}

TEST_CASE("spec compile through the ABI returns fields and questions", "[SPEC-002][SPEC-008]") {
  const auto r = call(&cemkit_spec_compile, json{{"spec", minimal_spec()}});
  REQUIRE(r.status == CEMKIT_OK);
  const auto& spec = r.body["spec"];
  CHECK(spec["spec_id"] == "capi-test");
  CHECK(spec["fields"]["product.nominal_size"]["si_value"] == 0.12);
  CHECK(spec["fields"]["product.nominal_size"]["original_unit"] == "mm");
  CHECK(spec["has_unresolved_essential_unknowns"] == true);
  CHECK(spec["questions"].size() == 2);

  json rejected = minimal_spec();
  rejected["product"]["nominal_size"]["unit"] = "Pa";
  const auto e = call(&cemkit_spec_compile, json{{"spec", rejected}});
  CHECK(e.status == CEMKIT_FAILED);
  CHECK(e.body["error"]["code"] == "unit_mismatch");
  CHECK(e.body["error"]["subject"] == "product.nominal_size");

  const auto bad_mode =
      call(&cemkit_spec_compile, json{{"spec", minimal_spec()}, {"autonomous_mode", "yes"}});
  CHECK(bad_mode.status == CEMKIT_FAILED);
  CHECK(bad_mode.body["error"]["subject"] == "autonomous_mode");
}

TEST_CASE("geometry smoke builds a valid solid deterministically", "[GEO-001][GEO-002][GEO-004]") {
  const auto first = call(&cemkit_geometry_smoke, smoke_request());
  REQUIRE(first.status == CEMKIT_OK);
  CHECK(first.body["backend"] == "occt");
  CHECK(first.body["validity"]["ok"] == true);
  CHECK(first.body["topology"]["solids"] == 1);
  CHECK(first.body["mass_properties"]["volume"].get<double>() > 0.0);
  REQUIRE(first.body["exports"].size() == 1);
  CHECK(first.body["exports"][0]["sha256"].get<std::string>().size() == 64);
  // GEO-001: the same parameters give the same result, including the export hash.
  const auto second = call(&cemkit_geometry_smoke, smoke_request());
  CHECK(second.body == first.body);

  json not_one_solid = smoke_request();
  not_one_solid["sections"][0]["radius"] = 0.020;  // plate starts outside the hub
  const auto failed = call(&cemkit_geometry_smoke, not_one_solid);
  CHECK(failed.status == CEMKIT_FAILED);
  CHECK(failed.body["error"]["code"] == "geometry_failed");

  json bad_format = smoke_request();
  bad_format["exports"] = {{{"format", "obj"}}};
  CHECK(call(&cemkit_geometry_smoke, bad_format).body["error"]["subject"] == "format");
}

TEST_CASE("responses carry the versions STORE-002 records", "[MAINT-005][PHY-004]") {
  const auto r = call(&cemkit_l0_batch, json{{"cases", json::array()}});
  REQUIRE(r.status == CEMKIT_OK);
  CHECK(r.body["models"]["fans.l0"] == "1.0.0");
  CHECK(r.body["models"]["fans.feasibility"] == "1.0.0");
  CHECK(r.body["models"]["platform.air"] == "1.0.0");
  CHECK(r.body["plugins"]["fans.axial_ducted"] == "0.1.0");
}

TEST_CASE("air may be partial; the rest comes from the kernel's default air", "[PHY-002]") {
  const json request{{"cases",
                      {{{"function", "max_fan_total_pressure"}, {"args", json::object()}},
                       {{"function", "max_fan_total_pressure"},
                        {"args", json::object()},
                        {"air", {{"pressure", 100000.0}}}}}}};
  const auto r = call(&cemkit_l0_batch, request);
  REQUIRE(r.status == CEMKIT_OK);
  CHECK(r.body["results"][0]["value"] == 1418.55);
  CHECK_THAT(r.body["results"][1]["value"].get<double>(), WithinRel(1400.0, 1e-12));
  CHECK(r.body["results"][0]["fidelity_label"] == "L0 predicted");
}

TEST_CASE("spec compile asks the family registry; an unknown family is rejected", "[SPEC-006]") {
  json unknown = minimal_spec();
  unknown["family"] = "fans.no_such_family";
  const auto e = call(&cemkit_spec_compile, json{{"spec", unknown}});
  CHECK(e.status == CEMKIT_FAILED);
  CHECK(e.body["error"]["code"] == "spec_rejected");
  CHECK(e.body["error"]["subject"] == "family");
}

TEST_CASE("geometry smoke can return the export bytes", "[GEO-004]") {
  json request = smoke_request();
  request["include_data"] = true;
  const auto r = call(&cemkit_geometry_smoke, request);
  REQUIRE(r.status == CEMKIT_OK);
  const auto& file = r.body["exports"][0];
  const auto size = file["size_bytes"].get<std::size_t>();
  const auto encoded = file["data_base64"].get<std::string>();
  CHECK(encoded.size() == (size + 2) / 3 * 4);
  CHECK(encoded.starts_with("SVNPLTEwMzAz"));  // "ISO-10303" (a STEP file)
}

TEST_CASE("feasibility from a spec maps the duty and uses the family hook", "[SEL-004][UC-03]") {
  json spec = minimal_spec();
  spec["product"]["duty"] = {{"flow",
                              {{"value", 0.05},
                               {"unit", "m3/s"},
                               {"provenance", "user"},
                               {"tolerance", {{"relative", 0.05}}}}},
                             {"pressure",
                              {{"value", 120.0},
                               {"unit", "Pa"},
                               {"kind", "fan_static"},
                               {"provenance", "user"},
                               {"tolerance", {{"minus", 0.0}, {"plus", 10.0}}}}}};
  spec["product"]["size_reference"] = {{"value", "duct_inner_diameter"}, {"provenance", "user"}};
  spec["product"]["rotational_speed"] = {
      {"value", 2000.0}, {"unit", "rpm"}, {"provenance", "user"}};
  const auto r = call(&cemkit_feasibility, json{{"spec", spec}});
  REQUIRE(r.status == CEMKIT_OK);
  // l0_005: 120 Pa static + 11.5315 Pa dynamic through the 0.12 m duct.
  const auto& total = r.body["inputs"]["fan_total_pressure"];
  CHECK_THAT(total["value"].get<double>(), WithinRel(131.53153903336792, 1e-12));
  CHECK(total["provenance"] == "derived");
  CHECK(total["fidelity"] == "l0_predicted");
  CHECK(r.body["report"]["verdict"] == "unconfirmed");
  CHECK(r.body["report"]["checks"][1]["status"] == "not_computable");
  CHECK(r.body["report"]["checks"][2]["status"] == "range_unsourced");

  const auto unknown = call(&cemkit_feasibility, json{{"spec", minimal_spec()}});
  CHECK(unknown.status == CEMKIT_FAILED);
  CHECK(unknown.body["error"]["subject"] == "product.duty.flow");

  json no_reference = spec;
  no_reference["product"].erase("size_reference");
  const auto e = call(&cemkit_feasibility, json{{"spec", no_reference}});
  CHECK(e.status == CEMKIT_FAILED);
  CHECK(e.body["error"]["subject"] == "product.size_reference");

  json impossible = spec;
  impossible["product"]["duty"]["pressure"]["value"] = 1500.0;
  impossible["product"]["duty"]["pressure"]["kind"] = "fan_total";
  const auto i = call(&cemkit_feasibility, json{{"spec", impossible}});
  REQUIRE(i.status == CEMKIT_OK);
  CHECK(i.body["report"]["verdict"] == "infeasible");
  CHECK(i.body["report"]["checks"][0]["violation"]["details"]["bounds"] == "(0, 1418.55]");
}

TEST_CASE("feasibility from a spec computes the tip-speed check from the D6 tip diameter",
          "[SEL-004][AX-001][AX-009]") {
  json spec = minimal_spec();
  spec["product"]["duty"] = {{"flow",
                              {{"value", 0.05},
                               {"unit", "m3/s"},
                               {"provenance", "user"},
                               {"tolerance", {{"relative", 0.05}}}}},
                             {"pressure",
                              {{"value", 150.0},
                               {"unit", "Pa"},
                               {"kind", "fan_total"},
                               {"provenance", "user"},
                               {"tolerance", {{"minus", 0.0}, {"plus", 10.0}}}}}};
  spec["product"]["size_reference"] = {{"value", "duct_inner_diameter"}, {"provenance", "user"}};
  spec["product"]["tip_clearance_min"] = {{"value", 0.5}, {"unit", "mm"}, {"provenance", "user"}};
  spec["product"]["rotational_speed"] = {
      {"value", 2000.0}, {"unit", "rpm"}, {"provenance", "user"}};
  const auto r = call(&cemkit_feasibility, json{{"spec", spec}});
  REQUIRE(r.status == CEMKIT_OK);
  // Owner decision D6: D_tip = 0.12 m - 2 x 0.0005 m, provenance default, provisional.
  const auto& d_tip = r.body["inputs"]["d_tip"];
  CHECK_THAT(d_tip["value"].get<double>(), WithinRel(0.12 - (2.0 * 0.0005), 1e-12));
  CHECK(d_tip["unit"] == "m");
  CHECK(d_tip["provenance"] == "default");
  CHECK(d_tip["provisional"] == true);
  CHECK(d_tip["from"] == json::array({"product.nominal_size", "product.size_reference",
                                      "product.tip_clearance_min"}));
  const auto tip_check = [](const json& report) {
    for (const auto& check : report["checks"]) {
      if (check["limit"] == "incompressible_tip_speed") {
        return check;
      }
    }
    FAIL("no incompressible_tip_speed check");
    return json{};
  };
  CHECK(tip_check(r.body["report"])["status"] == "pass");

  json no_clearance = spec;
  no_clearance["product"].erase("tip_clearance_min");
  const auto n = call(&cemkit_feasibility, json{{"spec", no_clearance}});
  REQUIRE(n.status == CEMKIT_OK);
  CHECK(!n.body["inputs"].contains("d_tip"));
  CHECK(tip_check(n.body["report"])["status"] == "not_computable");

  // A clearance of half the duct diameter leaves no rotor: rejected on its field, never a number.
  json too_wide = spec;
  too_wide["product"]["tip_clearance_min"]["value"] = 60.0;
  const auto w = call(&cemkit_feasibility, json{{"spec", too_wide}});
  CHECK(w.status == CEMKIT_FAILED);
  CHECK(w.body["error"]["code"] == "spec_rejected");
  CHECK(w.body["error"]["subject"] == "product.tip_clearance_min");
}
