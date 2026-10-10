#pragma once

// fans.axial_ducted geometry recipe (ADR-015 M2/M3; GEO-001, GEO-002, AX-001, AX-009). Builds the
// rotor (hub fused with a circular pattern of blades) and the duct from STATED parameters only. It
// computes no performance: a valid solid is not evidence that the fan works (CLAUDE.md rule 9).
// Until the axial L1 model drives these parameters from the duty point, every value comes from the
// caller and is reported as given.
//
// Blade sections are circular-arc cambered plates of constant thickness (pure geometry: the arc
// through the leading and trailing points with the stated camber height). Each section lies in the
// plane x = radius; the chord makes the stagger angle with the hub axis (+z) and the camber bows
// towards +y at stagger 0. Planar sections are a geometric simplification of sections on cylinders.

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "cemkit/core/quantity.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/geometry/port/backend.hpp"
#include "cemkit/product/json_io.hpp"
#include "cemkit/product/operations.hpp"

namespace cemkit::fans::axial_ducted {

struct BladeStation {
  core::Length radius;
  core::Length chord;
  core::Angle stagger;  // from the hub axis
  core::Length camber;  // height of the camber arc above its chord; 0 is a flat plate
  core::Length thickness;
};

struct FanGeometryParams {
  std::size_t blade_count = 0;
  core::Length hub_radius;
  core::Length hub_length;
  // Radii strictly increasing; the first lies inside the hub (so the blade is embedded in it), the
  // last is the blade tip.
  std::vector<BladeStation> stations;
  core::Length duct_inner_radius;
  core::Length duct_wall;
  core::Length duct_length;
  std::optional<core::Length> root_fillet;  // none unless stated
};

struct FanGeometry {
  std::unique_ptr<geometry::port::Solid> rotor;
  std::unique_ptr<geometry::port::Solid> duct;
  core::Length tip_clearance;  // duct_inner_radius - tip radius
};

// geometry_failed naming the first invalid parameter (as a field path), or the two solids.
[[nodiscard]] core::Result<FanGeometry> build_fan_geometry(const geometry::port::GeometryBackend& b,
                                                           const FanGeometryParams& params);

// The parameters of a request, every length in m and angle in rad (docs/capi.md); a malformed
// request throws product::BadRequest.
[[nodiscard]] FanGeometryParams fan_geometry_params_of(const product::Json& args);

// Registers "ducted_fan_geometry" (C ABI cemkit_product_operation, docs/capi.md): the rotor and
// duct of a request, each with validity, topology, mass properties and exports.
[[nodiscard]] core::Status register_operations(product::Operations& operations);

// The blade section points (for tests): upper and lower curves, `points` each.
[[nodiscard]] geometry::port::SplineSection blade_section(const BladeStation& station,
                                                          std::size_t points);

}  // namespace cemkit::fans::axial_ducted
