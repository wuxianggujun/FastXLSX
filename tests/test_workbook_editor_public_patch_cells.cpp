// Public Patch-mode WorkbookEditor targeted cell replacement tests.
//
// These tests stay on the public WorkbookEditor facade and verify that
// replace_cells() and its explicit missing-cell policy use the large-worksheet
// Patch transformer path without materializing the worksheet through
// WorksheetEditor. replace_or_insert_cells() is covered as a compatibility
// wrapper for the Insert policy.

#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
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
    if (haystack.find(needle) == std::string::npos) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n  missing: %.*s\n  in: %s\n",
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(needle.size()), needle.data(), haystack.c_str());
    }
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    if (haystack.find(needle) != std::string::npos) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n  unexpected: %.*s\n  in: %s\n",
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(needle.size()), needle.data(), haystack.c_str());
    }
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

std::filesystem::path write_patch_cells_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("old-a1"),
            fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::boolean(true)});
        data.append_row({fastxlsx::CellView::text("old-a2"),
            fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::boolean(false)});
        data.append_row({fastxlsx::CellView::text("old-a3"),
            fastxlsx::CellView::number(3.0),
            fastxlsx::CellView::formula("A1+B1")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();
    return path;
}

void check_patch_cells_clean_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.pending_targeted_cell_replacement_count() == 0,
        prefix + " should have no targeted cell diagnostics");
    check(editor.pending_targeted_cell_replacement_worksheet_names().empty(),
        prefix + " should have no targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should have no targeted cell payload bytes");
    check(!editor.has_pending_targeted_cell_replacement("Data"),
        prefix + " should not report Data targeted patches");
}

void test_replace_cells_patches_existing_cells_and_preserves_unrelated_parts()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data", {
        {{1, 1}, fastxlsx::CellValue::number(42.0)},
        {{2, 2}, fastxlsx::CellValue::text("patched & value")},
        {{2, 3}, fastxlsx::CellValue::error("#VALUE!")},
        {{3, 2}, fastxlsx::CellValue::blank()},
        {{3, 3}, fastxlsx::CellValue::formula("A1+B1")},
    });

    check(editor.has_pending_changes(),
        "replace_cells should mark WorkbookEditor as pending");
    check(editor.pending_change_count() == 1,
        "replace_cells should increment public pending change count");
    check(editor.pending_replacement_cell_count() == 0,
        "replace_cells should not count whole-sheet replacement cells");
    check(editor.pending_targeted_cell_replacement_count() == 5,
        "replace_cells should expose targeted cell count");
    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == std::vector<std::string> {"Data"},
        "replace_cells should expose targeted worksheet name");
    check(editor.has_pending_targeted_cell_replacement("Data"),
        "replace_cells should expose targeted worksheet predicate");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() > 0,
        "replace_cells should expose staged replacement cell XML bytes");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1, "replace_cells should expose one pending summary");
    if (summaries.size() == 1) {
        check(summaries[0].source_name == "Data",
            "replace_cells summary should use source sheet name");
        check(summaries[0].planned_name == "Data",
            "replace_cells summary should use planned sheet name");
        check(!summaries[0].sheet_data_replaced,
            "replace_cells summary should not report whole-sheet replacement");
        check(summaries[0].targeted_cells_replaced,
            "replace_cells summary should report targeted cells");
        check(summaries[0].targeted_cell_replacement_count == 5,
            "replace_cells summary should report targeted count");
        check(summaries[0].estimated_targeted_cell_replacement_xml_bytes > 0,
            "replace_cells summary should report targeted payload bytes");
    }

    editor.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string& untouched_sheet = output_entries.at("xl/worksheets/sheet2.xml");

    check_contains(data_sheet, "<dimension ref=\"A1:C3\"",
        "replace_cells should preserve/refresh worksheet dimension");
    check_contains(data_sheet, "<c r=\"A1\"><v>42</v></c>",
        "replace_cells should replace A1 as a number");
    check_contains(data_sheet,
        "<c r=\"B2\" t=\"inlineStr\"><is><t>patched &amp; value</t></is></c>",
        "replace_cells should replace B2 as escaped inline text");
    check_contains(data_sheet, "<c r=\"C2\" t=\"e\"><v>#VALUE!</v></c>",
        "replace_cells should replace C2 as an error token");
    check_contains(data_sheet, "<c r=\"B3\"/>",
        "replace_cells should replace B3 as explicit blank");
    check_contains(data_sheet, "<c r=\"C3\"><f>A1+B1</f></c>",
        "replace_cells should replace C3 as formula text");
    check_contains(data_sheet, "<c r=\"C1\" t=\"b\"><v>1</v></c>",
        "replace_cells should preserve untouched source cells");
    check_not_contains(data_sheet, "old-a1",
        "replace_cells should remove old A1 payload");
    check(untouched_sheet == source_entries.at("xl/worksheets/sheet2.xml"),
        "replace_cells should preserve untouched worksheet bytes");
    check_contains(output_entries.at("xl/workbook.xml"), "fullCalcOnLoad=\"1\"",
        "replace_cells should request workbook recalculation");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "replace_cells should not create calcChain.xml");
}

void test_replace_cells_rejects_missing_target_without_public_state_pollution()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const bool threw = threw_fastxlsx_error([&] {
        editor.replace_cells("Data", {{{9, 9}, fastxlsx::CellValue::text("missing")}});
    });

    check(threw, "replace_cells should reject missing target cells");
    check(!editor.has_pending_changes(),
        "failed replace_cells should not mark pending changes");
    check(editor.pending_change_count() == 0,
        "failed replace_cells should not increment public count");
    check_patch_cells_clean_diagnostics(editor,
        "missing-target replace_cells failure");
    check(editor.last_edit_error().has_value(),
        "failed replace_cells should record last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorkbookEditor::replace_cells() failed",
            "failed replace_cells diagnostic should include public API name");
        check_contains(*editor.last_edit_error(), "not found",
            "failed replace_cells diagnostic should include missing-target context");
    }

    editor.save_as(output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/worksheets/sheet1.xml")
            == source_entries.at("xl/worksheets/sheet1.xml"),
        "failed replace_cells should leave saved worksheet unchanged");
}

void test_replace_cells_rejects_unknown_missing_cell_policy_without_public_state_pollution()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-policy-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const bool threw = threw_fastxlsx_error([&] {
        editor.replace_cells("Data",
            {{{1, 1}, fastxlsx::CellValue::text("policy")}},
            static_cast<fastxlsx::CellPatchMissingCellPolicy>(99));
    });

    check(threw, "replace_cells should reject unknown missing-cell policy");
    check(!editor.has_pending_changes(),
        "failed policy validation should not mark pending changes");
    check(editor.pending_change_count() == 0,
        "failed policy validation should not increment public count");
    check_patch_cells_clean_diagnostics(
        editor, "unknown missing-cell policy replace_cells failure");
    check(editor.last_edit_error().has_value(),
        "failed policy validation should record last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorkbookEditor::replace_cells() failed",
            "failed policy validation diagnostic should include public API name");
        check_contains(*editor.last_edit_error(), "unknown CellPatchMissingCellPolicy",
            "failed policy validation diagnostic should include policy context");
    }
}

void test_replace_cells_insert_policy_patches_existing_and_inserts_missing_cells_and_rows()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-upsert-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-upsert-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data",
        {
            {{1, 1}, fastxlsx::CellValue::number(88.0)},
            {{1, 4}, fastxlsx::CellValue::text("inserted d1")},
            {{4, 2}, fastxlsx::CellValue::formula("A1+B1")},
        },
        fastxlsx::CellPatchMissingCellPolicy::Insert);

    check(editor.has_pending_changes(),
        "replace_cells Insert policy should mark WorkbookEditor as pending");
    check(editor.pending_change_count() == 1,
        "replace_cells Insert policy should increment public pending count");
    check(editor.pending_targeted_cell_replacement_count() == 3,
        "replace_cells Insert policy should expose targeted cell count");
    check(editor.has_pending_targeted_cell_replacement("Data"),
        "replace_cells Insert policy should reuse targeted diagnostics");

    editor.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");

    check_contains(data_sheet, "<dimension ref=\"A1:D4\"",
        "replace_cells Insert policy should refresh dimension for inserted cells and rows");
    check_contains(data_sheet, "<c r=\"A1\"><v>88</v></c>",
        "replace_cells Insert policy should replace existing A1");
    check_contains(data_sheet,
        "<c r=\"D1\" t=\"inlineStr\"><is><t>inserted d1</t></is></c>",
        "replace_cells Insert policy should insert missing D1 into an existing row");
    check_contains(data_sheet, "<row r=\"4\"><c r=\"B4\"><f>A1+B1</f></c></row>",
        "replace_cells Insert policy should synthesize missing row 4");
    check_not_contains(data_sheet, "old-a1",
        "replace_cells Insert policy should remove replaced old A1 payload");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "replace_cells Insert policy should preserve untouched worksheet bytes");
    check_contains(output_entries.at("xl/workbook.xml"), "fullCalcOnLoad=\"1\"",
        "replace_cells Insert policy formula should request workbook recalculation");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "replace_cells Insert policy should not create calcChain.xml");
}

void test_replace_or_insert_cells_wrapper_delegates_to_insert_policy()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-upsert-wrapper-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-upsert-wrapper-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_or_insert_cells("Data",
        {{{1, 4}, fastxlsx::CellValue::text("wrapper inserted")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet, "<dimension ref=\"A1:D3\"",
        "replace_or_insert_cells wrapper should refresh dimension through Insert policy");
    check_contains(data_sheet,
        "<c r=\"D1\" t=\"inlineStr\"><is><t>wrapper inserted</t></is></c>",
        "replace_or_insert_cells wrapper should insert missing cells");
}

void test_replace_cells_can_follow_up_on_upserted_planned_cells()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-upsert-followup-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-upsert-followup-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data",
        {{{4, 2}, fastxlsx::CellValue::text("first inserted")}},
        fastxlsx::CellPatchMissingCellPolicy::Insert);
    editor.replace_cells("Data",
        {{{4, 2}, fastxlsx::CellValue::text("followup replacement")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<row r=\"4\"><c r=\"B4\" t=\"inlineStr\"><is><t>followup replacement</t></is></c></row>",
        "replace_cells should be able to rewrite a cell inserted by prior upsert");
    check_not_contains(data_sheet, "first inserted",
        "follow-up replace_cells should consume the earlier upsert payload");
}

void test_replace_cells_mode_mixing_guards_and_empty_noop()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-guards-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_cells("Data", {});
        check(!editor.has_pending_changes(),
            "empty replace_cells should not queue pending changes");
        check_patch_cells_clean_diagnostics(editor, "empty replace_cells");
        check(!editor.last_edit_error().has_value(),
            "empty replace_cells should clear last_edit_error");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_cells("Data", {{{1, 1}, fastxlsx::CellValue::text("patched")}});
        check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
            "worksheet() should reject a sheet after replace_cells");
        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("whole")}}); }),
            "replace_sheet_data should reject a sheet after replace_cells");
        check(editor.pending_targeted_cell_replacement_count() == 1,
            "mode guard failures should preserve targeted cell diagnostics");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        (void)editor.worksheet("Data");
        check(threw_fastxlsx_error([&] {
            editor.replace_cells("Data", {{{1, 1}, fastxlsx::CellValue::text("patched")}});
        }), "replace_cells should reject a materialized worksheet");
        check_patch_cells_clean_diagnostics(editor,
            "materialized replace_cells rejection");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("whole")}});
        check(threw_fastxlsx_error([&] {
            editor.replace_cells("Data", {{{1, 1}, fastxlsx::CellValue::text("patched")}});
        }), "replace_cells should reject a whole-sheet replacement");
        check_patch_cells_clean_diagnostics(editor,
            "whole-sheet replacement replace_cells rejection");
    }
}

void test_replace_cells_follows_planned_catalog_after_rename()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-rename-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data", {{{1, 1}, fastxlsx::CellValue::text("renamed patch")}});
    editor.rename_sheet("Data", "RenamedData");

    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == std::vector<std::string> {"RenamedData"},
        "rename_sheet should move replace_cells diagnostics to planned name");
    check(editor.has_pending_targeted_cell_replacement("RenamedData"),
        "rename_sheet should expose targeted diagnostics under planned name");
    check(!editor.has_pending_targeted_cell_replacement("Data"),
        "rename_sheet should remove targeted diagnostics from old name");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        "rename after replace_cells should expose one summary");
    if (summaries.size() == 1) {
        check(summaries[0].source_name == "Data",
            "rename summary should keep source sheet name");
        check(summaries[0].planned_name == "RenamedData",
            "rename summary should expose planned sheet name");
        check(summaries[0].renamed,
            "rename summary should mark renamed");
        check(summaries[0].targeted_cells_replaced,
            "rename summary should keep targeted cells flag");
        check(summaries[0].targeted_cell_replacement_count == 1,
            "rename summary should keep targeted count");
    }

    check(threw_fastxlsx_error([&] {
        editor.replace_cells("Data", {{{1, 2}, fastxlsx::CellValue::text("old name")}});
    }), "replace_cells should use planned catalog after rename");
    editor.replace_cells("RenamedData",
        {{{1, 2}, fastxlsx::CellValue::text("second patch")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), "name=\"RenamedData\"",
        "rename after replace_cells should update workbook catalog");
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>renamed patch</t></is></c>",
        "rename after replace_cells should preserve first targeted patch");
    check_contains(data_sheet,
        "<c r=\"B1\" t=\"inlineStr\"><is><t>second patch</t></is></c>",
        "replace_cells should accept planned name after rename");
}

void test_replace_cells_duplicate_targets_keep_latest_payload()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-duplicates-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-duplicates-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data", {
        {{1, 1}, fastxlsx::CellValue::text("first")},
        {{1, 1}, fastxlsx::CellValue::text("second")},
        {{1, 2}, fastxlsx::CellValue::number(7.0)},
    });
    check(editor.pending_targeted_cell_replacement_count() == 2,
        "duplicate targets should count unique final coordinates");
    editor.replace_cells("Data", {{{1, 1}, fastxlsx::CellValue::text("third")}});
    check(editor.pending_targeted_cell_replacement_count() == 2,
        "later same-coordinate call should replace diagnostic, not add count");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>third</t></is></c>",
        "later replace_cells call should win for duplicate target");
    check_not_contains(data_sheet, ">first<",
        "duplicate replace_cells should not retain first payload");
    check_not_contains(data_sheet, ">second<",
        "later replace_cells call should not retain previous payload");
}

} // namespace

int main()
{
    try {
        test_replace_cells_patches_existing_cells_and_preserves_unrelated_parts();
        test_replace_cells_rejects_missing_target_without_public_state_pollution();
        test_replace_cells_rejects_unknown_missing_cell_policy_without_public_state_pollution();
        test_replace_cells_insert_policy_patches_existing_and_inserts_missing_cells_and_rows();
        test_replace_or_insert_cells_wrapper_delegates_to_insert_policy();
        test_replace_cells_can_follow_up_on_upserted_planned_cells();
        test_replace_cells_mode_mixing_guards_and_empty_noop();
        test_replace_cells_follows_planned_catalog_after_rename();
        test_replace_cells_duplicate_targets_keep_latest_payload();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Unhandled exception: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor public patch-cells check(s) failed\n",
            g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public patch-cells tests passed\n");
    return 0;
}
