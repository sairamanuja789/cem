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

core::Result<Field> parse_field(const std::string& path, const nlohmann::json& j) {
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

  // Handle text fields (e.g. process, material, size_reference, scope)
  if (j["value"].is_string()) {
    f.text_value = j["value"].get<std::string>();
  } else if (j["value"].is_array()) {
    f.text_value = j["value"].dump();
  }

  const auto kind_opt = expected_field_kind(path);
  if (kind_opt.has_value()) {
    const auto kind = *kind_opt;

    if (!j["value"].is_number()) {
      return core::fail(core::ErrorCode::spec_rejected, "numeric field value must be a number",
                        path);
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
        return core::fail(core::ErrorCode::spec_rejected, "duty point must carry a tolerance",
                          path);
      }
    }

    if (j.contains("tolerance") && j["tolerance"].is_object()) {
      const auto& tol_j = j["tolerance"];
      Tolerance tol;
      if (tol_j.contains("relative") && tol_j["relative"].is_number()) {
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
    const auto& val = it.value();

    if (key == "objectives") {
      continue;
    }

    const bool is_field =
        val.is_object() && (val.contains("provenance") || val.contains("value") ||
                            val.contains("unit") || expected_field_kind(path).has_value());

    if (is_field) {
      const auto f_res = parse_field(path, val);
      if (!f_res.has_value()) {
        return core::fail(f_res.error().code(), f_res.error().message(), f_res.error().subject(),
                          f_res.error().details());
      }
      fields[path] = *f_res;
    } else if (val.is_object()) {
      const auto res = traverse_fields(path, val, fields);
      if (!res.has_value()) {
        return res;
      }
    } else {
      // It's a primitive or array directly where a field object was expected!
      if (prefix == "envelope" || prefix == "air" || prefix == "manufacturing" ||
          prefix == "product" || prefix.starts_with("product.") || prefix.starts_with("air.")) {
        return core::fail(core::ErrorCode::spec_rejected,
                          "field must be an object with value and provenance", path,
                          {{"path", path}});
      }
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

  // Required top-level attributes
  if (!doc.contains("schema_version") || !doc["schema_version"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing schema_version", "schema_version");
  }
  if (!doc.contains("spec_id") || !doc["spec_id"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing spec_id", "spec_id");
  }
  if (!doc.contains("revision") || !doc["revision"].is_number_integer()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing revision", "revision");
  }
  if (!doc.contains("family") || !doc["family"].is_string()) {
    return core::fail(core::ErrorCode::spec_rejected, "missing family", "family");
  }

  Spec spec;
  spec.raw_document_ = doc;
  spec.schema_version_ = doc["schema_version"].get<std::string>();
  spec.spec_id_ = doc["spec_id"].get<std::string>();
  spec.revision_ = doc["revision"].get<std::uint32_t>();
  spec.family_ = doc["family"].get<std::string>();
  if (doc.contains("title") && doc["title"].is_string()) {
    spec.title_ = doc["title"].get<std::string>();
  }

  if (doc.contains("parent") && doc["parent"].is_object()) {
    const auto& p = doc["parent"];
    if (p.contains("spec_id") && p["spec_id"].is_string() && p.contains("revision") &&
        p["revision"].is_number_integer()) {
      spec.parent_ = SpecRef{.spec_id = p["spec_id"].get<std::string>(),
                             .revision = p["revision"].get<std::uint32_t>()};
    }
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
    bool is_unknown =
        (it == spec.fields_.end() || it->second.provenance == core::Provenance::unknown ||
         !it->second.si_value.has_value());

    if (is_unknown) {
      if (options_.autonomous_mode) {
        // Autonomous mode sets provisional default (SPEC-007)
        if (it != spec.fields_.end()) {
          it->second.provisional = true;
          if (ess_path == "product.duty.flow") {
            it->second.original_value = 0.05;
            it->second.original_unit = "m3/s";
            it->second.si_value = 0.05;
            it->second.si_unit = "m3/s";
            it->second.tolerance = Tolerance{.type = Tolerance::Type::relative,
                                             .minus = 0.05,
                                             .plus = 0.05,
                                             .original_minus = 0.05,
                                             .original_plus = 0.05,
                                             .unit = ""};
          } else if (ess_path == "product.duty.pressure") {
            it->second.original_value = 150.0;
            it->second.original_unit = "Pa";
            it->second.si_value = 150.0;
            it->second.si_unit = "Pa";
            it->second.pressure_kind = "fan_static";
            it->second.tolerance = Tolerance{.type = Tolerance::Type::relative,
                                             .minus = 0.05,
                                             .plus = 0.05,
                                             .original_minus = 0.05,
                                             .original_plus = 0.05,
                                             .unit = ""};
          }
        }
      } else {
        // Generate question (SPEC-008)
        std::string unit = "SI";
        std::string reason = "Essential requirement for family " + spec.family_;
        if (ess_path == "product.duty.flow") {
          unit = "m3/s";
          reason = "Essential duty point flow rate (AX-003).";
        } else if (ess_path == "product.duty.pressure") {
          unit = "Pa";
          reason = "Essential duty point pressure rise and kind (AX-004).";
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
