#pragma once

#include <string>
#include <string_view>

namespace cemkit::core {

// SHA-256 (FIPS 180-4) of a byte string, as 64 lowercase hexadecimal characters. Used to name
// content-addressed artifacts (GEO-004); the Python store uses the same digest (hashlib.sha256).
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

}  // namespace cemkit::core
