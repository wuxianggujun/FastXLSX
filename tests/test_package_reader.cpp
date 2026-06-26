#include "test_package_reader_common.hpp"

void test_package_writer_rejects_empty_package_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-empty-package.xlsx");
    const std::string sentinel = "preserve existing empty-package output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, {},
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "empty ZIP package",
            "empty package failure should explain the missing entries");
    }

    check(failed, "PackageWriter should reject empty ZIP packages");
    check(fastxlsx::test::read_file(path) == sentinel,
        "empty package should fail before overwriting output");
}

void test_package_writer_rejects_invalid_compression_levels_before_output()
{
    auto check_invalid_level = [](int compression_level, std::string_view output_name) {
        const std::filesystem::path path = output_path(output_name);
        const std::string sentinel = "preserve existing invalid-compression output";
        write_file(path, sentinel);

        fastxlsx::detail::PackageWriterOptions options;
        options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
        options.compression_level = compression_level;

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {"xl/workbook.xml", "<workbook/>"},
                },
                options);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "ZIP compression level",
                "invalid compression level failure should explain the bad option");
        }

        check(failed, "PackageWriter should reject invalid compression levels");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid compression level should fail before overwriting output");
    };

    check_invalid_level(fastxlsx::detail::package_writer_default_compression_level - 1,
        "fastxlsx-package-writer-invalid-compression-low.xlsx");
    check_invalid_level(fastxlsx::detail::package_writer_max_compression_level + 1,
        "fastxlsx-package-writer-invalid-compression-high.xlsx");
}

void test_package_writer_rejects_zip64_entry_count_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-zip64-entry-count.xlsx");
    const std::string sentinel = "preserve existing zip64-entry-count output";
    write_file(path, sentinel);

    std::vector<fastxlsx::detail::PackageEntry> entries;
    entries.reserve(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u);
    for (std::uint32_t index = 0;
         index <= static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
         ++index) {
        entries.emplace_back("xl/entry" + std::to_string(index) + ".xml", "");
    }

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, entries,
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "Zip64",
            "entry-count failure should explain Zip64 requirement");
    }

    check(failed, "PackageWriter should reject ZIP32 entry count overflow");
    check(fastxlsx::test::read_file(path) == sentinel,
        "entry-count overflow should fail before overwriting output");
}

void test_package_writer_rejects_zip_entry_name_length_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-entry-name-length.xlsx");
    const std::string sentinel = "preserve existing entry-name-length output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {std::string(static_cast<std::size_t>(
                     std::numeric_limits<std::uint16_t>::max()) + 1u,
                     'a'),
                    ""},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "entry name length",
            "entry-name failure should explain the ZIP field limit");
    }

    check(failed, "PackageWriter should reject entry names beyond ZIP field size");
    check(fastxlsx::test::read_file(path) == sentinel,
        "entry-name overflow should fail before overwriting output");
}

void test_package_writer_rejects_invalid_entry_names_before_output()
{
    struct InvalidEntryNameCase {
        std::string_view suffix;
        std::string entry_name;
    };

    const std::vector<InvalidEntryNameCase> cases = {
        {"empty", ""},
        {"absolute", "/xl/workbook.xml"},
        {"trailing-slash", "xl/workbook.xml/"},
        {"empty-segment", "xl//workbook.xml"},
        {"dot-segment", "xl/./workbook.xml"},
        {"parent-segment", "xl/../workbook.xml"},
        {"backslash", R"(xl\workbook.xml)"},
        {"query", "xl/workbook.xml?version=1"},
        {"fragment", "xl/workbook.xml#sheet"},
        {"null-byte", std::string("xl/workbook\0.xml", 16)},
    };

    for (const InvalidEntryNameCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-writer-invalid-entry-name-"
            + std::string(test_case.suffix) + ".xlsx");
        const std::string sentinel = "preserve existing invalid-entry-name output";
        write_file(path, sentinel);

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {test_case.entry_name, "<payload/>"},
                },
                {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "ZIP entry name",
                "invalid entry-name failure should explain the ZIP name constraint");
        }

        check(failed, "PackageWriter should reject invalid ZIP entry names");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid entry-name failure should fail before overwriting output");
    }
}

void test_package_writer_rejects_duplicate_entry_names_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-duplicate-entry-name.xlsx");
    const std::string sentinel = "preserve existing duplicate-entry-name output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/workbook.xml", "<workbook/>"},
                {"xl/workbook.xml", "<duplicate/>"},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "duplicate ZIP entry name",
            "duplicate entry-name failure should explain the conflicting ZIP entry");
    }

    check(failed, "PackageWriter should reject duplicate ZIP entry names");
    check(fastxlsx::test::read_file(path) == sentinel,
        "duplicate entry-name failure should fail before overwriting output");
}

void test_package_writer_rejects_zip64_file_chunk_before_output()
{
    const std::filesystem::path chunk_path =
        output_path("fastxlsx-package-writer-zip64-large-chunk.bin");
    create_sparse_file_with_size(
        chunk_path, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u);

    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-zip64-large-chunk.xlsx");
    const std::string sentinel = "preserve existing zip64-large-chunk output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/large.bin",
                    std::vector<fastxlsx::detail::PackageEntryChunk> {
                        fastxlsx::detail::PackageEntryChunk::file(chunk_path)}},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "Zip64",
            "large file-backed chunk failure should explain Zip64 requirement");
    }

    std::error_code remove_error;
    std::filesystem::remove(chunk_path, remove_error);

    check(failed, "PackageWriter should reject file-backed chunks requiring Zip64");
    check(fastxlsx::test::read_file(path) == sentinel,
        "Zip64-sized file chunk should fail before overwriting output");
}

void test_package_writer_rejects_missing_file_chunk_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-missing-file-chunk.xlsx");
    const std::string sentinel = "preserve existing missing-file-chunk output";
    write_file(path, sentinel);
    const std::filesystem::path missing_chunk_path = path / "missing.bin";

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/missing.bin",
                    std::vector<fastxlsx::detail::PackageEntryChunk> {
                        fastxlsx::detail::PackageEntryChunk::file(missing_chunk_path)}},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "file-backed ZIP entry chunk",
            "missing file-backed chunk failure should explain the bad chunk path");
    }

    check(failed, "PackageWriter should reject missing file-backed chunks");
    check(fastxlsx::test::read_file(path) == sentinel,
        "missing file-backed chunk should fail before overwriting output");
}

void test_package_writer_rejects_mixed_legacy_data_and_chunks_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-mixed-data-and-chunks.xlsx");
    const std::string sentinel = "preserve existing mixed-data-and-chunks output";
    write_file(path, sentinel);

    fastxlsx::detail::PackageEntry entry("xl/mixed.xml", "<legacy/>");
    entry.chunks.push_back(
        fastxlsx::detail::PackageEntryChunk::memory("<chunked/>"));

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, {entry},
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "chunked payload",
            "mixed payload failure should explain the conflicting entry sources");
    }

    check(failed, "PackageWriter should reject entries that mix data and chunks");
    check(fastxlsx::test::read_file(path) == sentinel,
        "mixed data/chunks should fail before overwriting output");
}

void test_package_writer_rejects_invalid_chunk_sources_before_output()
{
    const std::filesystem::path file_chunk_path =
        output_path("fastxlsx-package-writer-invalid-chunk-source.bin");
    write_file(file_chunk_path, "file-backed chunk payload");

    struct InvalidChunkCase {
        std::string_view suffix;
        fastxlsx::detail::PackageEntryChunk chunk;
        std::string_view expected_message;
    };

    fastxlsx::detail::PackageEntryChunk memory_with_path =
        fastxlsx::detail::PackageEntryChunk::memory("<memory/>");
    memory_with_path.path = file_chunk_path;

    fastxlsx::detail::PackageEntryChunk file_with_data =
        fastxlsx::detail::PackageEntryChunk::file(file_chunk_path);
    file_with_data.data = "<ignored-memory/>";

    fastxlsx::detail::PackageEntryChunk unknown_kind =
        fastxlsx::detail::PackageEntryChunk::memory("<unknown/>");
    unknown_kind.kind = static_cast<fastxlsx::detail::PackageEntryChunk::Kind>(99);

    const std::vector<InvalidChunkCase> cases = {
        {"memory-with-path", memory_with_path, "memory and file sources"},
        {"file-with-data", file_with_data, "memory and file sources"},
        {"unknown-kind", unknown_kind, "unsupported ZIP entry chunk kind"},
    };

    for (const InvalidChunkCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-writer-invalid-chunk-source-"
            + std::string(test_case.suffix) + ".xlsx");
        const std::string sentinel = "preserve existing invalid-chunk-source output";
        write_file(path, sentinel);

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {"xl/chunk-source.xml",
                        std::vector<fastxlsx::detail::PackageEntryChunk> {test_case.chunk}},
                },
                {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "ZIP entry 'xl/chunk-source.xml' chunk 0",
                "invalid chunk-source failure should identify the entry and chunk");
            check_contains(error.what(), test_case.expected_message,
                "invalid chunk-source failure should explain the bad chunk state");
        }

        check(failed, "PackageWriter should reject invalid chunk source state");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid chunk source should fail before overwriting output");
    }

    std::error_code remove_error;
    std::filesystem::remove(file_chunk_path, remove_error);
}

void test_stored_zip_backend_contextualizes_actual_chunk_failures()
{
    const std::filesystem::path file_chunk_path =
        output_path("fastxlsx-stored-zip-backend-actual-chunk-context.bin");
    const std::string original_body = "file-backed stored backend chunk";
    write_file(file_chunk_path, original_body);

    fastxlsx::detail::PackageEntryChunk file_chunk =
        fastxlsx::detail::PackageEntryChunk::file(file_chunk_path);
    file_chunk.has_expected_size = true;
    file_chunk.expected_size = static_cast<std::uint64_t>(original_body.size());
    write_file(file_chunk_path, original_body + "-extended-after-validation");

    const std::filesystem::path path =
        output_path("fastxlsx-stored-zip-backend-actual-chunk-context.xlsx");

    bool failed = false;
    try {
        fastxlsx::detail::write_stored_zip(path,
            {
                {"xl/chunk-source.xml",
                    std::vector<fastxlsx::detail::PackageEntryChunk> {
                        fastxlsx::detail::PackageEntryChunk::memory("<prefix/>"),
                        file_chunk}},
            });
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "ZIP entry 'xl/chunk-source.xml' chunk 1",
            "stored backend chunk failure should identify the output entry and chunk");
        check_contains(error.what(), file_chunk_path.filename().generic_string(),
            "stored backend chunk failure should include the file-backed chunk path");
        check_contains(error.what(), "ZIP entry chunk size changed after staging",
            "stored backend chunk failure should preserve the size-contract detail");
        check_contains(error.what(),
            std::string("expected ") + std::to_string(original_body.size()) + " bytes",
            "stored backend chunk failure should report expected bytes");
        check_contains(error.what(),
            std::string("actual ") + std::to_string(
                std::string(original_body + "-extended-after-validation").size())
                + " bytes",
            "stored backend chunk failure should report the actual bytes");
    }

    std::error_code remove_error;
    std::filesystem::remove(file_chunk_path, remove_error);

    check(failed, "stored ZIP backend should reject changed file-backed chunks");
}

void test_package_reader_reads_stored_entries_and_unknown_parts()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-stored.xlsx");
    const std::string unknown_body("raw\0bytes", 9);

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    check(reader.entries().size() == 4, "PackageReader entry count mismatch");
    check(reader.path() == path, "PackageReader should retain the source path");

    const auto* content_types = reader.find_entry("[Content_Types].xml");
    check(content_types != nullptr, "PackageReader should find [Content_Types].xml");
    check(content_types->compression_method == 0, "stored entry method mismatch");
    check(content_types->uncompressed_size == 8, "content types entry size mismatch");
    check(reader.read_entry("[Content_Types].xml") == "<Types/>",
        "content types entry body mismatch");

    const auto* unknown = reader.find_entry("custom/unknown.bin");
    check(unknown != nullptr, "PackageReader should find unknown entries");
    check(unknown->compressed_size == unknown_body.size(), "unknown entry size mismatch");
    check(reader.read_entry("custom/unknown.bin") == unknown_body,
        "unknown entry bytes should be readable");

    check(reader.find_entry("xl/missing.xml") == nullptr,
        "missing entries should not be found");
    check(reader.part_index().size() == 2,
        "PackageReader should index non-metadata package parts");
    check(reader.part_index().find_part(fastxlsx::detail::PartName("/xl/workbook.xml"))
            != nullptr,
        "PackageReader should index workbook part even without content type metadata");
    const auto* unknown_part =
        reader.part_index().find_part(fastxlsx::detail::PartName("/custom/unknown.bin"));
    check(unknown_part != nullptr, "PackageReader should index unknown package parts");
    check(unknown_part->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageReader indexed parts should default to copy-original planning state");
    check(reader.package_relationships().empty(),
        "empty package relationships should remain empty");

    bool missing_read_failed = false;
    try {
        (void)reader.read_entry("xl/missing.xml");
    } catch (const std::exception&) {
        missing_read_failed = true;
    }
    check(missing_read_failed, "reading a missing entry should fail");
}

void test_package_reader_extracts_stored_entry_to_file()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-extract.xlsx");
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "stored-entry-streaming-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const std::filesystem::path fresh_extracted =
        output_path("fastxlsx-package-reader-extracted-unknown-fresh.bin");
    std::error_code ignored;
    std::filesystem::remove(fresh_extracted, ignored);
    reader.extract_entry_to_file("custom/unknown.bin", fresh_extracted);
    check(fastxlsx::test::read_file(fresh_extracted) == unknown_body,
        "PackageReader should extract stored entries to a fresh output path");

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-extracted-unknown.bin");
    write_file(extracted, "stale extraction output");
    reader.extract_entry_to_file("custom/unknown.bin", extracted);

    check(fastxlsx::test::read_file(extracted) == unknown_body,
        "PackageReader should atomically replace stored extraction output with entry bytes");
}

void test_package_reader_rejects_extracting_to_directory()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-extract-to-directory.xlsx");
    const std::string unknown_body = "opaque unknown bytes";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const std::filesystem::path directory_output =
        output_path("fastxlsx-package-reader-extract-directory-output");
    std::filesystem::create_directories(directory_output);
    const std::filesystem::path sentinel = directory_output / "sentinel.txt";
    write_file(sentinel, "preserve extraction output directory");

    bool failed = false;
    try {
        reader.extract_entry_to_file("custom/unknown.bin", directory_output);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "output path cannot be a directory",
            "extract-to-directory should explain the invalid output target");
    }
    check(failed, "PackageReader should reject directory extraction output targets");
    check(std::filesystem::is_directory(directory_output),
        "failed extract-to-directory should preserve the output directory");
    check(fastxlsx::test::read_file(sentinel) == "preserve extraction output directory",
        "failed extract-to-directory should preserve existing directory contents");
}

void test_package_reader_rejects_extracting_to_invalid_parent_before_read()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-extract-parent-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string corrupt_source_bytes = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(corrupt_source_bytes, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-extract-parent-corrupt.xlsx");
    write_file(path, corrupt_source_bytes);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const std::filesystem::path missing_parent =
        output_path("fastxlsx-package-reader-extract-missing-parent");
    std::error_code ignored;
    std::filesystem::remove_all(missing_parent, ignored);
    const std::filesystem::path missing_parent_output = missing_parent / "entry.bin";

    bool missing_parent_failed = false;
    try {
        reader.extract_entry_to_file("custom/unknown.bin", missing_parent_output);
    } catch (const std::exception& error) {
        missing_parent_failed = true;
        check_contains(error.what(), "parent path must be an existing directory",
            "extract-to-missing-parent should fail before reading entry chunks");
    }
    check(missing_parent_failed,
        "PackageReader should reject extraction output with a missing parent");
    check(!std::filesystem::exists(missing_parent_output),
        "failed extract-to-missing-parent should not create the output file");

    const std::filesystem::path file_parent =
        output_path("fastxlsx-package-reader-extract-file-parent.bin");
    write_file(file_parent, "not a directory");
    const std::filesystem::path file_parent_output = file_parent / "entry.bin";

    bool file_parent_failed = false;
    try {
        reader.extract_entry_to_file("custom/unknown.bin", file_parent_output);
    } catch (const std::exception& error) {
        file_parent_failed = true;
        check_contains(error.what(), "parent path must be an existing directory",
            "extract-to-file-parent should fail before reading entry chunks");
    }
    check(file_parent_failed,
        "PackageReader should reject extraction output with a non-directory parent");
    check(fastxlsx::test::read_file(file_parent) == "not a directory",
        "failed extract-to-file-parent should preserve the existing parent file");
}

void test_package_reader_rejects_extracting_over_source_package()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-extract-over-source.xlsx");
    const std::string unknown_body = "opaque unknown bytes";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::string source_bytes = fastxlsx::test::read_file(path);
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool direct_failed = false;
    try {
        reader.extract_entry_to_file("custom/unknown.bin", path);
    } catch (const std::exception& error) {
        direct_failed = true;
        check_contains(error.what(), "cannot overwrite the source package",
            "extract-over-source should explain the invalid output target");
    }
    check(direct_failed,
        "PackageReader should reject extracting a package entry over the source package");
    check(fastxlsx::test::read_file(path) == source_bytes,
        "extract-over-source rejection should preserve the source package bytes");

    const std::filesystem::path equivalent_path =
        path.parent_path() / "." / path.filename();
    bool equivalent_failed = false;
    try {
        reader.extract_entry_to_file("custom/unknown.bin", equivalent_path);
    } catch (const std::exception& error) {
        equivalent_failed = true;
        check_contains(error.what(), "cannot overwrite the source package",
            "extract-over-equivalent-source should explain the invalid output target");
    }
    check(equivalent_failed,
        "PackageReader should reject extracting over a path-equivalent source package");
    check(fastxlsx::test::read_file(path) == source_bytes,
        "extract-over-equivalent-source rejection should preserve source package bytes");
    const fastxlsx::detail::PackageReader reopened =
        fastxlsx::detail::PackageReader::open(path);
    check(reopened.read_entry("custom/unknown.bin") == unknown_body,
        "extract-over-source rejection should leave source package readable");
}

void test_package_reader_streams_stored_entry_chunks()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-entry-chunks.xlsx");
    std::string unknown_body;
    for (int index = 0; unknown_body.size() <= 2U * 1024U * 1024U; ++index) {
        unknown_body += "stored-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/unknown.bin");

    std::string chunk;
    std::string streamed_body;
    std::size_t chunk_count = 0;
    while (source(chunk)) {
        check(!chunk.empty(), "PackageReader chunk source should not emit empty chunks");
        streamed_body += chunk;
        ++chunk_count;
    }

    check(streamed_body == unknown_body,
        "PackageReader should stream stored entry bytes through chunk source");
    check(chunk_count > 1, "large stored entry should be delivered in multiple chunks");
}

void test_package_reader_missing_entry_diagnostics_include_requested_name()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-missing-entry-context.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/present.bin", "present"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    constexpr std::string_view missing_entry = "custom/missing.bin";

    const auto expect_missing_entry_failure =
        [&](auto&& action, const char* scenario) {
            bool failed = false;
            try {
                action();
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), "ZIP entry 'custom/missing.bin'",
                    "missing entry failure should name the requested entry");
                check_contains(error.what(), "not present in the package",
                    "missing entry failure should preserve the lookup failure reason");
            }
            check(failed, scenario);
        };

    expect_missing_entry_failure(
        [&] { (void)reader.read_entry(missing_entry); },
        "PackageReader::read_entry should reject a missing entry with context");
    expect_missing_entry_failure(
        [&] { (void)reader.entry_chunk_source(missing_entry); },
        "PackageReader::entry_chunk_source should reject a missing entry with context");

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-missing-entry-output.bin");
    const std::string sentinel = "preserve output when requested entry is missing";
    write_file(extracted, sentinel);
    expect_missing_entry_failure(
        [&] { reader.extract_entry_to_file(missing_entry, extracted); },
        "PackageReader::extract_entry_to_file should reject a missing entry with context");
    check(fastxlsx::test::read_file(extracted) == sentinel,
        "missing entry extraction should not modify an existing output file");
}

void test_package_reader_rejects_inconsistent_materialized_chunk_sizes()
{
    const std::string combined = fastxlsx::detail::testing_read_entry_chunks_to_string(
        make_package_reader_test_chunk_source({"mat", "erial", "ized"}), 12);
    check(combined == "materialized",
        "PackageReader chunk materialization should concatenate valid chunks");

    const auto expect_failure =
        [](std::vector<std::string> chunks, std::uint64_t expected_size,
            std::string_view expected_error_fragment,
            std::size_t read_attempt,
            std::size_t consumed_chunks,
            std::uint64_t consumed_bytes,
            std::uint64_t last_chunk_bytes,
            const char* scenario) {
            std::uint64_t actual_size = 0;
            for (const std::string& chunk : chunks) {
                actual_size += static_cast<std::uint64_t>(chunk.size());
            }
            bool failed = false;
            try {
                (void)fastxlsx::detail::testing_read_entry_chunks_to_string(
                    make_package_reader_test_chunk_source(chunks), expected_size);
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error_fragment,
                    "chunk materialization failure should explain the size contract violation");
                if (expected_error_fragment != std::string_view("emitted an empty chunk")) {
                    check_contains(error.what(),
                        std::string("expected ") + std::to_string(expected_size) + " bytes",
                        "chunk materialization failure should report expected bytes");
                    const std::string actual_prefix =
                        actual_size > expected_size ? "actual at least " : "actual ";
                    check_contains(error.what(),
                        actual_prefix + std::to_string(actual_size) + " bytes",
                        "chunk materialization failure should report actual bytes");
                }
                check_zip_entry_chunk_consumer_progress_diagnostics(error.what(),
                    "ZIP entry materialization chunk source",
                    read_attempt,
                    consumed_chunks,
                    consumed_bytes,
                    last_chunk_bytes,
                    "chunk materialization failure should report consumer progress");
            }
            check(failed, scenario);
        };

    expect_failure({"short"}, 6, "ended before expected bytes", 2, 1, 5, 5,
        "PackageReader should reject chunk sources that end before the expected size");
    expect_failure({"over", "flow"}, 7, "produced more bytes than expected", 2, 1, 4, 4,
        "PackageReader should reject chunk sources that produce too many bytes");
    expect_failure({""}, 0, "emitted an empty chunk", 1, 0, 0, 0,
        "PackageReader should reject empty chunks from a materialized entry source");
}

void test_package_reader_rejects_inconsistent_extraction_chunk_sizes_before_commit()
{
    const std::filesystem::path package_path =
        output_path("fastxlsx-package-reader-extract-size-contract-source.xlsx");
    write_file(package_path, "source package placeholder");

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-extract-size-contract-output.bin");
    const std::string sentinel = "preserve stale extraction output";
    write_file(extracted, sentinel);

    fastxlsx::detail::testing_extract_entry_chunks_to_committed_file(package_path,
        make_package_reader_test_chunk_source({"ex", "act"}), extracted, 5);
    check(fastxlsx::test::read_file(extracted) == "exact",
        "PackageReader extraction chunk helper should commit exact-size output");

    const auto expect_failure =
        [&](std::vector<std::string> chunks, std::uint64_t expected_size,
            std::string_view expected_error_fragment,
            std::size_t read_attempt,
            std::size_t consumed_chunks,
            std::uint64_t consumed_bytes,
            std::uint64_t last_chunk_bytes,
            const char* scenario) {
            std::uint64_t actual_size = 0;
            for (const std::string& chunk : chunks) {
                actual_size += static_cast<std::uint64_t>(chunk.size());
            }
            write_file(extracted, sentinel);
            bool failed = false;
            try {
                fastxlsx::detail::testing_extract_entry_chunks_to_committed_file(package_path,
                    make_package_reader_test_chunk_source(chunks), extracted, expected_size);
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error_fragment,
                    "chunk extraction failure should explain the size contract violation");
                if (expected_error_fragment != std::string_view("emitted an empty chunk")) {
                    check_contains(error.what(),
                        std::string("expected ") + std::to_string(expected_size) + " bytes",
                        "chunk extraction failure should report expected bytes");
                    const std::string actual_prefix =
                        actual_size > expected_size ? "actual at least " : "actual ";
                    check_contains(error.what(),
                        actual_prefix + std::to_string(actual_size) + " bytes",
                        "chunk extraction failure should report actual bytes");
                }
                check_zip_entry_chunk_consumer_progress_diagnostics(error.what(),
                    "ZIP entry file extraction chunk source",
                    read_attempt,
                    consumed_chunks,
                    consumed_bytes,
                    last_chunk_bytes,
                    "chunk extraction failure should report consumer progress");
            }
            check(failed, scenario);
            check(fastxlsx::test::read_file(extracted) == sentinel,
                "failed chunk extraction should preserve the previous output file");
        };

    expect_failure({"short"}, 6, "ended before expected bytes", 2, 1, 5, 5,
        "PackageReader extraction should reject chunk sources that end early");
    expect_failure({"over", "flow"}, 7, "produced more bytes than expected", 2, 1, 4, 4,
        "PackageReader extraction should reject chunk sources that produce too many bytes");
    expect_failure({""}, 0, "emitted an empty chunk", 1, 0, 0, 0,
        "PackageReader extraction should reject empty chunks");
}

} // namespace

int main()
{
    try {
        test_package_writer_rejects_empty_package_before_output();
        test_package_writer_rejects_invalid_compression_levels_before_output();
        test_package_writer_rejects_zip64_entry_count_before_output();
        test_package_writer_rejects_zip_entry_name_length_before_output();
        test_package_writer_rejects_invalid_entry_names_before_output();
        test_package_writer_rejects_duplicate_entry_names_before_output();
        test_package_writer_rejects_zip64_file_chunk_before_output();
        test_package_writer_rejects_missing_file_chunk_before_output();
        test_package_writer_rejects_mixed_legacy_data_and_chunks_before_output();
        test_package_writer_rejects_invalid_chunk_sources_before_output();
        test_stored_zip_backend_contextualizes_actual_chunk_failures();
        test_package_reader_reads_stored_entries_and_unknown_parts();
        test_package_reader_extracts_stored_entry_to_file();
        test_package_reader_rejects_extracting_to_directory();
        test_package_reader_rejects_extracting_to_invalid_parent_before_read();
        test_package_reader_rejects_extracting_over_source_package();
        test_package_reader_streams_stored_entry_chunks();
        test_package_reader_missing_entry_diagnostics_include_requested_name();
        test_package_reader_rejects_inconsistent_materialized_chunk_sizes();
        test_package_reader_rejects_inconsistent_extraction_chunk_sizes_before_commit();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
