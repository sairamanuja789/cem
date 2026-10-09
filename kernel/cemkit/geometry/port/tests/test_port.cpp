#include <unistd.h>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "cemkit/core/error.hpp"
#include "cemkit/core/result.hpp"
#include "cemkit/core/sha256.hpp"
#include "cemkit/geometry/port/backend.hpp"

namespace port = cemkit::geometry::port;
using cemkit::core::ErrorCode;
using cemkit::core::Result;

namespace {

// A backend that "exports" fixed bytes, so the port's file handling is tested without an engine.
class FixedSolid final : public port::Solid {
 public:
  [[nodiscard]] std::string_view backend() const noexcept override { return "fixed"; }
};

class FixedBackend final : public port::GeometryBackend {
 public:
  explicit FixedBackend(Result<std::string> bytes) : bytes_{std::move(bytes)} {}

  [[nodiscard]] std::string_view name() const noexcept override { return "fixed"; }
  [[nodiscard]] Result<std::unique_ptr<port::Solid>> build_test_solid(
      const port::TestSolidParams& /*params*/) const override {
    return std::make_unique<FixedSolid>();
  }
  [[nodiscard]] Result<port::ValidityReport> check_validity(
      const port::Solid& /*solid*/) const override {
    return port::ValidityReport{};
  }
  [[nodiscard]] Result<port::Topology> topology(const port::Solid& /*solid*/) const override {
    return port::Topology{};
  }
  [[nodiscard]] Result<port::MassProperties> mass_properties(
      const port::Solid& /*solid*/) const override {
    return cemkit::core::fail(ErrorCode::internal_error, "not used");
  }
  [[nodiscard]] Result<std::string> export_bytes(
      const port::Solid& /*solid*/, const port::ExportRequest& /*request*/) const override {
    return bytes_;
  }

 private:
  Result<std::string> bytes_;
};

// A fresh, empty directory under the system temp directory, removed at scope exit. The process ID
// keeps concurrent test runs apart.
class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_{std::filesystem::temp_directory_path() /
              (std::string{name} + "_" + std::to_string(::getpid()))} {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;
  ~TempDir() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  // operator<< on the rdbuf rather than istreambuf_iterator: GCC 13 at -O2 reports a false
  // -Wnull-dereference inside the iterator's inlined streambuf calls.
  std::ostringstream bytes;
  bytes << in.rdbuf();
  return std::move(bytes).str();
}

std::size_t count_entries(const std::filesystem::path& directory) {
  return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{directory},
                                                std::filesystem::directory_iterator{}));
}

}  // namespace

TEST_CASE("a valid report passes require_valid", "[GEO-002]") {
  const port::ValidityReport report{.brep_valid = true,
                                    .closed = true,
                                    .manifold = true,
                                    .self_intersection_free = true,
                                    .free_edges = 0,
                                    .non_manifold_edges = 0};
  CHECK(report.ok());
  CHECK(port::require_valid(report, "solid").has_value());
}

TEST_CASE("require_valid names every failed check as geometry_failed", "[GEO-002]") {
  const port::ValidityReport report{.brep_valid = true,
                                    .closed = false,
                                    .manifold = false,
                                    .self_intersection_free = false,
                                    .free_edges = 4,
                                    .non_manifold_edges = 1};
  CHECK_FALSE(report.ok());
  const auto status = port::require_valid(report, "test_solid");
  REQUIRE_FALSE(status.has_value());
  const auto& error = status.error();
  CHECK(error.code() == ErrorCode::geometry_failed);
  CHECK(error.subject() == "test_solid");
  CHECK(error.message() ==
        "solid failed validity checks: not closed, not manifold, "
        "self-intersecting");
  CHECK(error.details().at("free_edges") == "4");
  CHECK(error.details().at("non_manifold_edges") == "1");
  CHECK_FALSE(error.details().contains("brep_valid"));
}

TEST_CASE("require_valid reports an invalid B-rep", "[GEO-002]") {
  const port::ValidityReport report{.brep_valid = false,
                                    .closed = true,
                                    .manifold = true,
                                    .self_intersection_free = true,
                                    .free_edges = 0,
                                    .non_manifold_edges = 0};
  const auto status = port::require_valid(report, "s");
  REQUIRE_FALSE(status.has_value());
  CHECK(status.error().message() == "solid failed validity checks: invalid B-rep");
}

TEST_CASE("export formats have fixed extensions", "[GEO-004]") {
  CHECK(port::extension(port::ExportFormat::step) == "step");
  CHECK(port::extension(port::ExportFormat::stl) == "stl");
}

TEST_CASE("exports are written under their sha256 name", "[GEO-004]") {
  const TempDir dir{"cemkit_geometry_port_export"};
  const std::string bytes{"solid fixed\0bytes", 17};
  const FixedBackend backend{bytes};
  const FixedSolid solid;

  const auto file = port::export_to_directory(
      backend, solid,
      port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt},
      dir.path());
  REQUIRE(file.has_value());
  CHECK(file->sha256 == cemkit::core::sha256_hex(bytes));
  CHECK(file->path == dir.path() / (file->sha256 + ".step"));
  CHECK(file->size_bytes == bytes.size());
  CHECK(read_file(file->path) == bytes);
  CHECK(count_entries(dir.path()) == 1);  // no temporary file left behind

  // Writing the same content again names the same file and leaves one file.
  const auto again = port::export_to_directory(
      backend, solid,
      port::ExportRequest{.format = port::ExportFormat::step, .tessellation = std::nullopt},
      dir.path());
  REQUIRE(again.has_value());
  CHECK(again->path == file->path);
  CHECK(count_entries(dir.path()) == 1);

  // The format decides the extension.
  const auto stl = port::export_to_directory(
      backend, solid,
      port::ExportRequest{.format = port::ExportFormat::stl, .tessellation = std::nullopt},
      dir.path());
  REQUIRE(stl.has_value());
  CHECK(stl->path == dir.path() / (file->sha256 + ".stl"));
}

TEST_CASE("export into a missing directory is an error, not an exception", "[GEO-004]") {
  const TempDir dir{"cemkit_geometry_port_missing"};
  const FixedBackend backend{std::string{"x"}};
  const FixedSolid solid;
  const auto file =
      port::export_to_directory(backend, solid, port::ExportRequest{}, dir.path() / "absent");
  REQUIRE_FALSE(file.has_value());
  CHECK(file.error().code() == ErrorCode::invalid_input);
  CHECK(file.error().subject() == "directory");
}

TEST_CASE("an export error from the backend is passed through unchanged", "[GEO-004]") {
  const TempDir dir{"cemkit_geometry_port_backend_error"};
  const FixedBackend backend{
      cemkit::core::fail(ErrorCode::geometry_failed, "engine refused", "solid")};
  const FixedSolid solid;
  const auto file = port::export_to_directory(backend, solid, port::ExportRequest{}, dir.path());
  REQUIRE_FALSE(file.has_value());
  CHECK(file.error().code() == ErrorCode::geometry_failed);
  CHECK(file.error().message() == "engine refused");
  CHECK(count_entries(dir.path()) == 0);
}
