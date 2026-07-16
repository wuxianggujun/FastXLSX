#include "../src/package_reader.hpp"
#include "../src/package_writer.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef FASTXLSX_TEST_HAS_DIRECT_ZLIB_PROFILING
#include <zlib.h>
#endif

namespace {

void check(bool condition, std::string_view message)
{
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

class TemporaryFiles {
public:
    TemporaryFiles()
    {
        const auto suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = std::filesystem::temp_directory_path()
            / ("fastxlsx-direct-zlib-test-" + suffix);
        std::filesystem::create_directories(root_);
    }

    ~TemporaryFiles()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] std::filesystem::path path(std::string_view name) const
    {
        return root_ / std::filesystem::path(name);
    }

private:
    std::filesystem::path root_;
};

void write_file(const std::filesystem::path& path, std::string_view bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    check(static_cast<bool>(output), "failed to create package-writer test source");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    check(static_cast<bool>(output), "failed to write package-writer test source");
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "failed to open package-writer test output");
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string content_types_xml()
{
    return
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
}

std::string package_relationships_xml()
{
    return
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>)";
}

std::vector<fastxlsx::detail::PackageEntry> entries_for_file(
    const std::filesystem::path& source, std::uint64_t size, std::uint32_t crc32)
{
    fastxlsx::detail::PackageEntryChunk chunk =
        fastxlsx::detail::PackageEntryChunk::file(source);
    chunk.has_expected_size = true;
    chunk.expected_size = size;
    chunk.has_expected_crc32 = true;
    chunk.expected_crc32 = crc32;
    return {
        {"[Content_Types].xml", content_types_xml()},
        {"_rels/.rels", package_relationships_xml()},
        {"custom/payload.bin",
            std::vector<fastxlsx::detail::PackageEntryChunk> {std::move(chunk)}},
    };
}

const fastxlsx::detail::PackageWriterEntryTelemetry& payload_telemetry(
    const fastxlsx::detail::PackageWriterTelemetry& telemetry)
{
    for (const auto& entry : telemetry.entries) {
        if (entry.entry_name == "custom/payload.bin") {
            return entry;
        }
    }
    throw std::runtime_error("payload telemetry is missing");
}

void test_stored_backend_rejects_direct_zlib_engine(const TemporaryFiles& files)
{
    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
    options.deflate_engine = fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    bool threw = false;
    try {
        fastxlsx::detail::write_package(files.path("stored-rejected.zip"),
            {{"payload.bin", "payload"}}, options);
    } catch (const fastxlsx::FastXlsxError&) {
        threw = true;
    }
    check(threw, "stored backend should reject direct zlib engine selection");
}

void test_invalid_deflate_output_buffer_preserves_output(const TemporaryFiles& files)
{
    const std::filesystem::path output = files.path("invalid-buffer.zip");
    const std::string sentinel = "preserve invalid direct zlib output buffer sentinel";
    write_file(output, sentinel);

    fastxlsx::detail::PackageWriterOptions options;
    options.deflate_engine = fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    options.deflate_output_buffer_size = 12U * 1024U;
    bool threw = false;
    try {
        fastxlsx::detail::write_package(output, {{"payload.bin", "payload"}}, options);
    } catch (const fastxlsx::FastXlsxError&) {
        threw = true;
    }
    check(threw, "invalid direct zlib output buffer should be rejected");
    check(read_file(output) == sentinel,
        "invalid direct zlib output buffer should preserve existing output bytes");
}

#ifndef FASTXLSX_TEST_HAS_DIRECT_ZLIB_PROFILING
void test_disabled_profile_rejects_direct_zlib_engine(const TemporaryFiles& files)
{
    const std::filesystem::path output = files.path("disabled-profile.zip");
    const std::string sentinel = "preserve disabled direct zlib profiling output sentinel";
    write_file(output, sentinel);

    fastxlsx::detail::PackageWriterOptions options;
    options.deflate_engine = fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    bool threw = false;
    try {
        fastxlsx::detail::write_package(output, {{"payload.bin", "payload"}}, options);
    } catch (const fastxlsx::FastXlsxError&) {
        threw = true;
    }
    check(threw, "default build should reject the direct zlib profiling engine");
    check(read_file(output) == sentinel,
        "disabled direct zlib profiling should preserve existing output bytes");
}
#else

std::uint32_t crc32_of(std::string_view bytes)
{
    uLong value = ::crc32(0L, Z_NULL, 0);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const uInt chunk_size = static_cast<uInt>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<uInt>::max())));
        value = ::crc32(value,
            reinterpret_cast<const Bytef*>(bytes.data() + offset), chunk_size);
        offset += chunk_size;
    }
    return static_cast<std::uint32_t>(value);
}

std::string large_payload()
{
    const std::string pattern =
        "<row><c r=\"A1\"><v>123456789</v></c><c r=\"B1\" t=\"inlineStr\">"
        "<is><t>fastxlsx-direct-zlib-regression</t></is></c></row>\n";
    std::string payload;
    payload.reserve(5U * 1024U * 1024U + pattern.size());
    while (payload.size() < 5U * 1024U * 1024U) {
        payload += pattern;
    }
    return payload;
}

void test_explicit_direct_zlib_round_trip_and_default(const TemporaryFiles& files)
{
    const std::string payload = large_payload();
    const std::uint32_t payload_crc32 = crc32_of(payload);
    const std::filesystem::path source = files.path("payload.bin");
    write_file(source, payload);

    fastxlsx::detail::PackageWriterTelemetry direct_telemetry;
    fastxlsx::detail::PackageWriterOptions direct_options;
    direct_options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    direct_options.deflate_engine =
        fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    direct_options.compression_level = 1;
    direct_options.deflate_output_buffer_size = 64U * 1024U;
    direct_options.telemetry = &direct_telemetry;
    const std::filesystem::path direct_output = files.path("direct.zip");
    fastxlsx::detail::write_package(direct_output,
        entries_for_file(source, payload.size(), payload_crc32), direct_options);

    const auto& direct_entry = payload_telemetry(direct_telemetry);
    check(direct_entry.direct_zlib_raw,
        "explicit direct zlib engine should activate for a large staged entry");
    check(!direct_entry.raw_compressed_copy,
        "one-pass direct zlib should not report a precompressed source");
    check(direct_entry.reused_staged_crc32,
        "one-pass direct zlib should retain staged CRC metadata");
    check(direct_entry.input_bytes == payload.size(),
        "one-pass direct zlib should consume every logical input byte");
    check(direct_entry.direct_zlib_output_bytes > 0,
        "one-pass direct zlib should report compressed output");
    check(direct_entry.direct_zlib_output_buffer_bytes == 64U * 1024U,
        "one-pass direct zlib should report the configured output buffer");
    check(direct_entry.direct_zlib_output_buffer_peak_bytes
            <= direct_entry.direct_zlib_output_buffer_bytes,
        "one-pass direct zlib output peak should stay bounded");
    check(direct_entry.writer_write_process_cpu_us == 0,
        "raw output writes should not be counted as zlib engine process CPU");
    check(direct_entry.direct_zlib_engine_process_cpu_us
            <= direct_entry.total_process_cpu_us,
        "zlib engine process CPU should fit the complete entry process CPU");
    check(direct_entry.deflate_writer_process_cpu_us
            == direct_entry.direct_zlib_engine_process_cpu_us
                + direct_entry.close_process_cpu_us,
        "direct zlib writer CPU should contain only zlib API and entry-close CPU");
    const auto direct_reader = fastxlsx::detail::PackageReader::open(direct_output);
    check(direct_reader.read_entry("custom/payload.bin") == payload,
        "one-pass direct zlib output should round-trip exactly");

    fastxlsx::detail::PackageWriterTelemetry default_telemetry;
    fastxlsx::detail::PackageWriterOptions default_options;
    default_options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    default_options.compression_level = 1;
    default_options.telemetry = &default_telemetry;
    const std::filesystem::path default_output = files.path("default.zip");
    fastxlsx::detail::write_package(default_output,
        entries_for_file(source, payload.size(), payload_crc32), default_options);
    check(!payload_telemetry(default_telemetry).direct_zlib_raw,
        "production default should remain minizip-managed DEFLATE");
    const auto default_reader = fastxlsx::detail::PackageReader::open(default_output);
    check(default_reader.read_entry("custom/payload.bin") == payload,
        "default minizip output should remain readable");
}

void test_direct_zlib_guard_fallbacks(const TemporaryFiles& files)
{
    const std::string payload = large_payload();
    const std::filesystem::path source = files.path("guard-payload.bin");
    write_file(source, payload);

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.deflate_engine = fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    options.compression_level = 1;

    const std::string small_payload = payload.substr(0, 1024U * 1024U);
    const std::filesystem::path small_source = files.path("small-payload.bin");
    write_file(small_source, small_payload);
    fastxlsx::detail::PackageWriterTelemetry small_telemetry;
    options.telemetry = &small_telemetry;
    const std::filesystem::path small_output = files.path("small-fallback.zip");
    fastxlsx::detail::write_package(small_output,
        entries_for_file(small_source, small_payload.size(), crc32_of(small_payload)),
        options);
    check(!payload_telemetry(small_telemetry).direct_zlib_raw,
        "small staged entry should keep the minizip-managed path");
    check(fastxlsx::detail::PackageReader::open(small_output)
            .read_entry("custom/payload.bin") == small_payload,
        "small direct-zlib guard output should round-trip");

    fastxlsx::detail::PackageEntryChunk incomplete_chunk =
        fastxlsx::detail::PackageEntryChunk::file(source);
    incomplete_chunk.has_expected_size = true;
    incomplete_chunk.expected_size = payload.size();
    fastxlsx::detail::PackageWriterTelemetry incomplete_telemetry;
    options.telemetry = &incomplete_telemetry;
    const std::filesystem::path incomplete_output = files.path("incomplete-crc-fallback.zip");
    fastxlsx::detail::write_package(incomplete_output,
        {{"[Content_Types].xml", content_types_xml()},
            {"_rels/.rels", package_relationships_xml()},
            {"custom/payload.bin",
                std::vector<fastxlsx::detail::PackageEntryChunk> {
                    std::move(incomplete_chunk)}}},
        options);
    const auto& incomplete_entry = payload_telemetry(incomplete_telemetry);
    check(!incomplete_entry.direct_zlib_raw && !incomplete_entry.reused_staged_crc32,
        "entry without staged CRC should keep the minizip-managed path");
    check(fastxlsx::detail::PackageReader::open(incomplete_output)
            .read_entry("custom/payload.bin") == payload,
        "incomplete-CRC fallback output should round-trip");

    fastxlsx::detail::PackageWriterTelemetry stored_telemetry;
    options.compression_level = 0;
    options.telemetry = &stored_telemetry;
    const std::filesystem::path stored_output = files.path("stored-method-fallback.zip");
    fastxlsx::detail::write_package(stored_output,
        entries_for_file(source, payload.size(), crc32_of(payload)), options);
    check(!payload_telemetry(stored_telemetry).direct_zlib_raw,
        "stored compression should not activate raw DEFLATE");
    check(fastxlsx::detail::PackageReader::open(stored_output)
            .read_entry("custom/payload.bin") == payload,
        "stored-method direct-zlib guard output should round-trip");
}

void test_direct_zlib_detects_same_size_source_change(const TemporaryFiles& files)
{
    std::string payload = large_payload();
    const std::uint32_t expected_crc32 = crc32_of(payload);
    const std::filesystem::path source = files.path("mutated.bin");
    payload.front() = payload.front() == 'X' ? 'Y' : 'X';
    write_file(source, payload);

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.deflate_engine = fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw;
    options.compression_level = 1;
    bool threw = false;
    try {
        fastxlsx::detail::write_package(files.path("mutated.zip"),
            entries_for_file(source, payload.size(), expected_crc32), options);
    } catch (const fastxlsx::FastXlsxError&) {
        threw = true;
    }
    check(threw, "one-pass direct zlib should reject same-size staged CRC changes");
}

#endif

} // namespace

int main()
{
    try {
        const TemporaryFiles files;
        test_stored_backend_rejects_direct_zlib_engine(files);
        test_invalid_deflate_output_buffer_preserves_output(files);
#ifdef FASTXLSX_TEST_HAS_DIRECT_ZLIB_PROFILING
        test_explicit_direct_zlib_round_trip_and_default(files);
        test_direct_zlib_guard_fallbacks(files);
        test_direct_zlib_detects_same_size_source_change(files);
#else
        test_disabled_profile_rejects_direct_zlib_engine(files);
#endif
        std::cout << "package writer direct zlib tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "package writer direct zlib tests failed: " << error.what() << '\n';
        return 1;
    }
}
