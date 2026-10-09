#include "cemkit/spec/compiler.hpp"

namespace cemkit::spec {

core::Result<Spec> SpecCompiler::compile_json(std::string_view /*json_str*/) const {
  return core::fail(core::ErrorCode::internal_error, "not implemented");
}

core::Result<Spec> SpecCompiler::compile(const nlohmann::json& /*doc*/) const {
  return core::fail(core::ErrorCode::internal_error, "not implemented");
}

}  // namespace cemkit::spec
