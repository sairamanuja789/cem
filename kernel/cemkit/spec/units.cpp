#include "cemkit/spec/units.hpp"

namespace cemkit::spec {

std::string_view to_string(QuantityKind /*kind*/) noexcept { return {}; }
std::optional<QuantityKind> parse_quantity_kind(std::string_view /*name*/) noexcept { return std::nullopt; }

bool is_known_unit(std::string_view /*unit*/) noexcept { return false; }
std::optional<QuantityKind> kind_of_unit(std::string_view /*unit*/) noexcept { return std::nullopt; }
std::string_view coherent_si_unit(QuantityKind /*kind*/) noexcept { return {}; }

core::Result<double> convert_to_si(QuantityKind /*kind*/, std::string_view /*unit*/, double /*value*/,
                                   std::string_view /*field_name*/) {
  return core::fail(core::ErrorCode::internal_error, "not implemented");
}

core::Result<double> convert_tolerance_to_si(QuantityKind /*kind*/, std::string_view /*unit*/,
                                             double /*value*/, std::string_view /*field_name*/) {
  return core::fail(core::ErrorCode::internal_error, "not implemented");
}

}  // namespace cemkit::spec
