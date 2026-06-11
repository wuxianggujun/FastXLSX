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
    editor.replace_sheet_data("Data",
        {
            {fastxlsx::CellValue::number(42.25), fastxlsx::CellValue::text("fresh")},
            {fastxlsx::CellValue::formula("SUM(A1:A1)")},
        });
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
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

} // namespace

int main()
{
    try {
        test_replaces_sheet_data_and_preserves_untouched_parts();
        test_worksheet_names_and_has_worksheet();
        test_missing_sheet_throws_and_editor_stays_usable();
        test_save_as_over_source_throws();
        test_empty_rows_emit_empty_sheet_data();
        test_text_uses_inline_strings_and_preserves_shared_strings();
        test_calc_metadata_requests_recalculation_without_inventing_calcchain();
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
