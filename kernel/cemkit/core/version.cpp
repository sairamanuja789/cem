#include "cemkit/core/version.hpp"

namespace cemkit::core {

std::string_view kernel_version() noexcept { return CEMKIT_KERNEL_VERSION; }

}  // namespace cemkit::core
