#include "package_writer.hpp"

#include "zip_store_writer.hpp"

#include <fastxlsx/workbook.hpp>

#ifdef FASTXLSX_HAS_MINIZIP_NG
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#endif

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>

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

PackageWriterBackend resolve_backend(PackageWriterBackend backend)
{
    if (backend != PackageWriterBackend::Auto) {
        return backend;
    }

#ifdef FASTXLSX_HAS_MINIZIP_NG
    return PackageWriterBackend::MinizipNg;
#else
    return PackageWriterBackend::StoredZipBootstrap;
#endif
}

#ifdef FASTXLSX_HAS_MINIZIP_NG

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void check_minizip_result(int result, const char* operation)
{
    if (result != MZ_OK) {
        throw FastXlsxError(std::string("minizip-ng failed to ") + operation + " (error "
            + std::to_string(result) + ")");
    }
}

struct MinizipWriterDeleter {
    void operator()(void* writer) const
    {
        if (writer != nullptr) {
            mz_zip_writer_delete(&writer);
        }
    }
};

void write_minizip_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries)
{
    if (entries.empty()) {
        throw FastXlsxError("cannot write an empty ZIP package");
    }

    std::unique_ptr<void, MinizipWriterDeleter> writer(mz_zip_writer_create());
    if (!writer) {
        throw FastXlsxError("failed to create minizip-ng writer");
    }

    mz_zip_writer_set_compress_method(writer.get(), MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer.get(), MZ_COMPRESS_LEVEL_DEFAULT);

    const std::string output_path = path_to_utf8(path);
    check_minizip_result(mz_zip_writer_open_file(writer.get(), output_path.c_str(), 0, 0),
        "open XLSX package");

    try {
        for (const PackageEntry& entry : entries) {
            if (entry.name.empty()) {
                throw FastXlsxError("ZIP entry name cannot be empty");
            }

            mz_zip_file file_info {};
            file_info.filename = entry.name.c_str();
            file_info.flag = MZ_ZIP_FLAG_UTF8;
            file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
            file_info.modified_date = 0;
            file_info.uncompressed_size = static_cast<std::int64_t>(entry.data.size());

            check_minizip_result(mz_zip_writer_entry_open(writer.get(), &file_info),
                "open ZIP entry");

            std::size_t offset = 0;
            while (offset < entry.data.size()) {
                const std::size_t remaining = entry.data.size() - offset;
                const int chunk_size = static_cast<int>(std::min<std::size_t>(
                    remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
                const int written =
                    mz_zip_writer_entry_write(writer.get(), entry.data.data() + offset, chunk_size);
                if (written != chunk_size) {
                    throw FastXlsxError("minizip-ng failed to write ZIP entry data");
                }
                offset += static_cast<std::size_t>(written);
            }

            check_minizip_result(mz_zip_writer_entry_close(writer.get()), "close ZIP entry");
        }

        check_minizip_result(mz_zip_writer_close(writer.get()), "close XLSX package");
    } catch (...) {
        (void)mz_zip_writer_close(writer.get());
        throw;
    }
}

#endif

} // namespace

void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options)
{
    switch (resolve_backend(options.backend)) {
    case PackageWriterBackend::StoredZipBootstrap:
        write_stored_zip(path, to_zip_entries(entries));
        return;
    case PackageWriterBackend::MinizipNg:
#ifdef FASTXLSX_HAS_MINIZIP_NG
        write_minizip_package(path, entries);
        return;
#else
        throw FastXlsxError("minizip-ng package writer backend is not enabled");
#endif
    case PackageWriterBackend::Auto:
        break;
    }

    throw FastXlsxError("unsupported package writer backend");
}

} // namespace fastxlsx::detail
