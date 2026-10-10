#pragma once

#include <string>

namespace cemkit::spec {

// A question for an essential unknown field (SPEC-008).
struct Question {
  std::string field;
  std::string unit;
  std::string reason;

  bool operator==(const Question&) const = default;
};

}  // namespace cemkit::spec
