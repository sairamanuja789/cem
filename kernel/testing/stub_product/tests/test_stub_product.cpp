// SPEC-006, FAM-002, UC-09: the spec compiler asks the registered stub plugin for its essential
// fields. Spec values are test inputs, not engineering data.
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cemkit/product/registry.hpp"
#include "cemkit/spec/compiler.hpp"
#include "testing/stub_product/register.hpp"

using cemkit::core::ErrorCode;
using json = nlohmann::json;

namespace {

std::shared_ptr<const cemkit::product::Registry> stub_registry() {
  auto registry = std::make_shared<cemkit::product::Registry>();
  REQUIRE(cemkit::testing::stub_product::register_stub_product_families(*registry).has_value());
  return registry;
}

cemkit::spec::SpecCompiler compiler() {
  return cemkit::spec::SpecCompiler{cemkit::spec::CompilerOptions{
      .essential_resolver = cemkit::product::essential_field_resolver(stub_registry())}};
}

json minimal_spec(const std::string& family) {
  return json{{"schema_version", "1.0.0"},
              {"spec_id", "stub-1"},
              {"revision", 1},
              {"family", family},
              {"product",
               {{"duty",
                 {{"flow",
                   {{"value", 1.0},
                    {"unit", "m3/s"},
                    {"provenance", "user"},
                    {"tolerance", {{"relative", 0.1}}}}}}}}}};
}

std::vector<std::string> question_fields(const cemkit::spec::Spec& spec) {
  std::vector<std::string> fields;
  for (const auto& q : spec.questions()) {
    fields.push_back(q.field);
  }
  return fields;
}

}  // namespace

TEST_CASE("the stub product registers its families in a fixed order", "[FAM-002][UC-09]") {
  const auto registry = stub_registry();
  CHECK(registry->ids() == std::vector<std::string>{"stub_product.stub_family"});
}

TEST_CASE("the spec compiler asks the stub plugin for its essential fields", "[SPEC-006]") {
  const auto spec = compiler().compile(minimal_spec("stub_product.stub_family"));
  REQUIRE(spec.has_value());
  // Exactly the stub's declared list: not the axial fan's (flow, pressure), and the stated flow
  // is not asked for.
  CHECK(question_fields(*spec) ==
        std::vector<std::string>{"product.nominal_size", "product.rotational_speed"});
  CHECK(spec->has_unresolved_essential_unknowns());

  json complete = minimal_spec("stub_product.stub_family");
  complete["product"]["nominal_size"] = {{"value", 1.0}, {"unit", "m"}, {"provenance", "user"}};
  complete["product"]["rotational_speed"] = {
      {"value", 1.0}, {"unit", "rad/s"}, {"provenance", "user"}};
  const auto known = compiler().compile(complete);
  REQUIRE(known.has_value());
  CHECK(known->questions().empty());

  // A derived revision keeps asking the same plugin (SPEC-009).
  const auto derived = spec->derive_new_revision(json{{"title", "revised"}});
  REQUIRE(derived.has_value());
  CHECK(question_fields(*derived) == question_fields(*spec));
}

TEST_CASE("the spec compiler rejects a family with no registered plugin", "[SPEC-006][FAM-002]") {
  const auto spec = compiler().compile(minimal_spec("stub_product.not_registered"));
  REQUIRE(!spec.has_value());
  CHECK(spec.error().code() == ErrorCode::spec_rejected);
  CHECK(spec.error().subject() == "family");
}
