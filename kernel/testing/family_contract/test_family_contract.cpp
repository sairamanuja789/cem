// ADR-015 M1 (FAM-001, FAM-002, SPEC-001, SPEC-006, SPEC-008): every registered family, the
// products' and the test fixtures', goes through the same platform path (spec compile, family
// feasibility, initial design, geometry recipe) with the same rules. The fixtures include a
// structural bracket that shares no physics with fans and a ceiling fan with no pressure duty, so
// any fan assumption left in the platform fails here. Spec values are test inputs.
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cemkit/product/products.hpp"
#include "cemkit/product/registry.hpp"
#include "cemkit/spec/compiler.hpp"
#include "cemkit/spec/units.hpp"
#include "testing/contract_stubs/register.hpp"
#include "testing/stub_product/register.hpp"

using cemkit::core::ErrorCode;
using cemkit::product::Family;
using cemkit::product::Registry;
using json = nlohmann::json;

namespace {

constexpr const char* k_axial = "fans.axial_ducted";
constexpr const char* k_bracket = "contract_stubs.stub_bracket";
constexpr const char* k_ceiling = "contract_stubs.stub_ceiling";

std::shared_ptr<const Registry> all_families() {
  auto registry = std::make_shared<Registry>();
  cemkit::product::Operations operations;
  REQUIRE(cemkit::product::register_products(*registry, operations).has_value());
  REQUIRE(cemkit::testing::stub_product::register_stub_product_families(*registry).has_value());
  REQUIRE(cemkit::testing::contract_stubs::register_contract_stub_families(*registry).has_value());
  return registry;
}

const Family& family_of(const Registry& registry, const std::string& id) {
  auto f = registry.get(id);
  REQUIRE(f.has_value());
  return f->get();  // NOLINT(bugprone-unchecked-optional-access): REQUIRE above
}

// Sets a dotted path in a JSON document, creating objects on the way.
void put(json& doc, const std::string& path, json value) {
  json* node = &doc;
  std::size_t start = 0;
  for (auto dot = path.find('.'); dot != std::string::npos; dot = path.find('.', start)) {
    node = &(*node)[path.substr(start, dot - start)];
    start = dot + 1;
  }
  (*node)[path.substr(start)] = std::move(value);
}

// An unknown field object for a declared field (SPEC-001): null value, its coherent SI unit.
json unknown_field(const Family& family, const std::string& path) {
  json field{{"value", nullptr}, {"provenance", "unknown"}};
  const auto catalogue = family.field_catalogue();
  if (const auto* decl = catalogue.find(path); decl != nullptr && decl->kind.has_value()) {
    field["unit"] = std::string{cemkit::spec::coherent_si_unit(*decl->kind)};
  }
  return field;
}

// A spec of `family` whose essential fields are all stated and unknown.
json unknown_spec(const Family& family) {
  json doc{{"schema_version", "1.0.0"},
           {"spec_id", "contract"},
           {"revision", 1},
           {"family", std::string{family.id()}},
           {"product", json::object()}};
  for (const auto& path : family.essential_fields()) {
    put(doc, path, unknown_field(family, path));
  }
  return doc;
}

// A classified error: never an internal error, and a not_implemented names "<family>.<operation>"
// (D4).
void check_classified(const cemkit::core::Error& error, const Family& family,
                      const std::string& operation) {
  CHECK(error.code() != ErrorCode::internal_error);
  if (error.code() == ErrorCode::not_implemented) {
    const auto it = error.details().find("capability");
    REQUIRE(it != error.details().end());
    CHECK(it->second == std::string{family.id()} + "." + operation);
  }
}

}  // namespace

TEST_CASE("the contract suite covers the product and both unlike fixtures", "[FAM-002]") {
  const auto registry = all_families();
  CHECK(registry->contains(k_axial));
  CHECK(registry->contains(k_bracket));
  CHECK(registry->contains(k_ceiling));
}

TEST_CASE("every family's essential fields are in its catalogue and become questions",
          "[FAM-001][SPEC-006][SPEC-008]") {
  const auto registry = all_families();
  const cemkit::spec::SpecCompiler compiler{cemkit::product::compiler_options(registry)};
  for (const auto& id : registry->ids()) {
    INFO("family " << id);
    const Family& family = family_of(*registry, id);
    const auto spec = compiler.compile(unknown_spec(family));
    REQUIRE(spec.has_value());
    std::vector<std::string> asked;
    for (const auto& q : spec->questions()) {  // NOLINT(bugprone-unchecked-optional-access)
      asked.push_back(q.field);
    }
    CHECK(asked == family.essential_fields());
  }
}

TEST_CASE("every family goes through feasibility, initial design and recipe the same way",
          "[FAM-001][SEL-004]") {
  const auto registry = all_families();
  const cemkit::spec::SpecCompiler compiler{cemkit::product::compiler_options(registry)};
  for (const auto& id : registry->ids()) {
    INFO("family " << id);
    const Family& family = family_of(*registry, id);
    const auto spec = compiler.compile(unknown_spec(family));
    REQUIRE(spec.has_value());
    const auto& s = spec.value();  // NOLINT(bugprone-unchecked-optional-access): REQUIRE above

    const auto feasibility = family.feasibility(s);
    if (!feasibility) {
      check_classified(feasibility.error(), family, "feasibility");
    }
    const auto design = family.initial_design(s);
    if (!design) {
      check_classified(design.error(), family, "initial_design");
      continue;
    }
    const auto recipe = family.geometry_recipe(*design);
    if (!recipe) {
      check_classified(recipe.error(), family, "geometry_recipe");
    }
  }
}

TEST_CASE("each family rejects another family's fields, naming the field", "[SPEC-001][FAM-001]") {
  const auto registry = all_families();
  const cemkit::spec::SpecCompiler compiler{cemkit::product::compiler_options(registry)};
  int pairs_checked = 0;
  for (const std::string a : {k_axial, k_bracket, k_ceiling}) {
    for (const std::string b : {k_axial, k_bracket, k_ceiling}) {
      if (a == b) {
        continue;
      }
      INFO(a << " given a field of " << b);
      const Family& fa = family_of(*registry, a);
      const Family& fb = family_of(*registry, b);
      int rejected = 0;
      for (const auto& path : fb.essential_fields()) {
        if (fa.field_catalogue().find(path) != nullptr) {
          continue;  // both families declare it
        }
        json doc = unknown_spec(fa);
        put(doc, path, unknown_field(fb, path));
        const auto spec = compiler.compile(doc);
        REQUIRE(!spec.has_value());
        CHECK(spec.error().code() == ErrorCode::spec_rejected);
        // The compiler names the first undeclared level: the field, or the group it sits in.
        const std::string& subject = spec.error().subject();
        CHECK((subject == path || path.starts_with(subject + ".")));
        CHECK(subject.starts_with("product."));
        ++rejected;
      }
      CHECK(rejected > 0);  // these three families share no essential field
      ++pairs_checked;
    }
  }
  CHECK(pairs_checked == 6);
}

TEST_CASE("a non-fan family's pressure needs no fan pressure kind", "[SPEC-004][FAM-001]") {
  // SPEC-004's fan_total/fan_static rule belongs to fans.axial_ducted's catalogue, not the
  // platform.
  const auto registry = all_families();
  const cemkit::spec::SpecCompiler compiler{cemkit::product::compiler_options(registry)};
  json doc = unknown_spec(family_of(*registry, k_bracket));
  put(doc, "product.load.surface_pressure",
      json{{"value", 1000.0}, {"unit", "Pa"}, {"provenance", "user"}});
  const auto spec = compiler.compile(doc);
  REQUIRE(spec.has_value());
  CHECK(spec->field("product.load.surface_pressure")  // NOLINT(bugprone-unchecked-optional-access)
            .value_or(cemkit::spec::Field{})
            .qualifier == std::nullopt);

  json axial = unknown_spec(family_of(*registry, k_axial));
  put(axial, "product.duty.pressure",
      json{{"value", 100.0},
           {"unit", "Pa"},
           {"provenance", "user"},
           {"tolerance", {{"relative", 0.1}}}});
  const auto rejected = compiler.compile(axial);
  REQUIRE(!rejected.has_value());
  CHECK(rejected.error().subject() == "product.duty.pressure");
}
