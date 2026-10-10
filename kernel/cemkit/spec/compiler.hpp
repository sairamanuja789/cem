#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

#include "cemkit/core/result.hpp"
#include "cemkit/spec/options.hpp"
#include "cemkit/spec/spec.hpp"

namespace cemkit::spec {

class SpecCompiler {
 public:
  explicit SpecCompiler(CompilerOptions options = {}) : options_{std::move(options)} {}

  [[nodiscard]] core::Result<Spec> compile_json(std::string_view json_str) const;
  [[nodiscard]] core::Result<Spec> compile(const nlohmann::json& doc) const;

 private:
  CompilerOptions options_;
};

}  // namespace cemkit::spec
