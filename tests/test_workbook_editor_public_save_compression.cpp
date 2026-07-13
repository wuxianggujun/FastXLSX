#include "../src/package_reader.hpp"
#include "../src/workbook_editor_package_diagnostics.hpp"
#include "test_workbook_editor_facade_common.hpp"

#include <algorithm>

void remove_artifact_if_present(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

const fastxlsx::detail::PackageReaderEntry& require_entry(
    const fastxlsx::detail::PackageReader& reader, std::string_view name)
{
    const fastxlsx::detail::PackageReaderEntry* entry = reader.find_entry(name);
    if (entry == nullptr) {
        throw std::runtime_error("expected ZIP entry is missing: " + std::string(name));
    }
    return *entry;
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
std::string read_raw_compressed_payload(const std::filesystem::path& path,
    const fastxlsx::detail::PackageReaderEntry& entry)
{
    const std::string package = fastxlsx::test::read_file(path);
    check(entry.data_offset <= package.size()
            && entry.compressed_size <= package.size() - entry.data_offset,
        "raw compressed payload should stay within the package");
    return package.substr(static_cast<std::size_t>(entry.data_offset),
        static_cast<std::size_t>(entry.compressed_size));
}

const fastxlsx::detail::PackageEditorOutputEntryPlan& require_output_plan_entry(
    const fastxlsx::detail::PackageEditorOutputPlan& plan, std::string_view name)
{
    const auto item = std::find_if(plan.entries.begin(), plan.entries.end(),
        [name](const fastxlsx::detail::PackageEditorOutputEntryPlan& entry) {
            return entry.entry_name == name;
        });
    if (item == plan.entries.end()) {
        throw std::runtime_error("expected output-plan entry is missing: "
            + std::string(name));
    }
    return *item;
}
#endif

void test_default_save_preserves_stored_output_compatibility()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-save-compression-default-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-save-compression-default-output.xlsx");
    remove_artifact_if_present(output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    editor.save_as(output);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(output);
    check(require_entry(reader, "xl/worksheets/sheet1.xml").compression_method == 0,
        "no-options WorkbookEditor save should retain stored ZIP output");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "default stored save should advance the saved watermark");
    check_contains(reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "default stored save should persist the queued Patch edit");
}

void test_save_options_validate_before_dirty_session_staging_and_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-save-compression-options-source.xlsx");
    const std::filesystem::path invalid_low_output = artifact(
        "fastxlsx-workbook-editor-save-compression-invalid-low.xlsx");
    const std::filesystem::path invalid_high_output = artifact(
        "fastxlsx-workbook-editor-save-compression-invalid-high.xlsx");
    const std::filesystem::path compressed_output = artifact(
        "fastxlsx-workbook-editor-save-compression-deflate.xlsx");
    remove_artifact_if_present(invalid_low_output);
    remove_artifact_if_present(invalid_high_output);
    remove_artifact_if_present(compressed_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell_value(1, 1, fastxlsx::CellValue::text("compressed retry value"));

    const std::size_t pending_count = editor.pending_change_count();
    const std::size_t unsaved_count = editor.unsaved_change_count();
    check(sheet.has_pending_changes(),
        "compression validation setup should keep one dirty WorksheetEditor session");

    fastxlsx::WorkbookEditorSaveOptions options;
    options.zip_compression_level = -2;
    check(threw_fastxlsx_error([&] { editor.save_as(invalid_low_output, options); }),
        "compression level below -1 should fail");
    options.zip_compression_level = 10;
    check(threw_fastxlsx_error([&] { editor.save_as(invalid_high_output, options); }),
        "compression level above 9 should fail");

    check(!std::filesystem::exists(invalid_low_output)
            && !std::filesystem::exists(invalid_high_output),
        "invalid compression settings should fail before creating output");
    check(sheet.has_pending_changes(),
        "invalid compression settings should not commit the dirty session handoff");
    check(editor.pending_change_count() == pending_count,
        "invalid compression settings should preserve pending count");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == unsaved_count,
        "invalid compression settings should preserve the unsaved watermark");

    options.zip_compression_level = 6;
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    editor.save_as(compressed_output, options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(compressed_output);
    const fastxlsx::detail::PackageReaderEntry& worksheet_entry =
        require_entry(reader, "xl/worksheets/sheet1.xml");
    check(worksheet_entry.compression_method == 8,
        "positive WorkbookEditor compression level should write DEFLATE worksheet output");
    check(worksheet_entry.compressed_size < worksheet_entry.uncompressed_size,
        "DEFLATE worksheet output should be smaller than its logical XML payload");
    check_contains(reader.read_entry("xl/worksheets/sheet1.xml"),
        "compressed retry value",
        "compressed save retry should persist the dirty WorksheetEditor value");
    check(!sheet.has_pending_changes(),
        "successful compressed save should commit the dirty session handoff");
    check(!editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "successful compressed save should advance the saved watermark");

    fastxlsx::WorkbookEditor reopened =
        fastxlsx::WorkbookEditor::open(compressed_output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "compressed WorkbookEditor output should reopen through the public facade");
#else
    check(threw_fastxlsx_error([&] { editor.save_as(compressed_output, options); }),
        "stored-only builds should reject positive DEFLATE levels");
    check(!std::filesystem::exists(compressed_output),
        "unavailable DEFLATE should fail before creating output");
    check(sheet.has_pending_changes(),
        "unavailable DEFLATE should preserve the dirty session for retry");
    check(editor.pending_change_count() == pending_count
            && editor.unsaved_change_count() == unsaved_count,
        "unavailable DEFLATE should preserve public save state");

    options.zip_compression_level = 0;
    editor.save_as(compressed_output, options);
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(compressed_output);
    check(require_entry(reader, "xl/worksheets/sheet1.xml").compression_method == 0,
        "stored-only retry should write stored worksheet output");
    check_contains(reader.read_entry("xl/worksheets/sheet1.xml"),
        "compressed retry value",
        "stored-only retry should persist the dirty WorksheetEditor value");
#endif
}

void test_backend_default_compression_uses_active_profile()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-save-compression-auto-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-save-compression-auto-output.xlsx");
    remove_artifact_if_present(output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditorSaveOptions options;
    options.zip_compression_level = -1;
    editor.save_as(output, options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(output);
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    check(require_entry(reader, "xl/worksheets/sheet1.xml").compression_method == 8,
        "backend-default WorkbookEditor save should use production DEFLATE");
#else
    check(require_entry(reader, "xl/worksheets/sheet1.xml").compression_method == 0,
        "backend-default WorkbookEditor save should use stored bootstrap output");
#endif
}

void test_matching_deflate_save_raw_copies_unchanged_entries()
{
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-save-raw-copy-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-save-raw-copy-output.xlsx");
    remove_artifact_if_present(output);

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source);
    const fastxlsx::detail::PackageReaderEntry& source_untouched =
        require_entry(source_reader, "xl/worksheets/sheet2.xml");
    check(source_untouched.compression_method == 8,
        "production source should use DEFLATE for raw-copy coverage");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    fastxlsx::detail::PackageWriterOptions package_options;
    package_options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    package_options.compression_level = 1;
    const fastxlsx::detail::PackageEditorOutputPlan plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(
            editor, package_options);
    const auto& untouched_plan =
        require_output_plan_entry(plan, "xl/worksheets/sheet2.xml");
    check(untouched_plan.copied_from_source
            && untouched_plan.raw_compressed_source_copy,
        "matching DEFLATE save should plan unchanged worksheet raw-copy");
    check(untouched_plan.raw_compressed_source_bytes
            == source_untouched.compressed_size,
        "raw-copy plan should expose the source compressed byte count");
    check(!require_output_plan_entry(plan, "xl/workbook.xml")
                .raw_compressed_source_copy,
        "rewritten workbook metadata should not use raw-copy");

    fastxlsx::WorkbookEditorSaveOptions save_options;
    save_options.zip_compression_level = 1;
    editor.save_as(output, save_options);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const fastxlsx::detail::PackageReaderEntry& output_untouched =
        require_entry(output_reader, "xl/worksheets/sheet2.xml");
    check(output_reader.read_entry("xl/worksheets/sheet2.xml")
            == source_reader.read_entry("xl/worksheets/sheet2.xml"),
        "raw-copy save should preserve unchanged worksheet logical bytes");
    check(read_raw_compressed_payload(output, output_untouched)
            == read_raw_compressed_payload(source, source_untouched),
        "raw-copy save should preserve unchanged worksheet compressed bytes exactly");
#endif
}

} // namespace

int main()
{
    try {
        test_default_save_preserves_stored_output_compatibility();
        test_save_options_validate_before_dirty_session_staging_and_retry();
        test_backend_default_compression_uses_active_profile();
        test_matching_deflate_save_raw_copies_unchanged_entries();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr,
            "%d WorkbookEditor public save-compression check(s) failed\n",
            g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public save-compression tests passed\n");
    return 0;
}
