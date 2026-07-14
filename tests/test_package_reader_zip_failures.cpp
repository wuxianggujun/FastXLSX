#include "test_package_reader_common.hpp"

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG

std::string raw_compressed_payload(const std::filesystem::path& path,
    const fastxlsx::detail::PackageReaderEntry& entry)
{
    const std::string package = fastxlsx::test::read_file(path);
    check(entry.data_offset <= package.size()
            && entry.compressed_size <= package.size() - entry.data_offset,
        "raw compressed payload range should stay within the ZIP package");
    return package.substr(static_cast<std::size_t>(entry.data_offset),
        static_cast<std::size_t>(entry.compressed_size));
}

void test_package_reader_reads_deflated_entries_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string opaque_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOpaqueExternal" Type="https://fastxlsx.invalid/relationships/opaque-audit" Target="https://example.invalid/opaque" TargetMode="External"/>)"
        R"(</Relationships>)";
    std::string unknown_body = "deflated-opaque";
    unknown_body.append(1, '\0');
    unknown_body += "payload";
    unknown_body.append(1, '\0');
    unknown_body += std::string(256, 'X');
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "\ndeflated-read-entry-chunk-source-";
        unknown_body += std::to_string(index);
    }
    check(unknown_body.size() > 64U * 1024U,
        "DEFLATE read_entry fixture should exceed one reader chunk");

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet><sheetData/></worksheet>"},
            {"custom/opaque.bin", unknown_body},
            {"custom/_rels/opaque.bin.rels", opaque_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const auto* content_types_entry = reader.find_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "DEFLATE package should expose [Content_Types].xml entry");
    check(content_types_entry->compression_method == 8,
        "minizip PackageReader fixture should use DEFLATE entry method");
    check(reader.read_entry("[Content_Types].xml") == content_types,
        "PackageReader should read deflated content types bytes");

    const auto* unknown_entry = reader.find_entry("custom/opaque.bin");
    check(unknown_entry != nullptr, "PackageReader should index deflated unknown entries");
    check(unknown_entry->compression_method == 8,
        "unknown minizip entry should use DEFLATE method");
    check(unknown_entry->uncompressed_size == unknown_body.size(),
        "deflated unknown entry should report uncompressed size");
    check(reader.read_entry("custom/opaque.bin") == unknown_body,
        "PackageReader should read decompressed unknown entry bytes");

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    check(reader.part_index().find_part(workbook_part) != nullptr,
        "DEFLATE PackageReader should build the workbook part index");
    const auto* opaque_manifest_part = reader.part_index().find_part(opaque_part);
    check(opaque_manifest_part != nullptr,
        "DEFLATE PackageReader should build unknown part index entries");
    check(opaque_manifest_part->content_type == "application/octet-stream",
        "DEFLATE PackageReader should resolve unknown part content type defaults");

    const auto* workbook_relationships_set = reader.relationships_for(workbook_part);
    check(workbook_relationships_set != nullptr,
        "DEFLATE PackageReader should ingest workbook relationships");
    check(workbook_relationships_set->find_by_id("rId1") != nullptr,
        "DEFLATE PackageReader should preserve workbook relationship ids");
    check(reader.relationships_for(worksheet_part) == nullptr,
        "DEFLATE PackageReader should not invent missing worksheet relationships");

    const auto* opaque_relationships_set = reader.relationships_for(opaque_part);
    check(opaque_relationships_set != nullptr,
        "DEFLATE PackageReader should ingest unknown extension owner relationships");
    const auto* opaque_external =
        opaque_relationships_set->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "DEFLATE PackageReader should preserve unknown owner relationship ids");
    check(opaque_external->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "DEFLATE PackageReader should preserve external target mode");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    check(graph.relationships_for(opaque_part) != nullptr,
        "DEFLATE PackageReader relationship graph should attach unknown owner relationships");
}

void test_package_reader_streams_deflated_entry_chunks_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-entry-chunks.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; unknown_body.size() <= 2U * 1024U * 1024U; ++index) {
        unknown_body += "deflated-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* deflated_entry = reader.find_entry("custom/deflated.bin");
    check(deflated_entry != nullptr,
        "DEFLATE chunk-source fixture should include target entry");
    check(deflated_entry->compression_method == 8,
        "DEFLATE chunk-source fixture should use method 8");

    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/deflated.bin");

    std::string chunk;
    std::string streamed_body;
    std::size_t chunk_count = 0;
    while (source(chunk)) {
        check(!chunk.empty(), "DEFLATE chunk source should not emit empty chunks");
        streamed_body += chunk;
        ++chunk_count;
    }

    check(streamed_body == unknown_body,
        "PackageReader should stream decompressed DEFLATE entry bytes through chunk source");
    check(chunk_count > 1, "large DEFLATE entry should be delivered in multiple chunks");
}

void test_package_reader_closes_abandoned_deflated_entry_chunk_source()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-abandoned-deflated-entry-chunks.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "abandoned-deflated-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    {
        const fastxlsx::detail::PackageReader reader =
            fastxlsx::detail::PackageReader::open(path);
        fastxlsx::detail::PackageReaderChunkCallback source =
            reader.entry_chunk_source("custom/deflated.bin");

        std::string chunk;
        check(source(chunk), "abandoned DEFLATE chunk source should read a first chunk");
        check(!chunk.empty(), "abandoned DEFLATE chunk source should emit bytes");
    }

    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    check(removed && !error,
        "abandoned DEFLATE chunk source should close the source package handle");
}

void test_package_reader_extracts_deflated_entry_to_file_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-entry-extract.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "deflated-entry-file-backed-extract-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* deflated_entry = reader.find_entry("custom/deflated.bin");
    check(deflated_entry != nullptr,
        "DEFLATE extract fixture should include target entry");
    check(deflated_entry->compression_method == 8,
        "DEFLATE extract fixture should use method 8");

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-deflated-entry-extracted.bin");
    write_file(extracted, "stale deflated extraction output");
    reader.extract_entry_to_file("custom/deflated.bin", extracted);

    check(fastxlsx::test::read_file(extracted) == unknown_body,
        "PackageReader should atomically replace DEFLATE extraction output with entry bytes");
}

void test_package_writer_applies_explicit_minizip_compression_levels()
{
    const std::filesystem::path fastest_path =
        output_path("fastxlsx-package-writer-compression-level-0.xlsx");
    const std::filesystem::path smallest_path =
        output_path("fastxlsx-package-writer-compression-level-9.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(</Types>)";
    std::string workbook = "<workbook><payload>";
    workbook.append(32768, 'A');
    workbook += "</payload></workbook>";

    fastxlsx::detail::PackageWriterOptions fastest;
    fastest.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    fastest.compression_level = fastxlsx::detail::package_writer_min_compression_level;
    fastxlsx::detail::write_package(fastest_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", workbook},
        },
        fastest);

    fastxlsx::detail::PackageWriterOptions smallest;
    smallest.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    smallest.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::PackageWriterTelemetry telemetry;
    smallest.telemetry = &telemetry;
    fastxlsx::detail::write_package(smallest_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", workbook},
        },
        smallest);

    const fastxlsx::detail::PackageReader fastest_reader =
        fastxlsx::detail::PackageReader::open(fastest_path);
    const fastxlsx::detail::PackageReader smallest_reader =
        fastxlsx::detail::PackageReader::open(smallest_path);
    check(fastest_reader.read_entry("xl/workbook.xml") == workbook,
        "compression level 0 output should preserve workbook bytes");
    check(smallest_reader.read_entry("xl/workbook.xml") == workbook,
        "compression level 9 output should preserve workbook bytes");

    const std::string fastest_data = fastxlsx::test::read_file(fastest_path);
    const std::string smallest_data = fastxlsx::test::read_file(smallest_path);
    const ZipEntryLocation fastest_entry =
        find_zip_entry_location(fastest_data, "xl/workbook.xml");
    const ZipEntryLocation smallest_entry =
        find_zip_entry_location(smallest_data, "xl/workbook.xml");
    check(fastest_entry.compression_method == 0,
        "compression level 0 minizip output should use stored/no-compression");
    check(smallest_entry.compression_method == 8,
        "compression level 9 minizip output should use DEFLATE");
    check(fastest_entry.uncompressed_size == workbook.size(),
        "compression level 0 central directory should record uncompressed size");
    check(smallest_entry.uncompressed_size == workbook.size(),
        "compression level 9 central directory should record uncompressed size");
    check(fastest_entry.compressed_size == fastest_entry.uncompressed_size,
        "compression level 0 central directory should record stored size");
    check(fastest_entry.compressed_size > smallest_entry.compressed_size,
        "higher compression level should shrink a repetitive workbook payload");
    check(telemetry.backend == fastxlsx::detail::PackageWriterBackend::MinizipNg,
        "package writer telemetry should report the resolved minizip backend");
    check(telemetry.total_us > 0 && telemetry.entries.size() == 2,
        "package writer telemetry should report package and entry timings");
    const auto workbook_telemetry = std::find_if(telemetry.entries.begin(), telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& entry) {
            return entry.entry_name == "xl/workbook.xml";
        });
    check(workbook_telemetry != telemetry.entries.end(),
        "package writer telemetry should include the workbook entry");
    if (workbook_telemetry != telemetry.entries.end()) {
        check(!workbook_telemetry->raw_compressed_copy,
            "ordinary workbook telemetry should not report raw copy");
        check(workbook_telemetry->uncompressed_bytes == workbook.size()
                && workbook_telemetry->input_bytes == workbook.size(),
            "workbook telemetry should account for its complete input payload");
        check(workbook_telemetry->writer_write_calls > 0
                && workbook_telemetry->total_us > 0,
            "workbook telemetry should report minizip write calls and elapsed time");
    }
}

void test_package_writer_reuses_staged_crc32_for_chunked_minizip_entries()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-writer-staged-crc-body.bin");
    const std::filesystem::path combined_path =
        output_path("fastxlsx-package-writer-staged-crc-combined.xlsx");
    const std::filesystem::path fallback_path =
        output_path("fastxlsx-package-writer-staged-crc-fallback.xlsx");
    const std::string body = "0123456789-file-backed-payload";
    write_file(body_path, body);

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(</Types>)";
    const std::string prefix = "prefix:";
    const std::string suffix = ":suffix";
    constexpr std::uint64_t range_offset = 3;
    constexpr std::uint64_t range_size = 11;

    fastxlsx::detail::PackageEntryChunk full_file =
        fastxlsx::detail::PackageEntryChunk::file(body_path);
    full_file.has_expected_size = true;
    full_file.expected_size = body.size();
    full_file.has_expected_crc32 = true;
    full_file.expected_crc32 = fastxlsx::detail::crc32(body);

    fastxlsx::detail::PackageEntryChunk file_range =
        fastxlsx::detail::PackageEntryChunk::file_range(
            body_path, range_offset, range_size);
    file_range.has_expected_crc32 = true;
    file_range.expected_crc32 = fastxlsx::detail::crc32(
        std::string_view(body).substr(range_offset, range_size));

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = 6;
    fastxlsx::detail::PackageWriterTelemetry combined_telemetry;
    options.telemetry = &combined_telemetry;
    fastxlsx::detail::write_package(combined_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/chunked.bin",
                {
                    fastxlsx::detail::PackageEntryChunk::memory(prefix),
                    full_file,
                    fastxlsx::detail::PackageEntryChunk::memory(""),
                    file_range,
                    fastxlsx::detail::PackageEntryChunk::memory(suffix),
                }},
        },
        options);

    const fastxlsx::detail::PackageReader combined_reader =
        fastxlsx::detail::PackageReader::open(combined_path);
    check(combined_reader.read_entry("custom/chunked.bin")
            == prefix + body + body.substr(range_offset, range_size) + suffix,
        "combined staged CRC output should preserve ordered chunk bytes");
    const auto combined_entry = std::find_if(combined_telemetry.entries.begin(),
        combined_telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& entry) {
            return entry.entry_name == "custom/chunked.bin";
        });
    check(combined_entry != combined_telemetry.entries.end(),
        "combined staged CRC telemetry should include the chunked entry");
    if (combined_entry != combined_telemetry.entries.end()) {
        check(combined_entry->reused_staged_crc32,
            "chunked minizip entry should reuse staged CRC metadata");
        check(combined_entry->reused_staged_file_chunk_count == 2,
            "chunked minizip entry should report every reused file chunk CRC");
    }

    fastxlsx::detail::PackageWriterTelemetry fallback_telemetry;
    options.telemetry = &fallback_telemetry;
    fastxlsx::detail::write_package(fallback_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/chunked.bin",
                {fastxlsx::detail::PackageEntryChunk::file(body_path)}},
        },
        options);
    const auto fallback_entry = std::find_if(fallback_telemetry.entries.begin(),
        fallback_telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& entry) {
            return entry.entry_name == "custom/chunked.bin";
        });
    check(fallback_entry != fallback_telemetry.entries.end(),
        "fallback staged CRC telemetry should include the chunked entry");
    if (fallback_entry != fallback_telemetry.entries.end()) {
        check(!fallback_entry->reused_staged_crc32
                && fallback_entry->reused_staged_file_chunk_count == 0,
            "file chunks without expected CRC metadata should keep fallback validation");
    }
}

void test_package_writer_raw_copies_matching_compressed_entries()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-writer-raw-copy-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-writer-raw-copy-output.xlsx");
    std::string workbook = "<workbook><payload>";
    for (int index = 0; index < 8192; ++index) {
        workbook += "raw-copy-repetitive-payload-" + std::to_string(index % 17);
    }
    workbook += "</payload></workbook>";
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(</Types>)";

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = 6;
    fastxlsx::detail::write_package(source_path,
        {{"[Content_Types].xml", content_types}, {"xl/workbook.xml", workbook}},
        options);

    const fastxlsx::detail::PackageReader source =
        fastxlsx::detail::PackageReader::open(source_path);
    const fastxlsx::detail::PackageReaderEntry* source_workbook =
        source.find_entry("xl/workbook.xml");
    check(source_workbook != nullptr && source_workbook->compression_method == 8,
        "raw-copy source workbook should be DEFLATE");
    if (source_workbook == nullptr) {
        return;
    }

    fastxlsx::detail::PackageRawCompressedEntrySource raw_source {
        source_path,
        source_workbook->data_offset,
        source_workbook->compressed_size,
        source_workbook->uncompressed_size,
        source_workbook->crc32,
        source_workbook->compression_method,
    };
    fastxlsx::detail::PackageWriterTelemetry raw_copy_telemetry;
    options.telemetry = &raw_copy_telemetry;
    fastxlsx::detail::write_package(output,
        {
            {"[Content_Types].xml", content_types},
            fastxlsx::detail::PackageEntry::raw_compressed_copy(
                "xl/workbook.xml", raw_source),
        },
        options);

    const auto raw_workbook_telemetry = std::find_if(raw_copy_telemetry.entries.begin(),
        raw_copy_telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& entry) {
            return entry.entry_name == "xl/workbook.xml";
        });
    check(raw_workbook_telemetry != raw_copy_telemetry.entries.end(),
        "raw-copy telemetry should include the workbook entry");
    if (raw_workbook_telemetry != raw_copy_telemetry.entries.end()) {
        check(raw_workbook_telemetry->raw_compressed_copy,
            "raw-copy telemetry should identify compressed payload passthrough");
        check(raw_workbook_telemetry->input_bytes == source_workbook->compressed_size,
            "raw-copy telemetry should account for compressed source bytes");
        check(raw_workbook_telemetry->input_read_calls > 0
                && raw_workbook_telemetry->writer_write_calls > 0,
            "raw-copy telemetry should report file reads and writer calls");
    }

    const fastxlsx::detail::PackageReader written =
        fastxlsx::detail::PackageReader::open(output);
    const fastxlsx::detail::PackageReaderEntry* written_workbook =
        written.find_entry("xl/workbook.xml");
    check(written_workbook != nullptr,
        "raw-copy output should contain the workbook entry");
    if (written_workbook == nullptr) {
        return;
    }
    check(written.read_entry("xl/workbook.xml") == workbook,
        "raw-copy output should preserve the logical workbook payload");
    check(written_workbook->crc32 == source_workbook->crc32
            && written_workbook->compressed_size == source_workbook->compressed_size,
        "raw-copy output should preserve source CRC and compressed size");
    check(raw_compressed_payload(output, *written_workbook)
            == raw_compressed_payload(source_path, *source_workbook),
        "raw-copy output should preserve exact compressed payload bytes");

    fastxlsx::detail::PackageWriterOptions stored_options;
    stored_options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
    bool rejected_stored_raw_source = false;
    try {
        fastxlsx::detail::write_package(output,
            {fastxlsx::detail::PackageEntry::raw_compressed_copy(
                "xl/workbook.xml", raw_source)},
            stored_options);
    } catch (const fastxlsx::FastXlsxError&) {
        rejected_stored_raw_source = true;
    }
    check(rejected_stored_raw_source,
        "stored bootstrap should reject raw compressed entry sources");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_read()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-crc-target";
    unknown_body.append(512, 'Z');
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.read_entry("custom/blob.bin");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "custom/blob.bin",
            "corrupt DEFLATE read should report the ZIP entry name");
    }
    check(failed, "PackageReader should reject corrupt DEFLATE entry bytes");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_extract()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-extract-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-extract-crc-target";
    unknown_body.append(512, 'Z');
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate extract setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate extract setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-extract-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-deflated-extract-crc-output.bin");
    const std::string sentinel = "preserve existing corrupt-deflate extraction output";
    write_file(extracted, sentinel);

    bool failed = false;
    try {
        reader.extract_entry_to_file("custom/blob.bin", extracted);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "custom/blob.bin",
            "corrupt DEFLATE extract should report the ZIP entry name");
    }
    check(failed,
        "PackageReader should reject corrupt DEFLATE entry bytes during extract");
    check(fastxlsx::test::read_file(extracted) == sentinel,
        "corrupt DEFLATE extraction should preserve the previous output file");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_chunk_source()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-chunks-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-chunk-crc-target";
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "\ndeflated-chunk-crc-target-row-";
        unknown_body += std::to_string(index);
    }
    check(unknown_body.size() > 64U * 1024U,
        "DEFLATE chunk-source CRC fixture should exceed one reader chunk");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate chunk-source setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate chunk-source setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-chunks-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/blob.bin");

    bool failed = false;
    std::size_t emitted_chunks = 0;
    std::uint64_t emitted_bytes = 0;
    std::uint64_t last_chunk_bytes = 0;
    try {
        std::string chunk;
        while (source(chunk)) {
            ++emitted_chunks;
            emitted_bytes += static_cast<std::uint64_t>(chunk.size());
            last_chunk_bytes = static_cast<std::uint64_t>(chunk.size());
        }
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "custom/blob.bin",
            "corrupt DEFLATE chunk-source read should report the ZIP entry name");
        check_zip_entry_chunk_source_progress_diagnostics(error.what(), emitted_chunks + 1,
            emitted_chunks,
            emitted_bytes,
            last_chunk_bytes,
            "corrupt DEFLATE chunk-source read should report reader progress");
    }
    check(failed,
        "PackageReader should reject corrupt DEFLATE entry bytes during chunk source read");
}

#endif

void test_package_reader_rejects_duplicate_entries()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-duplicate-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"xl/sheet1.xml", "one"},
            {"xl/sheet2.xml", "two"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::string duplicate_name = "xl/sheet1.xml";
    const std::string original_name = "xl/sheet2.xml";
    check(duplicate_name.size() == original_name.size(),
        "duplicate-entry fixture names should have matching lengths");

    const ZipEntryLocation second_entry = find_zip_entry_location(data, original_name);
    data.replace(second_entry.central_offset + 46u, original_name.size(), duplicate_name);
    data.replace(second_entry.local_offset + 30u, original_name.size(), duplicate_name);

    const std::filesystem::path path = output_path("fastxlsx-package-reader-duplicate.xlsx");
    write_file(path, data);

    expect_open_failure_contains(path, "ZIP entry 'xl/sheet1.xml'",
        "duplicate ZIP entry rejection should include the entry name");
}

void test_package_reader_rejects_invalid_entry_names()
{
    struct InvalidEntryNameCase {
        std::string_view suffix;
        std::string entry_name;
        std::string_view expected_context;
    };

    const std::vector<InvalidEntryNameCase> cases = {
        {"absolute", "/xl/workbook.xml", "ZIP entry '/xl/workbook.xml'"},
        {"trailing-slash", "xl/workbook.xml/", "ZIP entry 'xl/workbook.xml/'"},
        {"empty-segment", "xl//workbook.xml", "ZIP entry 'xl//workbook.xml'"},
        {"dot-segment", "xl/./workbook.xml", "ZIP entry 'xl/./workbook.xml'"},
        {"parent-segment", "xl/../workbook.xml", "ZIP entry 'xl/../workbook.xml'"},
        {"backslash", R"(xl\workbook.xml)", R"(ZIP entry 'xl\\workbook.xml')"},
        {"query", "xl/workbook.xml?version=1",
            "ZIP entry 'xl/workbook.xml?version=1'"},
        {"fragment", "xl/workbook.xml#sheet", "ZIP entry 'xl/workbook.xml#sheet'"},
        {"null-byte", std::string("xl/workbook\0.xml", 16),
            R"(ZIP entry 'xl/workbook\0.xml')"},
    };

    for (const InvalidEntryNameCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-reader-invalid-entry-name-"
            + std::string(test_case.suffix) + ".xlsx");
        fastxlsx::detail::write_stored_zip(path,
            {
                {"[Content_Types].xml",
                    R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
                {test_case.entry_name, "<workbook/>"},
            });

        expect_open_failure_contains(path, test_case.expected_context,
            "PackageReader invalid entry-name rejection should include entry context");
    }
}

void test_package_reader_ignores_zip_directory_entries()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-directory-entry-source.xlsx");
    const std::string content_types =
        R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)";
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::string data =
        add_zip_directory_entry(fastxlsx::test::read_file(source_path), "xl/_rels/");
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-directory-entry.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    check(reader.entries().size() == 2,
        "PackageReader should ignore ZIP directory entries when indexing parts");
    check(reader.find_entry("xl/_rels/") == nullptr,
        "PackageReader should not expose directory entries as package parts");
    check(reader.read_entry("[Content_Types].xml") == content_types,
        "PackageReader should still read real parts when directory entries are present");
}

void test_package_reader_rejects_invalid_zip_directory_entry_names()
{
    struct InvalidDirectoryEntryNameCase {
        std::string_view suffix;
        std::string entry_name;
        std::string_view expected_context;
    };

    const std::vector<InvalidDirectoryEntryNameCase> cases = {
        {"absolute", "/xl/", "ZIP entry '/xl/'"},
        {"empty-segment", "xl//", "ZIP entry 'xl//'"},
        {"dot-segment", "xl/./", "ZIP entry 'xl/./'"},
        {"parent-segment", "xl/../", "ZIP entry 'xl/../'"},
    };

    for (const InvalidDirectoryEntryNameCase& test_case : cases) {
        const std::filesystem::path source_path = output_path(
            "fastxlsx-package-reader-invalid-directory-entry-source-"
            + std::string(test_case.suffix) + ".xlsx");
        fastxlsx::detail::write_package(source_path,
            {
                {"[Content_Types].xml",
                    R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
                {"xl/workbook.xml", "<workbook/>"},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

        const std::filesystem::path path = output_path(
            "fastxlsx-package-reader-invalid-directory-entry-"
            + std::string(test_case.suffix) + ".xlsx");
        write_file(path,
            add_zip_directory_entry(
                fastxlsx::test::read_file(source_path), test_case.entry_name));

        expect_open_failure_contains(path, test_case.expected_context,
            "PackageReader invalid ZIP directory entry-name rejection should include context");
    }
}

void test_package_reader_rejects_empty_central_directory_entry_name_with_context()
{
    const std::string entry_name = "xl/workbook.xml";
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-empty-central-entry-name-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {entry_name, "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation location = find_zip_entry_location(data, entry_name);
    write_u16(data, location.central_offset + 28u, 0);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-empty-central-entry-name.xlsx");
    write_file(path, data);

    expect_open_failure_contains(path, "ZIP entry '': ZIP entry name cannot be empty",
        "PackageReader empty central-directory entry-name rejection should include context");
}

void test_package_reader_rejects_bad_zip()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-bad.xlsx");
    write_file(path, "not a zip");

    expect_open_failure(path, "PackageReader should reject invalid ZIP packages");
}

void test_package_reader_rejects_central_directory_trailing_data_before_eocd()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-central-trailing-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    data.insert(eocd_offset, "unsupported trailing central directory data");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-central-trailing.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject unsupported data between central directory and EOCD");
}

#ifndef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_package_reader_rejects_compressed_entries_without_minizip()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-method-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::size_t central_offset = find_signature(data, 0x02014b50u);
    write_u16(data, local_offset + 8, 8);
    write_u16(data, central_offset + 10, 8);

    const std::filesystem::path path = output_path("fastxlsx-package-reader-method.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject compressed ZIP entries without minizip-ng");
}
#endif

void test_package_reader_unsupported_compression_diagnostics_include_entry_name()
{
    constexpr std::uint16_t unsupported_method = 12;
    const std::string entry_name = "custom/blob.bin";
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-unsupported-method-context-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {entry_name, "payload"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation location = find_zip_entry_location(data, entry_name);
    write_u16(data, location.local_offset + 8u, unsupported_method);
    write_u16(data, location.central_offset + 10u, unsupported_method);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-unsupported-method-context.xlsx");
    write_file(path, data);

    bool failed = false;
    try {
        (void)fastxlsx::detail::PackageReader::open(path);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), entry_name,
            "unsupported compression failure should include the ZIP entry name");
        check_contains(error.what(), "compression method 12",
            "unsupported compression failure should include the method number");
    }
    check(failed, "PackageReader should reject unsupported ZIP compression methods");
}

void test_package_reader_rejects_central_encrypted_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-central-encrypted-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t central_offset = find_signature(data, 0x02014b50u);
    write_u16(data, central_offset + 8,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, central_offset + 8)
            | 0x0001u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-central-encrypted.xlsx");
    write_file(path, data);

    expect_open_failure_contains(path, "ZIP entry '[Content_Types].xml'",
        "central-directory encrypted flag rejection should include the entry name");
}

void test_package_reader_rejects_local_encrypted_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-encrypted-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    write_u16(data, local_offset + 6,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, local_offset + 6)
            | 0x0001u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-encrypted.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header encrypted flags");
}

void test_package_reader_rejects_local_header_method_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-method-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    write_u16(data, local_offset + 8, 8);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-method.xlsx");
    write_file(path, data);

    expect_open_failure_contains(path, "ZIP entry '[Content_Types].xml'",
        "local-header compression method mismatches should include the ZIP entry name");
}

void test_package_reader_rejects_local_header_name_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-name-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::size_t local_name_offset = local_offset + 30u;
    data.at(local_name_offset) = data.at(local_name_offset) == '[' ? 'X' : '[';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-name.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header entry name mismatches");
}

void test_package_reader_rejects_local_header_size_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-size-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::uint32_t compressed_size =
        fastxlsx::test::read_u32(data, local_offset + 18);
    write_u32(data, local_offset + 18, compressed_size + 1u);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-size.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header entry size mismatches");
}

void test_package_reader_reads_stored_entry_with_data_descriptor()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-data-descriptor-source.xlsx");
    const std::string content_types =
        R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)";
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = add_data_descriptor_to_entry(
        fastxlsx::test::read_file(source_path), "[Content_Types].xml");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-data-descriptor.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const fastxlsx::detail::PackageReaderEntry* entry =
        reader.find_entry("[Content_Types].xml");
    check(entry != nullptr, "PackageReader should index data-descriptor entries");
    check(entry->compressed_size == content_types.size(),
        "data-descriptor central directory should provide compressed size");
    check(entry->uncompressed_size == content_types.size(),
        "data-descriptor central directory should provide uncompressed size");
    check(reader.read_entry("[Content_Types].xml") == content_types,
        "PackageReader should read stored data-descriptor entry bytes");
}

void test_package_reader_rejects_local_header_crc_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::uint32_t crc = fastxlsx::test::read_u32(data, local_offset + 14);
    write_u32(data, local_offset + 14, crc ^ 0xffffffffu);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-crc.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header CRC mismatches");
}

void test_package_reader_rejects_missing_content_types()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-missing-content-types.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path, "PackageReader should require [Content_Types].xml");
}

void test_package_reader_rejects_bad_content_types_xml()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-bad-content-types.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", R"(<Types><Override PartName="/xl/workbook.xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject malformed content type metadata");

    const std::filesystem::path wrong_root_path =
        output_path("fastxlsx-package-reader-content-types-wrong-root.xlsx");
    fastxlsx::detail::write_package(wrong_root_path,
        {
            {"[Content_Types].xml",
                R"(<Metadata><Types><Default Extension="xml" ContentType="application/xml"/></Types></Metadata>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(wrong_root_path,
        "PackageReader should reject content type metadata with a wrong document root");

    const std::filesystem::path nested_decoy_path =
        output_path("fastxlsx-package-reader-content-types-nested-decoy.xlsx");
    fastxlsx::detail::write_package(nested_decoy_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Metadata><Default Extension="xml" ContentType="application/xml"/></Metadata></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(nested_decoy_path,
        "PackageReader should reject nested decoy content type metadata");

    const std::filesystem::path namespaced_attribute_path =
        output_path("fastxlsx-package-reader-content-types-namespaced-attribute.xlsx");
    fastxlsx::detail::write_package(namespaced_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types xmlns:x="urn:fastxlsx:test"><Default x:Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_attribute_path,
        "PackageReader should reject namespaced content type metadata attributes");

    const std::filesystem::path duplicate_attribute_path =
        output_path("fastxlsx-package-reader-content-types-duplicate-attribute.xlsx");
    fastxlsx::detail::write_package(duplicate_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml" ContentType="text/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(duplicate_attribute_path,
        "PackageReader should reject duplicate content type metadata attributes");

    const std::filesystem::path text_child_path =
        output_path("fastxlsx-package-reader-content-types-text-child.xlsx");
    fastxlsx::detail::write_package(text_child_path,
        {
            {"[Content_Types].xml",
                R"(<Types>metadata<Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(text_child_path,
        "PackageReader should reject non-whitespace content type metadata text");

    const std::filesystem::path mismatched_qname_path =
        output_path("fastxlsx-package-reader-content-types-mismatched-qname.xlsx");
    fastxlsx::detail::write_package(mismatched_qname_path,
        {
            {"[Content_Types].xml",
                R"(<x:Types xmlns:x="urn:fastxlsx:test"><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(mismatched_qname_path,
        "PackageReader should reject mismatched content type metadata tag names");
}

void test_package_reader_rejects_oversized_metadata_materialization_on_open()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-oversized-metadata.xlsx");
    const std::string oversized_content_types =
        "<Types>"
        + std::string((4U * 1024U * 1024U) + 1U, ' ')
        + "</Types>";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", oversized_content_types},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure_contains(path,
        "materialized package metadata entry exceeds small XML limit",
        "PackageReader should reject oversized materialized metadata on open");
}

void test_package_reader_rejects_conflicting_content_type_defaults()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-conflicting-content-type-defaults.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types>)"
                R"(<Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension=".XML" ContentType="text/xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject conflicting content type defaults");
}

void test_package_reader_rejects_conflicting_content_type_overrides()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-conflicting-content-type-overrides.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types>)"
                R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
                R"(<Override PartName="/xl/./workbook.xml" ContentType="application/xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject conflicting content type overrides");
}

void test_package_reader_rejects_bad_relationships_xml()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-bad-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject malformed relationship metadata");

    const std::filesystem::path wrong_root_path =
        output_path("fastxlsx-package-reader-relationships-wrong-root.xlsx");
    fastxlsx::detail::write_package(wrong_root_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Metadata><Relationships><Relationship Id="rId1" Type="type" Target="target.xml"/></Relationships></Metadata>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(wrong_root_path,
        "PackageReader should reject relationship metadata with a wrong document root");

    const std::filesystem::path nested_decoy_path =
        output_path("fastxlsx-package-reader-relationships-nested-decoy.xlsx");
    fastxlsx::detail::write_package(nested_decoy_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Metadata><Relationship Id="rId1" Type="type" Target="target.xml"/></Metadata></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(nested_decoy_path,
        "PackageReader should reject nested decoy relationship metadata");

    const std::filesystem::path namespaced_id_path =
        output_path("fastxlsx-package-reader-relationships-namespaced-id.xlsx");
    fastxlsx::detail::write_package(namespaced_id_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships xmlns:x="urn:fastxlsx:test"><Relationship x:Id="rId1" Type="type" Target="target.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_id_path,
        "PackageReader should reject namespaced relationship id attributes");

    const std::filesystem::path namespaced_target_mode_path =
        output_path("fastxlsx-package-reader-relationships-namespaced-target-mode.xlsx");
    fastxlsx::detail::write_package(namespaced_target_mode_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships xmlns:x="urn:fastxlsx:test"><Relationship Id="rId1" Type="type" Target="https://example.invalid/target.xml" x:TargetMode="External"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_target_mode_path,
        "PackageReader should reject namespaced relationship TargetMode attributes");

    const std::filesystem::path duplicate_attribute_path =
        output_path("fastxlsx-package-reader-relationships-duplicate-attribute.xlsx");
    fastxlsx::detail::write_package(duplicate_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Id="rId2" Type="type" Target="target.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(duplicate_attribute_path,
        "PackageReader should reject duplicate relationship metadata attributes");

    const std::filesystem::path trailing_text_path =
        output_path("fastxlsx-package-reader-relationships-trailing-text.xlsx");
    fastxlsx::detail::write_package(trailing_text_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type" Target="target.xml"/></Relationships>metadata)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(trailing_text_path,
        "PackageReader should reject non-whitespace relationship metadata text");

    const std::filesystem::path mismatched_qname_path =
        output_path("fastxlsx-package-reader-relationships-mismatched-qname.xlsx");
    fastxlsx::detail::write_package(mismatched_qname_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type" Target="target.xml"></x:Relationship></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(mismatched_qname_path,
        "PackageReader should reject mismatched relationship metadata tag names");
}

void test_package_reader_rejects_duplicate_relationship_ids()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-duplicate-relationship-ids.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"_rels/.rels",
                R"(<Relationships>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject duplicate relationship ids inside one .rels part");
}

void test_package_reader_rejects_duplicate_source_relationship_ids()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-duplicate-source-relationship-ids.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/styles.xml", "<styleSheet/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(</Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject duplicate relationship ids inside source-owned .rels");
}

void test_package_reader_rejects_relationships_for_missing_source_part()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-orphan-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
                R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject source-owned relationships without the source part");
}

void test_package_reader_rejects_root_relationships_for_missing_source_part()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-orphan-root-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
                R"(</Types>)"},
            {"_rels/root.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject root source-owned relationships without the source part");
}

void test_package_reader_rejects_corrupt_entry_crc_on_read()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.read_entry("custom/blob.bin");
    } catch (const std::exception& error) {
        failed = true;
        check_zip_entry_crc_mismatch_diagnostics(error.what(), "custom/blob.bin",
            "corrupt stored read should report entry and expected/actual CRC");
    }
    check(failed, "PackageReader should reject corrupt entry bytes by CRC");
}

void test_package_reader_rejects_corrupt_entry_crc_on_extract()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-extract-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-extract-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-extract-crc-output.bin");
    const std::string sentinel = "preserve existing corrupt stored extraction output";
    write_file(extracted, sentinel);

    bool failed = false;
    try {
        reader.extract_entry_to_file("custom/blob.bin", extracted);
    } catch (const std::exception& error) {
        failed = true;
        check_zip_entry_crc_mismatch_diagnostics(error.what(), "custom/blob.bin",
            "corrupt stored extract should report entry and expected/actual CRC");
    }
    check(failed, "PackageReader should reject corrupt entry bytes during extract");
    check(fastxlsx::test::read_file(extracted) == sentinel,
        "corrupt stored extraction should preserve the previous output file");
}

void test_package_reader_rejects_corrupt_entry_crc_on_chunk_source()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-entry-chunks-crc-source.xlsx");
    std::string opaque_body = "opaque";
    for (int index = 0; opaque_body.size() <= 2U * 1024U * 1024U; ++index) {
        opaque_body += "\nstored-chunk-source-crc-target-row-";
        opaque_body += std::to_string(index);
    }
    check(opaque_body.size() > 1024U * 1024U,
        "stored chunk-source CRC fixture should exceed one reader chunk");

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", opaque_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-entry-chunks-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/blob.bin");

    bool failed = false;
    std::size_t emitted_chunks = 0;
    std::uint64_t emitted_bytes = 0;
    std::uint64_t last_chunk_bytes = 0;
    try {
        std::string chunk;
        while (source(chunk)) {
            ++emitted_chunks;
            emitted_bytes += static_cast<std::uint64_t>(chunk.size());
            last_chunk_bytes = static_cast<std::uint64_t>(chunk.size());
        }
    } catch (const std::exception& error) {
        failed = true;
        check_zip_entry_crc_mismatch_diagnostics(error.what(), "custom/blob.bin",
            "corrupt stored chunk source should report entry and expected/actual CRC");
        check(emitted_chunks > 1,
            "corrupt stored chunk-source setup should emit multiple chunks before CRC failure");
        check(emitted_bytes == opaque_body.size(),
            "corrupt stored chunk-source setup should emit the full payload before CRC failure");
        check_zip_entry_chunk_source_progress_diagnostics(error.what(), emitted_chunks + 1,
            emitted_chunks,
            emitted_bytes,
            last_chunk_bytes,
            "corrupt stored chunk source should report reader progress");
    }
    check(failed, "PackageReader should reject corrupt entry bytes during chunk source read");
}

void test_package_reader_contextualizes_truncated_stored_entry_read_failure()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-truncated-entry-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-truncated-entry-read.xlsx");
    write_file(path, data);
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    write_file(path, data.substr(0, blob.data_offset));

    bool failed = false;
    try {
        (void)reader.read_entry("custom/blob.bin");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "ZIP entry 'custom/blob.bin'",
            "truncated stored entry read failure should identify the entry");
        check_contains(error.what(), "failed to read XLSX package bytes",
            "truncated stored entry read failure should preserve the read failure");
    }
    check(failed, "PackageReader should reject truncated stored entry bytes");
}

void test_package_reader_rejects_corrupt_metadata_crc_on_open()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-crc-metadata-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "application/xml");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-crc-metadata.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject corrupt metadata entry bytes by CRC");
}


} // namespace

int main()
{
    try {
        test_package_reader_rejects_duplicate_entries();
        test_package_reader_rejects_invalid_entry_names();
        test_package_reader_ignores_zip_directory_entries();
        test_package_reader_rejects_invalid_zip_directory_entry_names();
        test_package_reader_rejects_empty_central_directory_entry_name_with_context();
        test_package_reader_rejects_bad_zip();
        test_package_reader_rejects_central_directory_trailing_data_before_eocd();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_package_reader_reads_deflated_entries_with_minizip();
        test_package_reader_streams_deflated_entry_chunks_with_minizip();
        test_package_reader_closes_abandoned_deflated_entry_chunk_source();
        test_package_reader_extracts_deflated_entry_to_file_with_minizip();
        test_package_writer_applies_explicit_minizip_compression_levels();
        test_package_writer_reuses_staged_crc32_for_chunked_minizip_entries();
        test_package_writer_raw_copies_matching_compressed_entries();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_read();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_extract();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_chunk_source();
#else
        test_package_reader_rejects_compressed_entries_without_minizip();
#endif
        test_package_reader_unsupported_compression_diagnostics_include_entry_name();
        test_package_reader_rejects_central_encrypted_flag();
        test_package_reader_rejects_local_encrypted_flag();
        test_package_reader_rejects_local_header_method_mismatch();
        test_package_reader_rejects_local_header_name_mismatch();
        test_package_reader_rejects_local_header_size_mismatch();
        test_package_reader_reads_stored_entry_with_data_descriptor();
        test_package_reader_rejects_local_header_crc_mismatch();
        test_package_reader_rejects_missing_content_types();
        test_package_reader_rejects_bad_content_types_xml();
        test_package_reader_rejects_oversized_metadata_materialization_on_open();
        test_package_reader_rejects_conflicting_content_type_defaults();
        test_package_reader_rejects_conflicting_content_type_overrides();
        test_package_reader_rejects_bad_relationships_xml();
        test_package_reader_rejects_duplicate_relationship_ids();
        test_package_reader_rejects_duplicate_source_relationship_ids();
        test_package_reader_rejects_relationships_for_missing_source_part();
        test_package_reader_rejects_root_relationships_for_missing_source_part();
        test_package_reader_rejects_corrupt_entry_crc_on_read();
        test_package_reader_rejects_corrupt_entry_crc_on_extract();
        test_package_reader_rejects_corrupt_entry_crc_on_chunk_source();
        test_package_reader_contextualizes_truncated_stored_entry_read_failure();
        test_package_reader_rejects_corrupt_metadata_crc_on_open();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
