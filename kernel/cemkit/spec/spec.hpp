#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "cemkit/core/provenance.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/spec/field.hpp"
#include "cemkit/spec/question.hpp"

namespace cemkit::spec {

struct SpecRef {
  std::string spec_id;
  std::uint32_t revision{1};

  bool operator==(const SpecRef&) const = default;
};

struct ProvenanceConflict {
  std::string field;
  core::Provenance winning{core::Provenance::unknown};
  core::Provenance overridden{core::Provenance::unknown};

  bool operator==(const ProvenanceConflict&) const = default;
};

class Spec {
 public:
  Spec() = default;

  [[nodiscard]] const std::string& schema_version() const noexcept { return schema_version_; }
  [[nodiscard]] const std::string& spec_id() const noexcept { return spec_id_; }
  [[nodiscard]] std::uint32_t revision() const noexcept { return revision_; }
  [[nodiscard]] const std::optional<SpecRef>& parent() const noexcept { return parent_; }
  [[nodiscard]] const std::string& family() const noexcept { return family_; }
  [[nodiscard]] const std::string& title() const noexcept { return title_; }

  [[nodiscard]] const std::map<std::string, Field>& fields() const noexcept { return fields_; }
  [[nodiscard]] std::optional<Field> field(std::string_view path) const;

  [[nodiscard]] const std::vector<Question>& questions() const noexcept { return questions_; }
  [[nodiscard]] const std::vector<ProvenanceConflict>& conflicts() const noexcept {
    return conflicts_;
  }

  [[nodiscard]] bool has_unresolved_essential_unknowns() const noexcept {
    return !questions_.empty();
  }

  // Contradiction checks (SPEC-010): e.g. required air power exceeds stated power limit.
  [[nodiscard]] core::Result<void> check_contradictions() const;

  // Derives a new immutable revision linked to this one as parent (SPEC-009).
  [[nodiscard]] core::Result<Spec> derive_new_revision(
      const nlohmann::json& modifications) const;

  [[nodiscard]] const nlohmann::json& raw_document() const noexcept { return raw_document_; }
  [[nodiscard]] nlohmann::json to_json() const;

 private:
  friend class SpecCompiler;

  std::string schema_version_{"1.0.0"};
  std::string spec_id_;
  std::uint32_t revision_{1};
  std::optional<SpecRef> parent_;
  std::string family_;
  std::string title_;

  std::map<std::string, Field> fields_;
  std::vector<Question> questions_;
  std::vector<ProvenanceConflict> conflicts_;
  nlohmann::json raw_document_;
};

}  // namespace cemkit::spec
