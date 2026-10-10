#include "cemkit/spec/spec.hpp"

#include "cemkit/core/error.hpp"
#include "cemkit/spec/compiler.hpp"

namespace cemkit::spec {

std::optional<Field> Spec::field(std::string_view path) const {
  auto it = fields_.find(std::string(path));
  if (it != fields_.end()) {
    return it->second;
  }
  return std::nullopt;
}

core::Result<void> Spec::check_contradictions() const {
  // Contradiction check (SPEC-010): air power exceeds stated power limit.
  const auto flow_f = field("product.duty.flow");
  const auto pressure_f = field("product.duty.pressure");

  if (flow_f && flow_f->si_value && pressure_f && pressure_f->si_value) {
    const double q = *flow_f->si_value;
    const double p = *pressure_f->si_value;
    if (q > 0.0 && p > 0.0) {
      const double air_power = q * p;

      // Look for power limit in product or motor or top-level; check every limit present
      const std::array power_limit_paths = {std::string_view{"product.power_limit"},
                                            std::string_view{"product.motor.power_limit"},
                                            std::string_view{"power_limit"}};

      for (const auto path : power_limit_paths) {
        const auto limit_f = field(path);
        if (limit_f && limit_f->si_value.has_value()) {
          const double power_limit = *limit_f->si_value;
          if (air_power > power_limit) {
            return core::fail(core::ErrorCode::infeasible_requirement,
                              "required air power exceeds stated power limit", limit_f->path,
                              {{"air_power", core::format_number(air_power)},
                               {"power_limit", core::format_number(power_limit)}});
          }
        }
      }
    }
  }

  return {};
}

namespace {

int provenance_rank(core::Provenance p) noexcept {
  switch (p) {
    case core::Provenance::user:
      return 4;
    case core::Provenance::image:
      return 3;
    case core::Provenance::derived:
      return 2;
    case core::Provenance::default_value:
      return 1;
    case core::Provenance::unknown:
      return 0;
  }
  return -1;
}

void merge_json(nlohmann::json& target, const nlohmann::json& patch) {
  if (!patch.is_object()) {
    target = patch;
    return;
  }
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_object() && target.contains(it.key()) && target[it.key()].is_object()) {
      merge_json(target[it.key()], it.value());
    } else {
      target[it.key()] = it.value();
    }
  }
}

void find_patch_fields(const std::string& prefix, const nlohmann::json& node,
                       std::vector<std::pair<std::string, nlohmann::json>>& patch_fields) {
  if (!node.is_object()) {
    return;
  }
  if (node.contains("provenance") && node["provenance"].is_string()) {
    patch_fields.emplace_back(prefix, node);
    return;
  }
  for (auto it = node.begin(); it != node.end(); ++it) {
    std::string path = prefix.empty() ? it.key() : prefix + "." + it.key();
    if (it.value().is_object()) {
      find_patch_fields(path, it.value(), patch_fields);
    }
  }
}

void remove_json_path(nlohmann::json& node, std::string_view path) {
  const auto dot = path.find('.');
  if (dot == std::string_view::npos) {
    if (node.is_object()) {
      node.erase(std::string(path));
    }
    return;
  }
  std::string head(path.substr(0, dot));
  std::string_view tail = path.substr(dot + 1);
  if (node.is_object() && node.contains(head) && node[head].is_object()) {
    remove_json_path(node[head], tail);
  }
}

}  // namespace

core::Result<Spec> Spec::derive_new_revision(const nlohmann::json& modifications) const {
  if (!modifications.is_object()) {
    return core::fail(core::ErrorCode::invalid_input, "modifications must be a JSON object",
                      "modifications");
  }
  if (revision_ >= std::numeric_limits<std::uint32_t>::max() - 1) {
    return core::fail(core::ErrorCode::invalid_input, "revision would overflow", "revision");
  }

  nlohmann::json updated = raw_document_;
  nlohmann::json patch_copy = modifications;
  std::vector<ProvenanceConflict> detected_conflicts;

  std::vector<std::pair<std::string, nlohmann::json>> patch_fields;
  find_patch_fields("", modifications, patch_fields);

  for (const auto& [path, field_json] : patch_fields) {
    const auto patch_prov_opt = core::parse_provenance(field_json["provenance"].get<std::string>());
    if (!patch_prov_opt.has_value()) {
      continue;
    }
    const auto patch_prov = *patch_prov_opt;

    auto old_it = fields_.find(path);
    if (old_it != fields_.end()) {
      const auto old_prov = old_it->second.provenance;
      if (provenance_rank(patch_prov) < provenance_rank(old_prov)) {
        // Lower rank loses: keep old field, record conflict, do not apply patch field
        detected_conflicts.push_back(
            ProvenanceConflict{.field = path, .winning = old_prov, .overridden = patch_prov});
        remove_json_path(patch_copy, path);
      } else if (provenance_rank(patch_prov) > provenance_rank(old_prov)) {
        // Higher rank wins: patch overrides old field, record conflict
        detected_conflicts.push_back(
            ProvenanceConflict{.field = path, .winning = patch_prov, .overridden = old_prov});
      }
    }
  }

  merge_json(updated, patch_copy);

  updated["revision"] = revision_ + 1;
  updated["parent"] = nlohmann::json{{"spec_id", spec_id_}, {"revision", revision_}};

  SpecCompiler compiler(options_);
  auto res = compiler.compile(updated);
  if (!res.has_value()) {
    return res;
  }

  res->conflicts_ = std::move(detected_conflicts);
  return res;
}

nlohmann::json Spec::to_json() const { return raw_document_; }

}  // namespace cemkit::spec
