#include "package_writer.hpp"

#include "zip_store_writer.hpp"

#include <fastxlsx/workbook.hpp>

namespace fastxlsx::detail {
namespace {

std::vector<ZipEntry> to_zip_entries(const std::vector<PackageEntry>& entries)
{
    std::vector<ZipEntry> zip_entries;
    zip_entries.reserve(entries.size());
    for (const PackageEntry& entry : entries) {
        zip_entries.push_back({entry.name, entry.data});
    }
    return zip_entries;
}

} // namespace

void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options)
{
    switch (options.backend) {
    case PackageWriterBackend::StoredZipBootstrap:
        write_stored_zip(path, to_zip_entries(entries));
        return;
    }

    throw FastXlsxError("unsupported package writer backend");
}

} // namespace fastxlsx::detail
