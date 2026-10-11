#include "products/fans/families/axial_ducted/geometry.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "cemkit/product/geometry_json.hpp"

namespace cemkit::fans::axial_ducted {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace port = geometry::port;

constexpr std::size_t k_section_points = 11;  // points per curve: a resolution choice, not data

double m(core::Length l) { return l.numerical_value_in(si::metre); }
core::Length len(double v) { return v * isq::length[si::metre]; }

core::Status fail(const std::string& message, const std::string& subject) {
  return core::fail(core::ErrorCode::geometry_failed, message, subject);
}

bool positive(double v) { return std::isfinite(v) && v > 0.0; }

core::Status validate(const FanGeometryParams& p) {
  if (p.blade_count < 1) {
    return fail("at least one blade is needed", "blade_count");
  }
  for (const auto& [value, name] :
       {std::pair{m(p.hub_radius), "hub_radius"}, std::pair{m(p.hub_length), "hub_length"},
        std::pair{m(p.duct_inner_radius), "duct_inner_radius"},
        std::pair{m(p.duct_wall), "duct_wall"}, std::pair{m(p.duct_length), "duct_length"}}) {
    if (!positive(value)) {
      return fail(std::string{name} + " must be positive and finite", name);
    }
  }
  if (p.root_fillet && !positive(m(*p.root_fillet))) {
    return fail("root_fillet must be positive and finite", "root_fillet");
  }
  if (p.stations.size() < 2) {
    return fail("at least 2 blade stations are needed", "stations");
  }
  for (std::size_t i = 0; i < p.stations.size(); ++i) {
    const auto& s = p.stations.at(i);
    const std::string prefix = std::format("stations[{}].", i);
    for (const auto& [value, name] :
         {std::pair{m(s.radius), "radius"}, std::pair{m(s.chord), "chord"},
          std::pair{m(s.thickness), "thickness"}}) {
      if (!positive(value)) {
        return fail(prefix + name + " must be positive and finite", prefix + name);
      }
    }
    const double camber = m(s.camber);
    if (!std::isfinite(camber) || camber < 0.0 || !(camber < m(s.chord) / 2.0)) {
      return fail("camber must be in [0, chord / 2)", prefix + "camber");
    }
    if (!(m(s.thickness) < m(s.chord))) {
      return fail("thickness must be less than the chord", prefix + "thickness");
    }
    if (!std::isfinite(s.stagger.numerical_value_in(si::radian))) {
      return fail("stagger must be finite", prefix + "stagger");
    }
    if (i > 0 && !(m(s.radius) > m(p.stations.at(i - 1).radius))) {
      return fail("station radii must be strictly increasing", prefix + "radius");
    }
  }
  if (!(m(p.stations.front().radius) < m(p.hub_radius))) {
    return fail("the first station must lie inside the hub so that the blade is embedded in it",
                "stations[0].radius");
  }
  if (!(m(p.stations.back().radius) < m(p.duct_inner_radius))) {
    return fail("the blade tip must lie inside the duct (positive tip clearance)",
                std::format("stations[{}].radius", p.stations.size() - 1));
  }
  // The blade must not cross the hub's end faces where it meets the hub, so that the hub keeps its
  // end edges and the junction is only on the cylindrical face.
  for (std::size_t i = 0; i < p.stations.size() && m(p.stations.at(i).radius) <= m(p.hub_radius);
       ++i) {
    const auto& s = p.stations.at(i);
    const double reach =
        (std::abs(std::cos(s.stagger.numerical_value_in(si::radian))) * m(s.chord) / 2.0) +
        m(s.camber) + m(s.thickness);
    if (!(reach < m(p.hub_length) / 2.0)) {
      return fail("the blade root reaches beyond the hub's end faces",
                  std::format("stations[{}]", i));
    }
  }
  return {};
}

port::RevolveProfile annulus(double r0, double r1, double z0, double z1) {
  return {.rz = {{len(r0), len(z0)}, {len(r1), len(z0)}, {len(r1), len(z1)}, {len(r0), len(z1)}}};
}

}  // namespace

port::SplineSection blade_section(const BladeStation& station, std::size_t points) {
  const double c = m(station.chord);
  const double h = m(station.camber);
  const double t = m(station.thickness);
  const double r = m(station.radius);
  const double st = station.stagger.numerical_value_in(si::radian);
  // Circular arc through (-c/2, 0) and (c/2, 0) with height h at mid-chord: radius
  // R = (c^2/4 + h^2) / (2h), centre (0, h - R). A flat plate for h = 0.
  const bool flat = h == 0.0;
  const double big_r = flat ? 0.0 : ((c * c / 4.0) + (h * h)) / (2.0 * h);
  port::SplineSection section;
  for (std::size_t i = 0; i < points; ++i) {
    const double u = (-c / 2.0) + (c * static_cast<double>(i) / static_cast<double>(points - 1));
    double v = 0.0;
    double nu = 0.0;
    double nv = 1.0;
    if (!flat) {
      v = std::sqrt((big_r * big_r) - (u * u)) - (big_r - h);
      nu = u / big_r;
      nv = (v + big_r - h) / big_r;
    }
    // (along chord a, along thickness b) -> (r, -a sin s + b cos s, a cos s + b sin s)
    const auto to_3d = [&](double a, double b) {
      return port::Point3{.x = len(r),
                          .y = len((-a * std::sin(st)) + (b * std::cos(st))),
                          .z = len((a * std::cos(st)) + (b * std::sin(st)))};
    };
    section.upper.push_back(to_3d(u + (nu * t / 2.0), v + (nv * t / 2.0)));
    section.lower.push_back(to_3d(u - (nu * t / 2.0), v - (nv * t / 2.0)));
  }
  return section;
}

core::Result<FanGeometry> build_fan_geometry(const port::GeometryBackend& b,
                                             const FanGeometryParams& p) {
  if (auto ok = validate(p); !ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const double half = m(p.hub_length) / 2.0;
  auto hub = b.revolve(annulus(0.0, m(p.hub_radius), -half, half));
  if (!hub) {
    return std::unexpected{std::move(hub).error()};
  }
  std::vector<port::SplineSection> sections;
  sections.reserve(p.stations.size());
  for (const auto& s : p.stations) {
    sections.push_back(blade_section(s, k_section_points));
  }
  auto blade = b.loft(sections);
  if (!blade) {
    return std::unexpected{std::move(blade).error()};
  }
  auto rotor = b.fuse_circular_pattern(**hub, **blade, p.blade_count);
  if (!rotor) {
    return std::unexpected{std::move(rotor).error()};
  }
  if (p.root_fillet) {
    // The blade-hub junction edges, selected by rule: sharp edges on the hub cylinder.
    const port::EdgesOnCylinder junction{.radius = p.hub_radius, .tolerance = len(1e-7)};
    auto filleted = b.fillet(**rotor, junction, *p.root_fillet);
    if (!filleted) {
      return std::unexpected{std::move(filleted).error()};
    }
    rotor = std::move(filleted);
  }
  const double duct_half = m(p.duct_length) / 2.0;
  auto duct = b.revolve(annulus(m(p.duct_inner_radius), m(p.duct_inner_radius) + m(p.duct_wall),
                                -duct_half, duct_half));
  if (!duct) {
    return std::unexpected{std::move(duct).error()};
  }
  return FanGeometry{.rotor = std::move(*rotor),
                     .duct = std::move(*duct),
                     .blade = std::move(*blade),
                     .tip_clearance = len(m(p.duct_inner_radius) - m(p.stations.back().radius))};
}

FanGeometryParams fan_geometry_params_of(const product::Json& a) {
  FanGeometryParams p;
  const product::Json& count = product::member(a, "blade_count");
  if (!count.is_number_unsigned()) {
    throw product::BadRequest{"blade_count", "must be a non-negative integer: blade_count"};
  }
  p.blade_count = count.get<std::size_t>();
  p.hub_radius = len(product::number(a, "hub_radius"));
  p.hub_length = len(product::number(a, "hub_length"));
  p.duct_inner_radius = len(product::number(a, "duct_inner_radius"));
  p.duct_wall = len(product::number(a, "duct_wall"));
  p.duct_length = len(product::number(a, "duct_length"));
  if (auto f = product::optional_number(a, "root_fillet")) {
    p.root_fillet = len(*f);
  }
  const product::Json& stations = product::member(a, "stations");
  if (!stations.is_array()) {
    throw product::BadRequest{"stations", "must be an array: stations"};
  }
  for (const auto& s : stations) {
    p.stations.push_back(
        BladeStation{.radius = len(product::number(s, "radius")),
                     .chord = len(product::number(s, "chord")),
                     .stagger = product::number(s, "stagger") * isq::angular_measure[si::radian],
                     .camber = len(product::number(s, "camber")),
                     .thickness = len(product::number(s, "thickness"))});
  }
  return p;
}

core::Status register_geometry_operation(product::Operations& operations) {
  using product::Json;
  return operations.add(
      "ducted_fan_geometry",
      [](const Json& c, const product::OperationContext& context) -> core::Result<Json> {
        // ADR-015 M3: rotor and duct from STATED parameters; no performance is computed.
        const auto params = fan_geometry_params_of(product::member(c, "args"));
        const auto options = product::export_options_of(c);
        auto g = build_fan_geometry(context.geometry, params);
        if (!g) {
          return std::unexpected(g.error());
        }
        // "parts" (optional): which solids to report and export, default rotor and duct; "blade"
        // is one unpatterned blade including its root station inside the hub (M5 meshing).
        std::vector<std::string> names{"rotor", "duct"};
        if (c.contains("parts")) {
          const Json& requested = c.at("parts");
          if (!requested.is_array() || requested.empty()) {
            throw product::BadRequest{"parts", "must be a non-empty array: parts"};
          }
          names.clear();
          for (const auto& n : requested) {
            if (!n.is_string() || (n != "rotor" && n != "duct" && n != "blade")) {
              throw product::BadRequest{"parts", "each part must be rotor, duct or blade: parts"};
            }
            names.push_back(n.get<std::string>());
          }
        }
        const auto solid_of = [&g](const std::string& n) {
          if (n == "rotor") {
            return g->rotor.get();
          }
          if (n == "duct") {
            return g->duct.get();
          }
          return g->blade.get();
        };
        Json parts = Json::array();
        for (const auto& name : names) {
          const geometry::port::Solid* solid = solid_of(name);
          auto report = product::solid_json(context.geometry, *solid, options);
          if (!report) {
            return std::unexpected(report.error());
          }
          Json part{{"name", name}};
          for (const auto& [key, value] : report->items()) {
            part[key] = std::move(value);
          }
          parts.push_back(std::move(part));
        }
        return Json{{"basis",
                     "geometry from stated parameters only; no performance is computed or "
                     "implied (CLAUDE.md rule 9)"},
                    {"tip_clearance", g->tip_clearance.numerical_value_in(si::metre)},
                    {"parts", std::move(parts)}};
      });
}

}  // namespace cemkit::fans::axial_ducted
