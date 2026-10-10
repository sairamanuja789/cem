#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cemkit/core/result.hpp"
#include "cemkit/spec/units.hpp"

namespace cemkit::spec {

// Values of a qualifier that are refused, with the reason the compiler gives.
struct RejectedQualifier {
  std::vector<std::string> values{};
  std::string message{};

  bool operator==(const RejectedQualifier&) const = default;
};

// A text member that a known numeric field must carry to say what the number means (for example the
// kind of a pressure, SPEC-004). The family states the accepted values and the messages.
struct QualifierRule {
  std::string member{};     // the member of the field object, e.g. "kind"
  std::string report_as{};  // the name the value is reported under, e.g. "pressure_kind"
  std::vector<std::string> accepted{};
  std::vector<RejectedQualifier> rejected{};
  std::string missing_message{};  // a known field without the member
  std::string unknown_prefix{};   // message = unknown_prefix + value + unknown_suffix
  std::string unknown_suffix{};
  std::string description{};  // schema text for the quantity

  bool operator==(const QualifierRule&) const = default;
};

// One field a spec may carry (ADR-015 rule 3). A numeric field has a quantity kind; a text field
// (kind empty) takes a string or, if `text_list`, a list of strings.
struct FieldDecl {
  std::string path{};
  std::optional<QuantityKind> kind{};
  bool text_list{false};
  // Schema only (the kernel does not enforce these yet): allowed text values, and whether the
  // field must be present (it may still be unknown).
  std::vector<std::string> allowed_values{};
  bool required{false};
  // A known value must carry a tolerance (SPEC-011), with this message when it does not.
  std::optional<std::string> tolerance_required{};
  std::optional<QualifierRule> qualifier{};
  // The reason given when this field is an essential unknown (SPEC-006); empty: a generic reason.
  std::string question{};
  std::string description{};
  std::string comment{};  // schema "$comment"

  bool operator==(const FieldDecl&) const = default;
};

// A group of fields (an object in the spec document), for the schema: its description and whether
// it must be present.
struct ContainerDecl {
  std::string path{};
  bool required{false};
  std::string description{};

  bool operator==(const ContainerDecl&) const = default;
};

// The fields a family declares under "product" (ADR-015): the single source of truth. The family's
// JSON schema is generated from it (family_schema) and the compiler validates against it.
struct FieldCatalogue {
  std::string title{};        // schema title
  std::string description{};  // schema description
  std::vector<FieldDecl> fields{};
  std::vector<ContainerDecl> containers{};

  [[nodiscard]] const FieldDecl* find(std::string_view path) const noexcept;
  [[nodiscard]] const ContainerDecl* find_container(std::string_view path) const noexcept;

  bool operator==(const FieldCatalogue&) const = default;
};

// The fields of the platform spec outside "product" (air, manufacturing, envelope and the top-level
// power limit), as the platform schema defines them.
[[nodiscard]] const FieldCatalogue& platform_catalogue();

// invalid_input naming the first problem: an empty, duplicate or non-"product." path, a container
// that is not a prefix of a field, a qualifier on a text field or one without accepted values.
[[nodiscard]] core::Status validate_family_catalogue(const FieldCatalogue& catalogue);

// The JSON Schema (2020-12) of the "product" block of family `family` ("<product>.<family>"),
// generated from its catalogue. Field objects refer to the platform schema's definitions.
[[nodiscard]] nlohmann::ordered_json family_schema(std::string_view family,
                                                   const FieldCatalogue& catalogue);
// The URN $id of that schema: urn:cemkit:schema:products:<product>:v1:families:<family>.
[[nodiscard]] std::string family_schema_id(std::string_view family);

// The family catalogue the compiler validates against (ADR-015). An error (for example an unknown
// family) rejects the spec with that error.
using FieldCatalogueResolver = std::function<core::Result<FieldCatalogue>(std::string_view family)>;

}  // namespace cemkit::spec
