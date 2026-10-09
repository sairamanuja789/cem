#include "cemkit/spec/essential.hpp"
#include "cemkit/spec/spec.hpp"

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
  return {};
}

core::Result<Spec> Spec::derive_new_revision(const nlohmann::json& /*modifications*/) const {
  return core::fail(core::ErrorCode::internal_error, "not implemented");
}

nlohmann::json Spec::to_json() const {
  return raw_document_;
}

}  // namespace cemkit::spec
