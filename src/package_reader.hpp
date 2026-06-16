#pragma once

#include <fastxlsx/detail/opc.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

struct PackageReaderEntry {
    std::string name;
    std::uint16_t compression_method = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t local_header_offset = 0;
    std::uint64_t data_offset = 0;
};

struct WorkbookSheetReference {
    std::string name;
    std::string sheet_id;
    std::string relationship_id;
    PartName part_name;
};

using PackageReaderChunkCallback = std::function<bool(std::string& output_chunk)>;

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
[[nodiscard]] std::string testing_read_entry_chunks_to_string(
    PackageReaderChunkCallback source, std::uint64_t expected_size);
void testing_extract_entry_chunks_to_committed_file(
    const std::filesystem::path& package_path, PackageReaderChunkCallback source,
    const std::filesystem::path& output_path, std::uint64_t expected_size);
#endif

// Internal PackageReader foundation for the Patch path.
//
// This first slice indexes stored/no-compression ZIP entries, and can also
// read DEFLATE entries when the opt-in minizip-ng dependency is enabled. It
// requires ZIP header sizes/CRC and rejects unsupported compression methods,
// encrypted entries, data-descriptor entries, and local-header
// CRC/method/name/size mismatches.
// It is enough to prove that existing package entries, including unknown parts,
// can be discovered and read without turning the package into a workbook DOM.
// It also ingests the small OPC metadata parts `[Content_Types].xml` and
// `.rels` into internal PartIndex / RelationshipGraph inputs, rejecting
// conflicting content type defaults/overrides and malformed relationship
// metadata such as duplicate ids within one owner. It is not a public editing
// API, a PackageEditor, Zip64 support, or a preservation / copy-write pipeline
// by itself.
class PackageReader {
public:
    [[nodiscard]] static PackageReader open(std::filesystem::path path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] const std::vector<PackageReaderEntry>& entries() const noexcept;
    [[nodiscard]] const PackageReaderEntry* find_entry(std::string_view name) const noexcept;
    [[nodiscard]] std::string read_entry(std::string_view name) const;
    [[nodiscard]] PackageReaderChunkCallback entry_chunk_source(std::string_view name)
        const;
    void extract_entry_to_file(std::string_view name, const std::filesystem::path& output_path)
        const;
    [[nodiscard]] const ContentTypesManifest& content_types() const noexcept;
    [[nodiscard]] const PartIndex& part_index() const noexcept;
    [[nodiscard]] const RelationshipSet& package_relationships() const noexcept;
    [[nodiscard]] const RelationshipSet* relationships_for(
        const PartName& source_part) const noexcept;
    [[nodiscard]] RelationshipGraph relationship_graph() const;
    [[nodiscard]] std::vector<WorkbookSheetReference> workbook_sheets() const;
    [[nodiscard]] std::vector<WorkbookSheetReference> workbook_sheets_from_xml(
        std::string_view workbook_xml) const;
    [[nodiscard]] PartName worksheet_part_by_sheet_name(std::string_view sheet_name) const;
    [[nodiscard]] PartName worksheet_part_by_sheet_name_from_xml(
        std::string_view sheet_name, std::string_view workbook_xml) const;

private:
    PackageReader() = default;

    std::filesystem::path path_;
    std::vector<PackageReaderEntry> entries_;
    ContentTypesManifest content_types_;
    PartIndex part_index_;
    RelationshipSet package_relationships_;
    std::vector<std::pair<PartName, RelationshipSet>> part_relationships_;
};

} // namespace fastxlsx::detail
