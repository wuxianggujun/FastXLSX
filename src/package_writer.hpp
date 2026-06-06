#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace fastxlsx::detail {

struct PackageEntry {
    std::string name;
    std::string data;
};

enum class PackageWriterBackend {
    Auto,
    StoredZipBootstrap,
    MinizipNg,
};

struct PackageWriterOptions {
    PackageWriterBackend backend = PackageWriterBackend::Auto;
};

// Internal package writer boundary. Auto selects the production minizip-ng
// backend when the dependency is enabled; otherwise it keeps the Phase 1
// stored/no-compression ZIP bootstrap for dependency-free builds.
void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options = {});

} // namespace fastxlsx::detail
