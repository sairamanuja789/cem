#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

#include "cemkit/core/result.hpp"
#include "cemkit/spec/essential.hpp"
#include "cemkit/spec/spec.hpp"

namespace cemkit::spec {

struct CompilerOptions {
  bool autonomous_mode{false};
  EssentialFieldResolver essential_resolver{default_essential_fields};
};

class SpecCompiler {
 public:
  explicit SpecCompiler(CompilerOptions options = {}) : options_{std::move(options)} {}

  [[nodiscard]] core::Result<Spec> compile_json(std::string_view json_str) const;
  [[nodiscard]] core::Result<Spec> compile(const nlohmann::json& doc) const;

 private:
  CompilerOptions options_;
};

}  // namespace cemkit::spec
