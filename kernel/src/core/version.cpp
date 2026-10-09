#include "fancem/core/version.hpp"

namespace fancem::core {

std::string_view kernel_version() noexcept { return FANCEM_KERNEL_VERSION; }

}  // namespace fancem::core
