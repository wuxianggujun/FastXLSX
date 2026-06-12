// Structure tests for the public Patch-mode WorkbookEditor facade.
//
// These tests build a real source workbook through the public WorkbookWriter,
// edit it through WorkbookEditor::replace_sheet_data(), and verify the output
// package through the shared ZIP test reader.
// They intentionally stay at the public-API level and do not touch the internal
// PackageEditor test surface in test_package_editor.cpp.

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n",
            static_cast<int>(message.size()), message.data());
    }
}

void check_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) != std::string::npos, message);
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) == std::string::npos, message);
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

// Writes a small two-sheet workbook through the public streaming writer. The
// first sheet carries placeholder rows to be replaced; the second sheet is left
// untouched so preservation can be checked.
std::filesystem::path write_two_sheet_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

// Writes a source workbook with document properties so patch tests can verify
// that WorkbookEditor preserves docProps bytes through save_as().
std::filesystem::path write_two_sheet_source_with_document_properties(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriterOptions options;
    options.document_properties.creator = "WorkbookEditor Tests";
    options.document_properties.last_modified_by = "WorkbookEditor Tests";
    options.document_properties.title = "WorkbookEditor preservation";
    options.document_properties.subject = "FastXLSX";
    options.document_properties.description = "WorkbookEditor docProps preservation source";
    options.document_properties.keywords = "FastXLSX,WorkbookEditor,Patch";
    options.document_properties.category = "tests";
    options.document_properties.application = "FastXLSX";
    options.document_properties.app_version = "1.0";

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

void test_replaces_sheet_data_and_preserves_untouched_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-replace-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.replace_sheet_data("Data",
        {
            {fastxlsx::CellValue::number(42.25), fastxlsx::CellValue::text("fresh")},
            {fastxlsx::CellValue::formula("SUM(A1:A1)")},
        });
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, R"(<v>7</v>)",
        "second replacement should overwrite earlier queued data");
    check_contains(worksheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "replaced sheet should carry new numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>fresh</t></is></c>)",
        "replaced sheet should carry new inline text cell");
    check_contains(worksheet_xml, "<f>SUM(A1:A1)</f>",
        "replaced sheet should carry new formula cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replaced sheet should drop old placeholder data");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "replaced sheet should drop old placeholder data");

    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "untouched worksheet bytes should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types bytes should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships bytes should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships bytes should be preserved");
}

void test_worksheet_names_and_has_worksheet()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-names-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2, "worksheet_names should list both sheets");
    check(names.size() == 2 && names[0] == "Data",
        "worksheet_names should list sheets in catalog order");
    check(names.size() == 2 && names[1] == "Untouched",
        "worksheet_names should list the second sheet name");

    check(editor.has_worksheet("Data"), "has_worksheet should find an existing sheet");
    check(editor.has_worksheet("Untouched"), "has_worksheet should find the second sheet");
    check(!editor.has_worksheet("Missing"),
        "has_worksheet should reject an absent sheet name");
}

void test_pending_change_diagnostics_track_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-pending-source.xlsx");

    fastxlsx::WorkbookEditor clean_editor = fastxlsx::WorkbookEditor::open(source);
    check(!clean_editor.has_pending_changes(),
        "newly opened editor should report no pending changes");
    check(clean_editor.pending_change_count() == 0,
        "newly opened editor should report zero pending changes");
    check(clean_editor.pending_replacement_cell_count() == 0,
        "newly opened editor should report zero pending replacement cells");
    check(clean_editor.estimated_pending_replacement_memory_usage() == 0,
        "newly opened editor should report zero pending replacement memory");

    check(threw_fastxlsx_error([&] {
        clean_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    }), "rejected replace_sheet_data should throw FastXlsxError");
    check(!clean_editor.has_pending_changes(),
        "rejected replace_sheet_data should not mark the editor dirty");
    check(clean_editor.pending_change_count() == 0,
        "rejected replace_sheet_data should not add pending changes");

    check(threw_fastxlsx_error([&] { clean_editor.rename_sheet("Data", "Bad/Name"); }),
        "rejected rename_sheet should throw FastXlsxError");
    check(!clean_editor.has_pending_changes(),
        "rejected rename_sheet should not mark the editor dirty");
    check(clean_editor.pending_change_count() == 0,
        "rejected rename_sheet should not add pending changes");

    clean_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(clean_editor.has_pending_changes(),
        "successful replace_sheet_data should mark the editor dirty");
    check(clean_editor.pending_change_count() > 0,
        "successful replace_sheet_data should expose a coarse pending count");
    check(clean_editor.pending_replacement_cell_count() == 1,
        "successful replace_sheet_data should expose final queued replacement cells");
    check(clean_editor.estimated_pending_replacement_memory_usage() > 0,
        "successful replace_sheet_data should expose estimated replacement memory");

    fastxlsx::WorkbookEditor rename_editor = fastxlsx::WorkbookEditor::open(source);
    rename_editor.rename_sheet("Data", "Renamed");
    check(rename_editor.has_pending_changes(),
        "successful rename_sheet should mark the editor dirty");
    check(rename_editor.pending_change_count() > 0,
        "successful rename_sheet should expose a coarse pending count");
    check(rename_editor.pending_replacement_cell_count() == 0,
        "rename_sheet should not add replacement cells");
    check(rename_editor.estimated_pending_replacement_memory_usage() == 0,
        "rename_sheet should not add replacement memory");
}

void test_replacement_guardrails_and_payload_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-guardrails-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-guardrails-output.xlsx");

    fastxlsx::WorkbookEditorOptions max_cell_options;
    max_cell_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor max_cell_editor =
        fastxlsx::WorkbookEditor::open(source, max_cell_options);

    check(threw_fastxlsx_error([&] {
        max_cell_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    }), "replace_sheet_data should enforce max_replacement_cells before commit");
    check(!max_cell_editor.has_pending_changes(),
        "max_replacement_cells failure should not mark the editor dirty");
    check(max_cell_editor.pending_replacement_cell_count() == 0,
        "max_replacement_cells failure should not record replacement cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() == 0,
        "max_replacement_cells failure should not record replacement memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(3.0)}});
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "valid guarded replacement should record one queued cell");
    const std::size_t first_memory =
        max_cell_editor.estimated_pending_replacement_memory_usage();
    check(first_memory > 0,
        "valid guarded replacement should record non-zero estimated memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(4.0)}});
    check(max_cell_editor.pending_change_count() == 2,
        "repeated same-sheet replacement should still count public edit calls");
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "repeated same-sheet replacement should report only final queued cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() > 0,
        "repeated same-sheet replacement should keep final estimated memory");

    max_cell_editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "guarded replacement output should use the final queued payload");
    check_not_contains(worksheet_xml, R"(<v>3</v>)",
        "guarded replacement output should drop stale same-sheet payload");

    fastxlsx::WorkbookEditorOptions memory_options;
    memory_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(source, memory_options);
    check(threw_fastxlsx_error([&] {
        memory_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "replace_sheet_data should enforce replacement_memory_budget_bytes before commit");
    check(!memory_editor.has_pending_changes(),
        "memory budget failure should not mark the editor dirty");
    check(memory_editor.pending_replacement_cell_count() == 0,
        "memory budget failure should not record replacement cells");
    check(memory_editor.estimated_pending_replacement_memory_usage() == 0,
        "memory budget failure should not record replacement memory");
}

void test_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    }), "replacing a missing sheet should throw FastXlsxError");

    // The editor must remain usable after a rejected edit.
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>7</v></c>)",
        "editor should still apply a valid edit after a rejected one");
}

void test_save_as_over_source_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-overwrite-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should throw FastXlsxError");
}

void test_empty_rows_emit_empty_sheet_data()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-empty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-empty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<sheetData></sheetData>",
        "empty rows should emit an empty sheetData");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "empty replacement should drop old placeholder data");
}

void test_text_uses_inline_strings_and_preserves_shared_strings()
{
    // Build a shared-string source so we can prove the table is preserved, not
    // migrated, when the replacement uses inline strings.
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-shared-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-placeholder")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part");
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-shared-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("inline-text")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(t="inlineStr")",
        "replacement text should be written as an inline string");
    check_contains(worksheet_xml, "inline-text",
        "replacement text should appear in the worksheet");

    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "existing sharedStrings should be preserved, not migrated or pruned");
}

void test_calc_metadata_requests_recalculation_without_inventing_calcchain()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-calc-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-calc-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "streaming writer source should not carry a calcChain");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::formula("SUM(A1:A1)")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "data edit should request workbook recalculation on load");

    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "editor should not invent a calcChain when the source has none");
}

void test_rename_sheet_changes_catalog_name_and_preserves_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "Renamed & Data");

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2 && names[0] == "Data",
        "source workbook view should stay on the original catalog after rename");
    check(names.size() == 2 && names[1] == "Untouched",
        "source workbook view should keep the untouched sheet name after rename");
    check(editor.has_worksheet("Data"),
        "source workbook view should still expose the original sheet name after rename");
    check(!editor.has_worksheet("Renamed & Data"),
        "source workbook view should not expose the planned rename before save");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "rename should XML-escape the new sheet name in the output catalog");
    check_not_contains(workbook_xml, R"(name="Data")",
        "rename should drop the old sheet name from the output catalog");

    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "rename should preserve the renamed sheet's worksheet bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "rename should preserve untouched worksheet bytes");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "rename should preserve content types bytes");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "rename should preserve package relationships bytes");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "rename should preserve workbook relationships bytes");

    // Reopening the output package confirms the new catalog name is the one a
    // reader sees, and the other sheet is unchanged.
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const std::vector<std::string> reopened_names = reopened.worksheet_names();
    check(reopened_names.size() == 2 && reopened_names[0] == "Renamed & Data",
        "reopened output should expose the renamed sheet in catalog order");
    check(reopened_names.size() == 2 && reopened_names[1] == "Untouched",
        "reopened output should keep the untouched sheet name");
    check(!reopened.has_worksheet("Data"),
        "reopened output should no longer expose the old sheet name");
}

void test_docprops_are_preserved_through_patch()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties("fastxlsx-workbook-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-docprops-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string core_before = source_entries.at("docProps/core.xml");
    const std::string app_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(123.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("docProps/core.xml") == core_before,
        "patch save should preserve docProps/core.xml bytes");
    check(output_entries.at("docProps/app.xml") == app_before,
        "patch save should preserve docProps/app.xml bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>123</v>)",
        "patch save should still apply the requested workbook edit");
}

void test_rename_to_existing_name_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-dup-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-dup-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "renaming to an existing sheet name should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "untouched"); }),
        "renaming to an ASCII case-insensitive duplicate should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a rejected one");
}

void test_rename_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Missing", "Renamed"); }),
        "renaming a missing sheet should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a missing-sheet rejection");
}

void test_rename_to_invalid_name_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "renaming to a sheet name with invalid characters should throw FastXlsxError");
}

} // namespace

int main()
{
    try {
        test_replaces_sheet_data_and_preserves_untouched_parts();
        test_worksheet_names_and_has_worksheet();
        test_pending_change_diagnostics_track_public_edits();
        test_replacement_guardrails_and_payload_diagnostics();
        test_missing_sheet_throws_and_editor_stays_usable();
        test_save_as_over_source_throws();
        test_empty_rows_emit_empty_sheet_data();
        test_text_uses_inline_strings_and_preserves_shared_strings();
        test_calc_metadata_requests_recalculation_without_inventing_calcchain();
        test_rename_sheet_changes_catalog_name_and_preserves_parts();
        test_docprops_are_preserved_through_patch();
        test_rename_to_existing_name_throws_and_editor_stays_usable();
        test_rename_missing_sheet_throws_and_editor_stays_usable();
        test_rename_to_invalid_name_throws();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor tests passed\n");
    return 0;
}
