#include "cemkit/product/geometry_json.hpp"

#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>

#include <cstddef>
#include <string>
#include <utility>

#include "cemkit/core/sha256.hpp"

namespace cemkit::product {

namespace {

namespace si = mp_units::si;
namespace isq = mp_units::isq;
namespace port = geometry::port;

core::Length length_of(double v) { return v * isq::length[si::metre]; }

}  // namespace

// RFC 4648 base64 (standard alphabet, with padding), for export bytes in a JSON response.
std::string base64(std::string_view bytes) {
  static constexpr std::string_view k_alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const auto n = (static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U) |
                   (static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1])) << 8U) |
                   static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 2]));
    out += k_alphabet[(n >> 18U) & 63U];
    out += k_alphabet[(n >> 12U) & 63U];
    out += k_alphabet[(n >> 6U) & 63U];
    out += k_alphabet[n & 63U];
  }
  if (i < bytes.size()) {
    auto n = static_cast<unsigned>(static_cast<unsigned char>(bytes[i])) << 16U;
    const bool two = i + 1 < bytes.size();
    if (two) {
      n |= static_cast<unsigned>(static_cast<unsigned char>(bytes[i + 1])) << 8U;
    }
    out += k_alphabet[(n >> 18U) & 63U];
    out += k_alphabet[(n >> 12U) & 63U];
    out += two ? k_alphabet[(n >> 6U) & 63U] : '=';
    out += '=';
  }
  return out;
}

port::ExportRequest export_request_of(const Json& e) {
  const std::string format = text(e, "format");
  if (format == "step") {
    return port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt};
  }
  if (format == "stl") {
    return port::ExportRequest{.format = port::ExportFormat::stl,
                               .tessellation = port::Tessellation{
                                   .linear_deflection = length_of(number(e, "linear_deflection")),
                                   .angular_deflection = number(e, "angular_deflection") *
                                                         isq::angular_measure[si::radian]}};
  }
  throw BadRequest{"format", "unknown export format: " + format};
}

ExportOptions export_options_of(const Json& request) {
  ExportOptions options;
  if (request.contains("exports")) {
    if (!request["exports"].is_array()) {
      throw BadRequest{"exports", "must be an array: exports"};
    }
    for (const auto& e : request["exports"]) {
      options.exports.push_back(export_request_of(e));
    }
  }
  if (request.contains("include_data")) {
    if (!request["include_data"].is_boolean()) {
      throw BadRequest{"include_data", "must be a boolean: include_data"};
    }
    options.include_data = request["include_data"].get<bool>();
  }
  return options;
}

core::Result<Json> solid_json(const port::GeometryBackend& backend, const port::Solid& solid,
                              const ExportOptions& options) {
  auto validity = backend.check_validity(solid);
  auto topology = backend.topology(solid);
  auto mass = backend.mass_properties(solid);
  if (!validity) {
    return std::unexpected(validity.error());
  }
  if (!topology) {
    return std::unexpected(topology.error());
  }
  if (!mass) {
    return std::unexpected(mass.error());
  }
  Json files = Json::array();
  for (const auto& e : options.exports) {
    auto bytes = backend.export_bytes(solid, e);
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    Json file{{"format", std::string{port::extension(e.format)}},
              {"sha256", core::sha256_hex(*bytes)},
              {"size_bytes", bytes->size()}};
    if (options.include_data) {
      file["data_base64"] = base64(*bytes);
    }
    files.push_back(std::move(file));
  }
  const auto& v = *validity;
  const auto& t = *topology;
  const auto& m = *mass;
  return Json{{"validity", Json{{"ok", v.ok()},
                                {"brep_valid", v.brep_valid},
                                {"closed", v.closed},
                                {"manifold", v.manifold},
                                {"self_intersection_free", v.self_intersection_free},
                                {"free_edges", v.free_edges},
                                {"non_manifold_edges", v.non_manifold_edges}}},
              {"topology", Json{{"solids", t.solids},
                                {"shells", t.shells},
                                {"faces", t.faces},
                                {"edges", t.edges},
                                {"vertices", t.vertices}}},
              {"mass_properties",
               Json{{"volume", m.volume.numerical_value_in(mp_units::cubic(si::metre))},
                    {"area", m.area.numerical_value_in(mp_units::square(si::metre))},
                    {"centroid", Json::array({m.centroid[0].numerical_value_in(si::metre),
                                              m.centroid[1].numerical_value_in(si::metre),
                                              m.centroid[2].numerical_value_in(si::metre)})}}},
              {"exports", std::move(files)}};
}

}  // namespace cemkit::product
