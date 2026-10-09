#include "cemkit/spec/spec.hpp"

#include "cemkit/core/error.hpp"
#include "cemkit/spec/compiler.hpp"
#include "cemkit/spec/essential.hpp"

namespace cemkit::spec {

std::vector<std::string> default_essential_fields(std::string_view family) {
  if (family == "fans.axial_ducted") {
    return {"product.duty.flow", "product.duty.pressure"};
  }
  return {};
}

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

      // Look for power limit in product or motor
      std::optional<Field> limit_f = field("product.power_limit");
      if (!limit_f) {
        limit_f = field("product.motor.power_limit");
      }
      if (!limit_f) {
        limit_f = field("power_limit");
      }

      if (limit_f && limit_f->si_value) {
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

}  // namespace

core::Result<Spec> Spec::derive_new_revision(const nlohmann::json& modifications) const {
  nlohmann::json updated = raw_document_;
  merge_json(updated, modifications);

  updated["revision"] = revision_ + 1;
  updated["parent"] = nlohmann::json{{"spec_id", spec_id_}, {"revision", revision_}};

  SpecCompiler compiler;
  auto res = compiler.compile(updated);
  if (!res.has_value()) {
    return res;
  }

  // Detect provenance conflicts / overrides (SPEC-005)
  for (const auto& [path, new_f] : res->fields_) {
    auto old_it = fields_.find(path);
    if (old_it != fields_.end()) {
      const auto& old_f = old_it->second;
      if (old_f.provenance != new_f.provenance &&
          provenance_rank(new_f.provenance) > provenance_rank(old_f.provenance)) {
        res->conflicts_.push_back(ProvenanceConflict{
            .field = path, .winning = new_f.provenance, .overridden = old_f.provenance});
      }
    }
  }

  return res;
}

nlohmann::json Spec::to_json() const { return raw_document_; }

}  // namespace cemkit::spec
