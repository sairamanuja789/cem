#include "cemkit/spec/compiler.hpp"

#include <string>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/provenance.hpp"
#include "cemkit/spec/units.hpp"

namespace cemkit::spec {

namespace {

std::optional<QuantityKind> expected_field_kind(std::string_view path) {
  if (path == "envelope.width" || path == "envelope.height" || path == "envelope.depth_min" ||
      path == "envelope.depth_max" || path == "product.nominal_size" ||
      path == "product.wall_thickness_min" || path == "product.tip_clearance_min" ||
      path == "product.motor.bore_diameter") {
    return QuantityKind::length;
  }
  if (path == "product.duty.flow") {
    return QuantityKind::volume_flow_rate;
  }
  if (path == "product.duty.pressure") {
    return QuantityKind::pressure;
  }
  if (path == "product.rotational_speed" || path == "product.motor.speed_min" ||
      path == "product.motor.speed_max") {
    return QuantityKind::angular_velocity;
  }
  if (path == "air.density") {
    return QuantityKind::density;
  }
  if (path == "air.temperature") {
    return QuantityKind::temperature;
  }
  if (path == "air.dynamic_viscosity") {
    return QuantityKind::dynamic_viscosity;
  }
  if (path == "product.pressure_margin" || path == "product.safety_factor.factor" ||
      path == "product.safety_factor.at_speed_ratio") {
    return QuantityKind::ratio;
  }
  if (path == "product.power_limit" || path == "product.motor.power_limit" ||
      path == "power_limit") {
    return QuantityKind::power;
  }
  return std::nullopt;
}

bool is_known_field(std::string_view path) {
  if (expected_field_kind(path).has_value()) {
    return true;
  }
  if (path == "manufacturing.process" || path == "manufacturing.material" ||
      path == "product.size_reference" || path == "product.scope") {
    return true;
  }
  return false;
}

bool is_known_container(std::string_view path) {
  return path == "air" || path == "manufacturing" || path == "envelope" || path == "product" ||
         path == "product.duty" || path == "product.safety_factor" || path == "product.motor";
}

core::Result<Field> parse_field(const std::string& path, const nlohmann::json& j) {
  if (!is_known_field(path)) {
    return core::fail(core::ErrorCode::spec_rejected, "unknown or invalid field path: " + path,
                      path, {{"path", path}});
  }

  if (!j.is_object()) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "field must be an object with provenance and value", path, {{"path", path}});
  }

  if (!j.contains("provenance") || !j["provenance"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "field is missing provenance", path,
                      {{"path", path}});
  }

  const std::string prov_str = j["provenance"].get<std::string>();
  const auto prov_opt = core::parse_provenance(prov_str);
  if (!prov_opt) {
    return core::fail(core::ErrorCode::spec_rejected, "invalid provenance: " + prov_str, path,
                      {{"provenance", prov_str}});
  }
  const auto prov = *prov_opt;

  Field f;
  f.path = path;
  f.provenance = prov;

  if (j.contains("provisional") && j["provisional"].is_boolean()) {
    f.provisional = j["provisional"].get<bool>();
  }
  if (j.contains("note") && j["note"].is_string()) {
    f.note = j["note"].get<std::string>();
  }

  if (prov == core::Provenance::image) {
    if (!j.contains("confidence") || !j["confidence"].is_number()) {
      return core::fail(core::ErrorCode::spec_rejected, "image field must carry confidence", path);
    }
    const double conf = j["confidence"].get<double>();
    if (conf < 0.0 || conf > 1.0) {
      return core::fail(core::ErrorCode::spec_rejected, "confidence must be in [0, 1]", path);
    }
    f.confidence = conf;
  } else if (j.contains("confidence")) {
    return core::fail(core::ErrorCode::spec_rejected, "confidence is only allowed on image fields",
                      path);
  }

  if (!j.contains("value")) {
    return core::fail(core::ErrorCode::spec_rejected, "field is missing value key", path);
  }

  if (prov == core::Provenance::unknown) {
    if (!j["value"].is_null()) {
      return core::fail(core::ErrorCode::spec_rejected, "unknown field must have null value", path);
    }
    if (j.contains("tolerance")) {
      return core::fail(core::ErrorCode::spec_rejected, "unknown field cannot carry tolerance",
                        path);
    }
    return f;
  }

  // Known field: value cannot be null
  if (j["value"].is_null()) {
    return core::fail(core::ErrorCode::spec_rejected, "known field value cannot be null", path);
  }

  const auto kind_opt = expected_field_kind(path);

  // Handle text fields (e.g. process, material, size_reference, scope)
  if (!kind_opt.has_value()) {
    if (!j["value"].is_string() && !j["value"].is_array()) {
      return core::fail(core::ErrorCode::spec_rejected, "text field value must be string or array",
                        path);
    }
    if (j["value"].is_string()) {
      f.text_value = j["value"].get<std::string>();
    } else {
      f.text_value = j["value"].dump();
    }
    return f;
  }

  const auto kind = *kind_opt;

  if (!j["value"].is_number()) {
    return core::fail(core::ErrorCode::spec_rejected, "numeric field value must be a number", path);
  }
  if (!j.contains("unit") || !j["unit"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "numeric field missing unit", path);
  }

  const double original_val = j["value"].get<double>();
  const std::string unit_str = j["unit"].get<std::string>();

  const auto si_res = convert_to_si(kind, unit_str, original_val, path);
  if (!si_res.has_value()) {
    return core::fail(si_res.error().code(), si_res.error().message(), path,
                      si_res.error().details());
  }

  // Positivity and validity bounds
  if (kind == QuantityKind::temperature && *si_res < 0.0) {
    return core::fail(core::ErrorCode::spec_rejected,
                      "temperature cannot be below absolute zero (0 K)", path);
  }
  if (kind == QuantityKind::volume_flow_rate && *si_res <= 0.0) {
    return core::fail(core::ErrorCode::spec_rejected, "flow rate must be positive", path);
  }
  if (kind == QuantityKind::density && *si_res <= 0.0) {
    return core::fail(core::ErrorCode::spec_rejected, "density must be positive", path);
  }
  if (kind == QuantityKind::dynamic_viscosity && *si_res <= 0.0) {
    return core::fail(core::ErrorCode::spec_rejected, "dynamic viscosity must be positive", path);
  }

  f.original_value = original_val;
  f.original_unit = unit_str;
  f.si_value = *si_res;
  f.si_unit = std::string(coherent_si_unit(kind));

  // Pressure specific checks (SPEC-004)
  if (kind == QuantityKind::pressure) {
    if (!j.contains("kind") || !j["kind"].is_string()) {
      return core::fail(core::ErrorCode::spec_rejected,
                        "pressure must state its kind (fan_total or fan_static)", path);
    }
    const std::string p_kind = j["kind"].get<std::string>();
    if (p_kind == "static_to_static" || p_kind == "static-to-static") {
      return core::fail(
          core::ErrorCode::spec_rejected,
          "static-to-static pressure is rejected (ISO 5801 requires fan_total or fan_static)",
          path);
    }
    if (p_kind == "total_to_static" || p_kind == "total-to-static") {
      return core::fail(core::ErrorCode::spec_rejected,
                        "'total-to-static' is an efficiency type, not a pressure type (ISO 5801)",
                        path);
    }
    if (p_kind != "fan_total" && p_kind != "fan_static") {
      return core::fail(core::ErrorCode::spec_rejected,
                        "unknown pressure kind: " + p_kind + " (must be fan_total or fan_static)",
                        path);
    }
    f.pressure_kind = p_kind;
  }

  // Tolerances (SPEC-011)
  if (path == "product.duty.flow" || path == "product.duty.pressure") {
    if (!j.contains("tolerance") || !j["tolerance"].is_object()) {
      return core::fail(core::ErrorCode::spec_rejected, "duty point must carry a tolerance", path);
    }
  }

  if (j.contains("tolerance") && j["tolerance"].is_object()) {
    const auto& tol_j = j["tolerance"];
    Tolerance tol;
    if (tol_j.contains("relative") && tol_j["relative"].is_number()) {
      if (kind == QuantityKind::temperature && unit_str != "K") {
        return core::fail(core::ErrorCode::spec_rejected,
                          "relative tolerance is not allowed for offset units", path);
      }
      tol.type = Tolerance::Type::relative;
      const double r = tol_j["relative"].get<double>();
      if (r <= 0.0) {
        return core::fail(core::ErrorCode::spec_rejected, "relative tolerance must be positive",
                          path);
      }
      tol.minus = r;
      tol.plus = r;
      tol.original_minus = r;
      tol.original_plus = r;
    } else if (tol_j.contains("minus") && tol_j["minus"].is_number() && tol_j.contains("plus") &&
               tol_j["plus"].is_number()) {
      tol.type = Tolerance::Type::absolute;
      const double m = tol_j["minus"].get<double>();
      const double p = tol_j["plus"].get<double>();
      if (m < 0.0 || p < 0.0) {
        return core::fail(core::ErrorCode::spec_rejected,
                          "absolute tolerance bounds must be non-negative", path);
      }
      const auto m_si = convert_tolerance_to_si(kind, unit_str, m, path);
      const auto p_si = convert_tolerance_to_si(kind, unit_str, p, path);
      if (!m_si.has_value() || !p_si.has_value()) {
        return core::fail(core::ErrorCode::spec_rejected, "cannot convert tolerance", path);
      }
      tol.minus = *m_si;
      tol.plus = *p_si;
      tol.original_minus = m;
      tol.original_plus = p;
      tol.unit = unit_str;
    } else {
      return core::fail(core::ErrorCode::spec_rejected, "malformed tolerance object", path);
    }
    f.tolerance = tol;
  }

  return f;
}

core::Result<void> traverse_fields(const std::string& prefix, const nlohmann::json& node,
                                   std::map<std::string, Field>& fields) {
  if (!node.is_object()) {
    return {};
  }
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string& key = it.key();
    std::string path;
    if (prefix.empty()) {
      path = key;
    } else {
      path = prefix;
      path += '.';
      path += key;
    }
    if (prefix.empty()) {
      if (key == "schema_version" || key == "spec_id" || key == "revision" || key == "parent" ||
          key == "family" || key == "title" || key == "objectives") {
        continue;
      }
    } else if (key == "objectives") {
      continue;
    }

    const auto& val = it.value();
    if (val.is_object()) {
      const bool is_field = val.contains("provenance") || val.contains("value") ||
                            val.contains("unit") || is_known_field(path);
      if (is_field) {
        if (!is_known_field(path)) {
          return core::fail(core::ErrorCode::spec_rejected,
                            "unknown or invalid field path: " + path, path, {{"path", path}});
        }
        const auto f_res = parse_field(path, val);
        if (!f_res.has_value()) {
          return core::fail(f_res.error().code(), f_res.error().message(), f_res.error().subject(),
                            f_res.error().details());
        }
        fields[path] = *f_res;
      } else {
        if (!is_known_container(path)) {
          return core::fail(core::ErrorCode::spec_rejected, "unknown container path: " + path, path,
                            {{"path", path}});
        }
        const auto res = traverse_fields(path, val, fields);
        if (!res.has_value()) {
          return res;
        }
      }
    } else {
      // It's a primitive or array directly where a field object or container was expected!
      return core::fail(core::ErrorCode::spec_rejected,
                        "field must be an object with value and provenance", path,
                        {{"path", path}});
    }
  }
  return {};
}

}  // namespace

core::Result<Spec> SpecCompiler::compile_json(std::string_view json_str) const {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_str);
  } catch (const std::exception& e) {
    return core::fail(core::ErrorCode::invalid_input,
                      std::string("malformed JSON input: ") + e.what(), "spec");
  }
  return compile(doc);
}

core::Result<Spec> SpecCompiler::compile(const nlohmann::json& doc) const {
  if (!doc.is_object()) {
    return core::fail(core::ErrorCode::spec_rejected, "spec must be a JSON object", "spec");
  }
  if (!options_.essential_resolver) {
    return core::fail(core::ErrorCode::invalid_input, "empty essential_resolver",
                      "essential_resolver");
  }

  // Required top-level attributes
  if (!doc.contains("schema_version") || !doc["schema_version"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing schema_version", "schema_version");
  }
  if (!doc.contains("spec_id") || !doc["spec_id"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing spec_id", "spec_id");
  }
  if (!doc.contains("revision")) {
    return core::fail(core::ErrorCode::spec_rejected, "missing revision", "revision");
  }
  if (!doc["revision"].is_number_integer()) {
    return core::fail(core::ErrorCode::invalid_input, "revision must be an integer", "revision");
  }
  const auto rev_val = doc["revision"].get<std::int64_t>();
  if (rev_val < 1 ||
      rev_val > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) - 1) {
    return core::fail(core::ErrorCode::invalid_input,
                      "revision must be between 1 and UINT32_MAX - 1", "revision");
  }

  if (!doc.contains("family") || !doc["family"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing family", "family");
  }

  // Check top-level keys
  for (auto it = doc.begin(); it != doc.end(); ++it) {
    const std::string& key = it.key();
    if (key == "schema_version" || key == "spec_id" || key == "revision" || key == "parent" ||
        key == "family" || key == "title" || key == "air" || key == "manufacturing" ||
        key == "envelope" || key == "objectives" || key == "product") {
      continue;
    }
    if (key == "power_limit") {
      if (!it.value().is_object()) {
        return core::fail(core::ErrorCode::spec_rejected,
                          "field must be an object with value and provenance", "power_limit");
      }
      continue;
    }
    return core::fail(core::ErrorCode::spec_rejected, "unknown top-level field: " + key, key);
  }

  Spec spec;
  spec.options_ = options_;
  spec.raw_document_ = doc;
  spec.schema_version_ = doc["schema_version"].get<std::string>();
  spec.spec_id_ = doc["spec_id"].get<std::string>();
  spec.revision_ = static_cast<std::uint32_t>(rev_val);
  spec.family_ = doc["family"].get<std::string>();
  if (doc.contains("title") && doc["title"].is_string()) {
    spec.title_ = doc["title"].get<std::string>();
  }

  if (doc.contains("parent") && !doc["parent"].is_null()) {
    if (!doc["parent"].is_object()) {
      return core::fail(core::ErrorCode::invalid_input, "parent must be an object or null",
                        "parent");
    }
    const auto& p = doc["parent"];
    if (!p.contains("spec_id") || !p["spec_id"].is_string() || !p.contains("revision") ||
        !p["revision"].is_number_integer()) {
      return core::fail(core::ErrorCode::invalid_input, "malformed parent reference", "parent");
    }
    const auto p_rev = p["revision"].get<std::int64_t>();
    if (p_rev < 1 || p_rev >= rev_val) {
      return core::fail(core::ErrorCode::invalid_input,
                        "parent revision must be >= 1 and < revision", "parent");
    }
    spec.parent_ = SpecRef{.spec_id = p["spec_id"].get<std::string>(),
                           .revision = static_cast<std::uint32_t>(p_rev)};
  }

  // Parse fields
  const auto trav_res = traverse_fields("", doc, spec.fields_);
  if (!trav_res.has_value()) {
    return core::fail(trav_res.error().code(), trav_res.error().message(),
                      trav_res.error().subject(), trav_res.error().details());
  }

  // Essential fields checks (SPEC-006, SPEC-007, SPEC-008)
  const auto essential_list = options_.essential_resolver(spec.family_);
  for (const auto& ess_path : essential_list) {
    auto it = spec.fields_.find(ess_path);
    const bool is_unknown =
        (it == spec.fields_.end() || it->second.provenance == core::Provenance::unknown ||
         (!it->second.si_value.has_value() && !it->second.text_value.has_value()));

    if (is_unknown) {
      bool resolved_by_default = false;
      if (options_.autonomous_mode && options_.default_resolver) {
        auto def_opt = options_.default_resolver(spec.family_, ess_path);
        if (def_opt.has_value()) {
          Field f = std::move(*def_opt);
          f.path = ess_path;
          f.provenance = core::Provenance::default_value;
          f.provisional = true;
          spec.fields_[ess_path] = std::move(f);
          resolved_by_default = true;
        }
      }

      if (!resolved_by_default) {
        std::string unit;
        const auto kind_opt = expected_field_kind(ess_path);
        if (kind_opt.has_value()) {
          unit = std::string(coherent_si_unit(*kind_opt));
        }
        std::string reason;
        if (ess_path == "product.duty.flow") {
          reason = "Essential duty point flow rate (AX-003).";
        } else if (ess_path == "product.duty.pressure") {
          reason = "Essential duty point pressure rise and kind (AX-004).";
        } else if (ess_path == "product.nominal_size") {
          reason = "Essential outer geometry envelope / nominal size (AX-001).";
        } else if (ess_path == "product.rotational_speed") {
          reason = "Essential design operating rotational speed (AX-002).";
        } else {
          reason = "Essential requirement for family " + spec.family_ + " (" + ess_path + ").";
        }
        spec.questions_.push_back(
            Question{.field = ess_path, .unit = std::move(unit), .reason = std::move(reason)});
      }
    }
  }

  // Contradiction checks (SPEC-010)
  const auto contra_res = spec.check_contradictions();
  if (!contra_res.has_value()) {
    return core::fail(contra_res.error().code(), contra_res.error().message(),
                      contra_res.error().subject(), contra_res.error().details());
  }

  return spec;
}

}  // namespace cemkit::spec
