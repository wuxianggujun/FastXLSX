#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

struct PackageEntryChunk {
    enum class Kind {
        Memory,
        File,
    };

    Kind kind = Kind::Memory;
    std::string data;
    std::filesystem::path path;

    [[nodiscard]] static PackageEntryChunk memory(std::string value)
    {
        PackageEntryChunk chunk;
        chunk.kind = Kind::Memory;
        chunk.data = std::move(value);
        return chunk;
    }

    [[nodiscard]] static PackageEntryChunk file(std::filesystem::path value)
    {
        PackageEntryChunk chunk;
        chunk.kind = Kind::File;
        chunk.path = std::move(value);
        return chunk;
    }
};

struct PackageEntry {
    std::string name;
    std::string data;
    std::vector<PackageEntryChunk> chunks;

    PackageEntry() = default;

    PackageEntry(std::string entry_name, std::string entry_data)
        : name(std::move(entry_name))
        , data(std::move(entry_data))
    {
    }

    PackageEntry(std::string entry_name, std::vector<PackageEntryChunk> entry_chunks)
        : name(std::move(entry_name))
        , chunks(std::move(entry_chunks))
    {
    }
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
