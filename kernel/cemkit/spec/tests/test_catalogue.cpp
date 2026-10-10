// ADR-015 rule 3 (SPEC-001, SPEC-004, SPEC-006, SPEC-011): the spec compiler validates fields
// against the platform's and the family's catalogues and has no product field of its own. The
// catalogue here is a neutral test fixture; every name and value is a test parameter.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/spec/catalogue.hpp"
#include "cemkit/spec/compiler.hpp"

using cemkit::core::ErrorCode;
using json = nlohmann::json;
using namespace cemkit::spec;

namespace {

FieldCatalogue fixture_catalogue() {
  return FieldCatalogue{
      .title = "fixture",
      .description = "test fixture",
      .fields = {FieldDecl{.path = "product.size",
                           .kind = QuantityKind::length,
                           .required = true,
                           .question = "Fixture reason for the size."},
                 FieldDecl{.path = "product.group.rate",
                           .kind = QuantityKind::volume_flow_rate,
                           .tolerance_required = "fixture rate needs a tolerance"},
                 FieldDecl{.path = "product.mode", .allowed_values = {"a", "b"}},
                 FieldDecl{.path = "product.head",
                           .kind = QuantityKind::pressure,
                           .qualifier = QualifierRule{.member = "basis",
                                                      .report_as = "head_basis",
                                                      .accepted = {"x", "y"},
                                                      .rejected = {{.values = {"z"},
                                                                    .message = "z is refused"}},
                                                      .missing_message = "state the basis",
                                                      .unknown_prefix = "unknown basis: ",
                                                      .unknown_suffix = " (x or y)"}}},
      .containers = {{.path = "product.group", .description = "a group"}}};
}

CompilerOptions options() {
  return CompilerOptions{
      .essential_resolver = [](std::string_view) -> cemkit::core::Result<std::vector<std::string>> {
        return std::vector<std::string>{"product.size"};
      },
      .default_resolver = {},
      .catalogue_resolver = [](std::string_view family) -> cemkit::core::Result<FieldCatalogue> {
        if (family != "test.fixture") {
          return cemkit::core::fail(ErrorCode::spec_rejected, "unknown family", "family");
        }
        return fixture_catalogue();
      }};
}

json doc(json product) {
  return json{{"schema_version", "1.0.0"},
              {"spec_id", "c"},
              {"revision", 1},
              {"family", "test.fixture"},
              {"product", std::move(product)}};
}

cemkit::core::Error rejection(const json& d) {
  const auto r = SpecCompiler{options()}.compile(d);
  REQUIRE(!r.has_value());
  return r.error();
}

}  // namespace

TEST_CASE("the compiler takes field kinds, questions and rules from the catalogue",
          "[SPEC-001][SPEC-006][SPEC-008]") {
  const auto spec = SpecCompiler{options()}.compile(doc(json::object()));
  REQUIRE(spec.has_value());
  REQUIRE(spec->questions().size() == 1);
  CHECK(spec->questions().front().field == "product.size");
  CHECK(spec->questions().front().unit == "m");
  CHECK(spec->questions().front().reason == "Fixture reason for the size.");

  const auto known = SpecCompiler{options()}.compile(
      doc({{"size", {{"value", 20.0}, {"unit", "mm"}, {"provenance", "user"}}},
           {"head", {{"value", 1.0}, {"unit", "kPa"}, {"basis", "y"}, {"provenance", "user"}}}}));
  REQUIRE(known.has_value());
  CHECK(known->field("product.size").value_or(Field{}).si_value == 0.02);
  CHECK(known->field("product.head").value_or(Field{}).qualifier ==
        Qualifier{.name = "head_basis", .value = "y"});
}

TEST_CASE("fields outside the catalogue are rejected naming the path", "[SPEC-001]") {
  CHECK(rejection(
            doc({{"duty", {{"flow", {{"value", 1.0}, {"unit", "m3/s"}, {"provenance", "user"}}}}}}))
            .subject() == "product.duty");  // the first undeclared level is named
  CHECK(rejection(doc({{"elsewhere", {{"x", 1}}}})).subject() == "product.elsewhere");
}

TEST_CASE("a declared qualifier is required, refused or unknown with the family's messages",
          "[SPEC-004]") {
  const auto head = [](const json& extra) {
    json f{{"value", 1.0}, {"unit", "Pa"}, {"provenance", "user"}};
    f.update(extra);
    return doc({{"head", f}});
  };
  CHECK(rejection(head(json::object())).message() == "state the basis");
  CHECK(rejection(head({{"basis", "z"}})).message() == "z is refused");
  CHECK(rejection(head({{"basis", "w"}})).message() == "unknown basis: w (x or y)");
}

TEST_CASE("a declared tolerance requirement applies to known values only", "[SPEC-011]") {
  const auto rate = [](json f) { return doc({{"group", {{"rate", std::move(f)}}}}); };
  CHECK(rejection(rate({{"value", 1.0}, {"unit", "m3/s"}, {"provenance", "user"}})).message() ==
        "fixture rate needs a tolerance");
  CHECK(SpecCompiler{options()}
            .compile(rate({{"value", nullptr}, {"unit", "m3/s"}, {"provenance", "unknown"}}))
            .has_value());
}

TEST_CASE("the compiler refuses to run without a catalogue resolver", "[SPEC-001]") {
  CompilerOptions o = options();
  o.catalogue_resolver = nullptr;
  const auto r = SpecCompiler{o}.compile(doc(json::object()));
  REQUIRE(!r.has_value());
  CHECK(r.error().code() == ErrorCode::invalid_input);
  CHECK(r.error().subject() == "catalogue_resolver");
}

TEST_CASE("a catalogue resolver error rejects the spec", "[SPEC-006]") {
  json d = doc(json::object());
  d["family"] = "test.other";
  CHECK(rejection(d).subject() == "family");
}

TEST_CASE("family catalogues are validated", "[FAM-001]") {
  CHECK(validate_family_catalogue(fixture_catalogue()).has_value());
  const auto invalid = [](const FieldCatalogue& c) {
    const auto r = validate_family_catalogue(c);
    REQUIRE(!r.has_value());
    return r.error().code();
  };
  CHECK(invalid({.fields = {{.path = "envelope.width", .kind = QuantityKind::length}}}) ==
        ErrorCode::invalid_input);
  CHECK(invalid({.fields = {{.path = "product.a"}, {.path = "product.a"}}}) ==
        ErrorCode::invalid_input);
  CHECK(invalid({.fields = {{.path = "product.a", .qualifier = QualifierRule{.member = "m"}}}}) ==
        ErrorCode::invalid_input);
  CHECK(invalid({.fields = {{.path = "product.a"}}, .containers = {{.path = "product.b"}}}) ==
        ErrorCode::invalid_input);
}

TEST_CASE("the family schema is generated from the catalogue", "[SPEC-001][FAM-002]") {
  const auto schema = family_schema("test.fixture", fixture_catalogue());
  CHECK(schema["$id"] == "urn:cemkit:schema:products:test:v1:families:fixture");
  CHECK(schema["x-cemkit-families"] == nlohmann::ordered_json::array({"test.fixture"}));
  CHECK(schema["required"] == nlohmann::ordered_json::array({"size"}));
  CHECK(schema["properties"]["size"]["$ref"] == "urn:cemkit:schema:v1:spec#/$defs/length");
  CHECK(schema["properties"]["group"]["properties"]["rate"]["then"]["required"][0] == "tolerance");
  CHECK(schema["properties"]["mode"]["properties"]["value"]["enum"] ==
        nlohmann::ordered_json::array({"a", "b", nullptr}));
  CHECK(schema["properties"]["head"]["$ref"] == "#/$defs/pressure_with_basis");
  CHECK(schema["$defs"]["pressure_with_basis"]["properties"]["basis"]["enum"] ==
        nlohmann::ordered_json::array({"x", "y"}));
}
