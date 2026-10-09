#include "cemkit/geometry/port/backend.hpp"

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/sha256.hpp"

namespace cemkit::geometry::port {

core::Status require_valid(const ValidityReport& report, std::string_view subject) {
  if (report.ok()) {
    return {};
  }
  std::vector<std::string_view> failed;
  if (!report.brep_valid) {
    failed.emplace_back("invalid B-rep");
  }
  if (!report.closed) {
    failed.emplace_back("not closed");
  }
  if (!report.manifold) {
    failed.emplace_back("not manifold");
  }
  if (!report.self_intersection_free) {
    failed.emplace_back("self-intersecting");
  }
  std::string message = "solid failed validity checks: ";
  const char* separator = "";
  for (const auto reason : failed) {
    message += separator;
    message += reason;
    separator = ", ";
  }
  core::ErrorDetails details;
  if (report.free_edges > 0) {
    details.emplace("free_edges", std::to_string(report.free_edges));
  }
  if (report.non_manifold_edges > 0) {
    details.emplace("non_manifold_edges", std::to_string(report.non_manifold_edges));
  }
  return core::fail(core::ErrorCode::geometry_failed, std::move(message), std::string{subject},
                    std::move(details));
}

std::string_view extension(ExportFormat format) noexcept {
  switch (format) {
    case ExportFormat::step:
      return "step";
    case ExportFormat::stl:
      return "stl";
  }
  return "bin";  // unreachable: every enumerator is handled above
}

core::Result<ExportedFile> export_to_directory(const GeometryBackend& backend, const Solid& solid,
                                               const ExportRequest& request,
                                               const std::filesystem::path& directory) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec)) {
    return core::fail(core::ErrorCode::invalid_input, "export directory does not exist",
                      "directory", {{"path", directory.string()}});
  }
  auto bytes = backend.export_bytes(solid, request);
  if (!bytes) {
    return std::unexpected{std::move(bytes).error()};
  }

  ExportedFile file;
  file.sha256 = core::sha256_hex(*bytes);
  file.size_bytes = bytes->size();
  const std::string name = file.sha256 + "." + std::string{extension(request.format)};
  file.path = directory / name;
  // The temporary name is derived from the content, so two writers of different content never
  // share it, and two writers of the same content write the same bytes.
  const std::filesystem::path partial = directory / (name + ".partial");

  const auto write_failed = [&](std::string_view what) {
    std::filesystem::remove(partial, ec);
    return core::fail(core::ErrorCode::geometry_failed, std::string{what}, "export",
                      {{"path", file.path.string()}});
  };
  {
    std::ofstream out{partial, std::ios::binary | std::ios::trunc};
    if (!out) {
      return write_failed("export file could not be opened for writing");
    }
    out.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    out.close();
    if (!out) {
      return write_failed("export file could not be written");
    }
  }
  std::filesystem::rename(partial, file.path, ec);
  if (ec) {
    return write_failed("export file could not be moved into place");
  }
  return file;
}

}  // namespace cemkit::geometry::port
