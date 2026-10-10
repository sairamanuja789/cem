#include "cemkit/spec/catalogue.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "cemkit/core/error.hpp"

namespace cemkit::spec {

namespace {

using OJson = nlohmann::ordered_json;

constexpr std::string_view k_product_prefix = "product.";
constexpr std::string_view k_platform_spec = "urn:cemkit:schema:v1:spec#/$defs/";

FieldDecl quantity(std::string path, QuantityKind kind) {
  return FieldDecl{.path = std::move(path), .kind = kind};
}

FieldDecl text(std::string path) { return FieldDecl{.path = std::move(path)}; }

std::string platform_def(std::string_view name) {
  return std::string{k_platform_spec} + std::string{name};
}

std::string qualifier_def_name(const FieldDecl& field) {
  // validate_family_catalogue ensures a qualified field is numeric.
  return std::string{to_string(field.kind.value_or(QuantityKind::ratio))} + "_with_" +
         field.qualifier.value_or(QualifierRule{}).member;
}

OJson known_condition() { return OJson{{"$ref", "#/$defs/known"}}; }

OJson field_schema(const FieldDecl& field) {
  OJson out = OJson::object();
  if (!field.description.empty()) {
    out["description"] = field.description;
  }
  if (field.kind.has_value()) {
    out["$ref"] = field.qualifier ? "#/$defs/" + qualifier_def_name(field)
                                  : platform_def(to_string(*field.kind));
  } else if (field.allowed_values.empty() && !field.text_list) {
    out["$ref"] = platform_def("text");
  } else {
    out["$ref"] = platform_def("field_core");
    OJson value = OJson::object();
    OJson allowed = OJson::array();
    for (const auto& v : field.allowed_values) {
      allowed.push_back(v);
    }
    if (field.text_list) {
      value["type"] = OJson::array({"array", "null"});
      value["items"] =
          field.allowed_values.empty() ? OJson{{"type", "string"}} : OJson{{"enum", allowed}};
      value["minItems"] = 1;
      value["uniqueItems"] = true;
    } else {
      allowed.push_back(nullptr);
      value["enum"] = allowed;
    }
    out["properties"] = OJson{{"value", value}};
    out["unevaluatedProperties"] = false;
  }
  if (!field.comment.empty()) {
    out["$comment"] = field.comment;
  }
  if (field.tolerance_required) {
    out["if"] = known_condition();
    out["then"] = OJson{{"required", OJson::array({"tolerance"})}};
  }
  return out;
}

OJson qualifier_schema(const FieldDecl& field) {
  const QualifierRule rule = field.qualifier.value_or(QualifierRule{});
  const std::string kind{to_string(field.kind.value_or(QuantityKind::ratio))};
  OJson accepted = OJson::array();
  for (const auto& v : rule.accepted) {
    accepted.push_back(v);
  }
  OJson out = OJson::object();
  if (!rule.description.empty()) {
    out["description"] = rule.description;
  }
  out["$ref"] = platform_def("quantity_core");
  out["properties"] = OJson{{"unit", OJson{{"$ref", platform_def(kind + "_unit")}}},
                            {rule.member, {{"enum", accepted}}}};
  out["if"] = known_condition();
  out["then"] = OJson{{"required", OJson::array({rule.member})}};
  out["unevaluatedProperties"] = false;
  return out;
}

// The parent object path of `path` inside the product block ("" for the block itself).
std::string parent_of(std::string_view path) {
  const auto dot = path.rfind('.');
  return dot == std::string_view::npos ? std::string{} : std::string{path.substr(0, dot)};
}

std::string leaf_of(std::string_view path) {
  const auto dot = path.rfind('.');
  return std::string{dot == std::string_view::npos ? path : path.substr(dot + 1)};
}

// One object (the product block, or a container) with its children in declaration order.
OJson object_schema(const FieldCatalogue& catalogue, const std::string& path) {
  OJson properties = OJson::object();
  OJson required = OJson::array();
  std::set<std::string> seen;
  const auto add_child = [&](const std::string& child_path, OJson schema, bool is_required) {
    const std::string leaf = leaf_of(child_path);
    if (!seen.insert(leaf).second) {
      return;
    }
    if (is_required) {
      required.push_back(leaf);
    }
    properties[leaf] = std::move(schema);
  };
  for (const auto& field : catalogue.fields) {
    const std::string relative = field.path.substr(k_product_prefix.size());
    if (parent_of(relative) == path) {
      add_child(relative, field_schema(field), field.required);
      continue;
    }
    // The first field under a direct child container places that container.
    const std::string prefix = path.empty() ? std::string{} : path + ".";
    if (relative.starts_with(prefix) && relative.size() > prefix.size()) {
      const std::string rest = relative.substr(prefix.size());
      const std::string child = prefix + rest.substr(0, rest.find('.'));
      if (child != relative && !seen.contains(leaf_of(child))) {
        const ContainerDecl* decl = catalogue.find_container(std::string{k_product_prefix} + child);
        add_child(child, object_schema(catalogue, child), decl != nullptr && decl->required);
      }
    }
  }
  OJson out = OJson::object();
  if (!path.empty()) {
    if (const ContainerDecl* decl = catalogue.find_container(std::string{k_product_prefix} + path);
        decl != nullptr && !decl->description.empty()) {
      out["description"] = decl->description;
    }
  }
  out["type"] = "object";
  out["additionalProperties"] = false;
  if (!required.empty()) {
    out["required"] = required;
  }
  out["properties"] = properties;
  return out;
}

}  // namespace

const FieldDecl* FieldCatalogue::find(std::string_view path) const noexcept {
  const auto it = std::ranges::find(fields, path, &FieldDecl::path);
  return it == fields.end() ? nullptr : &*it;
}

const ContainerDecl* FieldCatalogue::find_container(std::string_view path) const noexcept {
  const auto it = std::ranges::find(containers, path, &ContainerDecl::path);
  return it == containers.end() ? nullptr : &*it;
}

const FieldCatalogue& platform_catalogue() {
  static const FieldCatalogue catalogue{
      .title = "platform fields",
      .description = "Fields outside the product block (urn:cemkit:schema:v1:spec).",
      .fields = {quantity("air.density", QuantityKind::density),
                 quantity("air.temperature", QuantityKind::temperature),
                 quantity("air.dynamic_viscosity", QuantityKind::dynamic_viscosity),
                 text("manufacturing.process"), text("manufacturing.material"),
                 quantity("envelope.width", QuantityKind::length),
                 quantity("envelope.height", QuantityKind::length),
                 quantity("envelope.depth_min", QuantityKind::length),
                 quantity("envelope.depth_max", QuantityKind::length),
                 quantity("power_limit", QuantityKind::power)},
      .containers = {{.path = "air"}, {.path = "manufacturing"}, {.path = "envelope"}}};
  return catalogue;
}

core::Status validate_family_catalogue(const FieldCatalogue& catalogue) {
  const auto bad = [](const std::string& message, const std::string& path) -> core::Status {
    return core::fail(core::ErrorCode::invalid_input, message, "field_catalogue", {{"path", path}});
  };
  std::set<std::string> paths;
  std::set<std::string> qualifier_defs;
  for (const auto& field : catalogue.fields) {
    if (!field.path.starts_with(k_product_prefix) || field.path.size() == k_product_prefix.size() ||
        field.path.ends_with('.') || field.path.find("..") != std::string::npos) {
      return bad("a family field path must be \"product.<name>\"", field.path);
    }
    if (!paths.insert(field.path).second) {
      return bad("a field path is declared twice", field.path);
    }
    if (field.qualifier) {
      if (!field.kind || field.qualifier->member.empty() || field.qualifier->report_as.empty() ||
          field.qualifier->accepted.empty()) {
        return bad("a qualifier needs a numeric field, a member, a report name and accepted values",
                   field.path);
      }
      if (!qualifier_defs.insert(qualifier_def_name(field)).second) {
        const auto first = std::ranges::find_if(catalogue.fields, [&](const FieldDecl& other) {
          return other.qualifier && qualifier_def_name(other) == qualifier_def_name(field);
        });
        if (first != catalogue.fields.end() && first->qualifier != field.qualifier) {
          return bad("two different qualifier rules share a quantity kind and member", field.path);
        }
      }
    }
    if (field.kind && (field.text_list || !field.allowed_values.empty())) {
      return bad("only a text field can list allowed values", field.path);
    }
  }
  for (const auto& container : catalogue.containers) {
    const std::string prefix = container.path + ".";
    const bool used = std::ranges::any_of(
        catalogue.fields, [&](const FieldDecl& f) { return f.path.starts_with(prefix); });
    if (!container.path.starts_with(k_product_prefix) || !used || paths.contains(container.path)) {
      return bad("a container must be a \"product.\" prefix of a declared field", container.path);
    }
  }
  return {};
}

std::string family_schema_id(std::string_view family) {
  const auto dot = family.find('.');
  const std::string product{family.substr(0, dot)};
  const std::string name{dot == std::string_view::npos ? family : family.substr(dot + 1)};
  return "urn:cemkit:schema:products:" + product + ":v1:families:" + name;
}

nlohmann::ordered_json family_schema(std::string_view family, const FieldCatalogue& catalogue) {
  OJson body = object_schema(catalogue, "");
  OJson out = OJson::object();
  out["$schema"] = "https://json-schema.org/draft/2020-12/schema";
  out["$id"] = family_schema_id(family);
  out["title"] = catalogue.title;
  out["description"] = catalogue.description;
  out["$comment"] =
      "GENERATED from the family's field catalogue (Family::field_catalogue, ADR-015); never edit. "
      "Regenerate with scripts/gen_family_schemas.sh.";
  out["x-cemkit-families"] = OJson::array({std::string{family}});
  for (auto& [key, value] : body.items()) {
    out[key] = value;
  }
  OJson defs = OJson::object();
  const bool conditional = std::ranges::any_of(catalogue.fields, [](const FieldDecl& f) {
    return f.tolerance_required.has_value() || f.qualifier.has_value();
  });
  if (conditional) {
    defs["known"] = OJson{{"required", OJson::array({"provenance"})},
                          {"properties", {{"provenance", {{"not", {{"const", "unknown"}}}}}}}};
  }
  for (const auto& field : catalogue.fields) {
    if (field.qualifier && !defs.contains(qualifier_def_name(field))) {
      defs[qualifier_def_name(field)] = qualifier_schema(field);
    }
  }
  if (!defs.empty()) {
    out["$defs"] = defs;
  }
  return out;
}

}  // namespace cemkit::spec
