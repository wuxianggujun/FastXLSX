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
    StoredZipBootstrap,
};

struct PackageWriterOptions {
    PackageWriterBackend backend = PackageWriterBackend::StoredZipBootstrap;
};

// Internal package writer boundary. The current backend still writes the
// Phase 1 stored/no-compression ZIP bootstrap; production ZIP support should
// replace the backend behind this function instead of calling ZIP code from
// workbook writers directly.
void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options = {});

} // namespace fastxlsx::detail
