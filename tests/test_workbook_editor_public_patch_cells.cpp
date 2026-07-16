// Public Patch-mode WorkbookEditor targeted cell replacement tests.
//
// These tests stay on the public WorkbookEditor facade and verify that
// replace_cells() and its explicit missing-cell policy use the large-worksheet
// Patch path without materializing the worksheet through WorksheetEditor.

#include "../src/workbook_editor_package_diagnostics.hpp"

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

bool has_note_containing(
    const std::vector<std::string>& notes,
    std::initializer_list<std::string_view> needles)
{
    for (const std::string& note : notes) {
        bool found = true;
        for (const std::string_view needle : needles) {
            if (note.find(needle) == std::string::npos) {
                found = false;
                break;
            }
        }
        if (found) {
            return true;
        }
    }
    return false;
}

const fastxlsx::detail::PackageEditorOutputEntryPlan* find_output_entry_plan(
    const fastxlsx::detail::PackageEditorOutputPlan& plan,
    std::string_view entry_name)
{
    for (const fastxlsx::detail::PackageEditorOutputEntryPlan& entry : plan.entries) {
        if (entry.entry_name == entry_name) {
            return &entry;
        }
    }
    return nullptr;
}

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

std::filesystem::path write_patch_cells_source_impl(
    std::string_view name, bool force_stored_archive)
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
    if (force_stored_archive) {
        const std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(path);
        fastxlsx::test::write_stored_zip_entries(path, entries);
    }
    return path;
}

std::filesystem::path write_patch_cells_source(std::string_view name)
{
    // Direct-range assertions require stored source entries. Keep this fixture
    // backend-neutral; compressed-source fallback has separate minizip coverage.
    return write_patch_cells_source_impl(name, true);
}

std::filesystem::path write_patch_cells_source_with_external_hyperlink(
    std::string_view name)
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
        data.add_external_hyperlink(1, 3, "https://example.invalid/data");
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    const std::map<std::string, std::string> entries =
        fastxlsx::test::read_zip_entries(path);
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
std::filesystem::path write_patch_cells_default_backend_source(std::string_view name)
{
    return write_patch_cells_source_impl(name, false);
}
#endif

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

void check_patch_cells_materialized_guard_error(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    const std::optional<std::string> error = editor.last_edit_error();
    check(error.has_value(), prefix + " should record last_edit_error");
    if (error.has_value()) {
        check_contains(*error, "WorkbookEditor::replace_cells() failed",
            prefix + " should report the public replace_cells wrapper");
        check_contains(*error,
            "cannot replace cells after materializing planned worksheet session",
            prefix + " should report the materialized-session guard");
    }
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

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(editor);
    check(has_note_containing(output_plan.notes,
              {"indexed source-entry direct-range", "matched 5 replacement targets"}),
        "replace_cells should expose the indexed source-entry direct-range fast path");
    check(has_note_containing(output_plan.notes,
              {"indexed source-entry fast path", "source package file ranges"}),
        "replace_cells should report source package file-range preservation");
    const auto* data_sheet_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml");
    check(data_sheet_plan != nullptr,
        "replace_cells output plan should include the edited worksheet");
    if (data_sheet_plan != nullptr) {
        check(data_sheet_plan->staged_replacement_chunks,
            "replace_cells should stage direct-range worksheet chunks");
        check(!data_sheet_plan->materialized_replacement,
            "replace_cells should not materialize the rewritten worksheet");
        check(data_sheet_plan->staged_replacement_file_range_chunk_count > 0,
            "replace_cells should preserve untouched worksheet XML as file ranges");
        check(data_sheet_plan->staged_replacement_memory_chunk_count > 0
                && data_sheet_plan->staged_replacement_memory_chunk_count < 5,
            "replace_cells should merge adjacent replacement payload memory chunks");
        check(data_sheet_plan->staged_replacement_memory_bytes
                == editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
            "replace_cells should keep all staged replacement payload bytes after merging chunks");
        check(data_sheet_plan->indexed_source_entry_direct_range,
            "replace_cells output plan should expose structured direct-range telemetry");
        check(data_sheet_plan->indexed_source_entry_scanned_source_cell_count == 9,
            "replace_cells output plan should report the scanned source cell count");
        check(data_sheet_plan->indexed_source_entry_matched_replacement_count == 5,
            "replace_cells output plan should report the matched target count");
        check(data_sheet_plan->indexed_source_entry_staged_output_bytes
                == data_sheet_plan->staged_replacement_expected_bytes,
            "replace_cells output plan should report staged worksheet output bytes");
        check_contains(data_sheet_plan->reason, "indexed direct-range",
            "replace_cells worksheet plan reason should name the fast path");
    }

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

void test_replace_cells_insert_policy_uses_direct_range_when_all_targets_exist()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-upsert-existing-direct-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-upsert-existing-direct-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data",
        {
            {{1, 1}, fastxlsx::CellValue::text("existing insert policy")},
            {{2, 2}, fastxlsx::CellValue::number(77.0)},
            {{3, 3}, fastxlsx::CellValue::formula("A1+B1")},
        },
        fastxlsx::CellPatchMissingCellPolicy::Insert);

    check(editor.pending_targeted_cell_replacement_count() == 3,
        "replace_cells Insert existing-only policy should expose targeted cell count");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() > 0,
        "replace_cells Insert existing-only policy should expose staged payload bytes");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(editor);
    check(has_note_containing(output_plan.notes,
              {"indexed source-entry direct-range", "matched 3 replacement targets"}),
        "replace_cells Insert existing-only policy should use direct-range fast path");
    const auto* data_sheet_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml");
    check(data_sheet_plan != nullptr,
        "replace_cells Insert existing-only output plan should include the edited worksheet");
    if (data_sheet_plan != nullptr) {
        check(data_sheet_plan->staged_replacement_chunks,
            "replace_cells Insert existing-only should stage direct-range worksheet chunks");
        check(!data_sheet_plan->materialized_replacement,
            "replace_cells Insert existing-only should not materialize the rewritten worksheet");
        check(data_sheet_plan->indexed_source_entry_direct_range,
            "replace_cells Insert existing-only should expose structured direct-range telemetry");
        check(data_sheet_plan->indexed_source_entry_matched_replacement_count == 3,
            "replace_cells Insert existing-only should report every matched target");
        check(data_sheet_plan->staged_replacement_file_range_chunk_count > 0,
            "replace_cells Insert existing-only should preserve untouched XML as file ranges");
        check(data_sheet_plan->staged_replacement_memory_bytes
                == editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
            "replace_cells Insert existing-only should account for all replacement payload bytes");
    }

    editor.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet, "<dimension ref=\"A1:C3\"",
        "replace_cells Insert existing-only should preserve worksheet dimension");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>existing insert policy</t></is></c>",
        "replace_cells Insert existing-only should replace A1 text");
    check_contains(data_sheet, "<c r=\"B2\"><v>77</v></c>",
        "replace_cells Insert existing-only should replace B2 number");
    check_contains(data_sheet, "<c r=\"C3\"><f>A1+B1</f></c>",
        "replace_cells Insert existing-only should replace C3 formula");
    check_not_contains(data_sheet, "old-a1",
        "replace_cells Insert existing-only should remove old A1 payload");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "replace_cells Insert existing-only should preserve untouched worksheet bytes");
}

void test_replace_cells_keeps_transformer_for_worksheet_relationships()
{
    const std::filesystem::path source =
        write_patch_cells_source_with_external_hyperlink(
            "workbook-editor-public-patch-cells-hyperlink-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-hyperlink-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data",
        {
            {{1, 1}, fastxlsx::CellValue::text("relationship-safe patch")},
            {{3, 3}, fastxlsx::CellValue::formula("A1+B1")},
        },
        fastxlsx::CellPatchMissingCellPolicy::Insert);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(editor);
    check(!has_note_containing(output_plan.notes, {"indexed source-entry direct-range"}),
        "replace_cells should not use direct-range fast path for sheets with relationships");
    const auto* data_sheet_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml");
    check(data_sheet_plan != nullptr,
        "relationship-bearing replace_cells output plan should include the edited worksheet");
    if (data_sheet_plan != nullptr) {
        check(!data_sheet_plan->indexed_source_entry_direct_range,
            "relationship-bearing replace_cells should leave direct-range telemetry off");
        check(data_sheet_plan->staged_replacement_chunks,
            "relationship-bearing replace_cells should still use staged rewrite output");
        check(!data_sheet_plan->materialized_replacement,
            "relationship-bearing replace_cells should not materialize the rewritten worksheet");
        check(data_sheet_plan->single_pass_worksheet_transform,
            "relationship-bearing replace_cells should expose single-pass telemetry");
        check(data_sheet_plan->single_pass_relationship_scan_input_call_count
                <= data_sheet_plan->single_pass_output_flush_count,
            "relationship-bearing replace_cells scanner batches should fit output flushes");
        check(data_sheet_plan->single_pass_relationship_scan_input_bytes > 0
                && data_sheet_plan->single_pass_relationship_scan_input_bytes
                    < data_sheet_plan->single_pass_staged_output_bytes,
            "relationship-bearing replace_cells should scan metadata instead of cell XML");
        check(data_sheet_plan->single_pass_relationship_scan_slow_path_tag_count >= 2,
            "relationship-bearing replace_cells should inspect root namespace and hyperlink tags");
    }

    editor.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>relationship-safe patch</t></is></c>",
        "relationship-bearing replace_cells should patch A1 text");
    check_contains(data_sheet, "<c r=\"C3\"><f>A1+B1</f></c>",
        "relationship-bearing replace_cells should patch C3 formula");
    check_contains(data_sheet, "<hyperlink ref=\"C1\" r:id=\"rId1\"/>",
        "relationship-bearing replace_cells should preserve worksheet hyperlink XML");
    check(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "relationship-bearing replace_cells should preserve worksheet relationships bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "relationship-bearing replace_cells should preserve untouched worksheet bytes");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_replace_cells_on_compressed_source_uses_bounded_direct_range_staging()
{
    const std::filesystem::path source =
        write_patch_cells_default_backend_source(
            "workbook-editor-public-patch-cells-compressed-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-compressed-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_cells("Data", {
        {{1, 1}, fastxlsx::CellValue::text("compressed direct-range patch")},
        {{3, 3}, fastxlsx::CellValue::formula("A1+B1")},
    });

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(editor);
    check(has_note_containing(output_plan.notes,
              {"indexed decompressed-source-entry direct-range", "matched 2 replacement targets"}),
        "compressed-source replace_cells should expose decompressed direct-range telemetry");
    check(has_note_containing(output_plan.notes,
              {"inflates the compressed worksheet once", "memory remains bounded"}),
        "compressed-source replace_cells should document its bounded staging strategy");
    const auto* data_sheet_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml");
    check(data_sheet_plan != nullptr,
        "compressed-source replace_cells output plan should include the edited worksheet");
    if (data_sheet_plan != nullptr) {
        check(data_sheet_plan->indexed_source_entry_direct_range,
            "compressed-source replace_cells should publish direct-range telemetry");
        check(data_sheet_plan->staged_replacement_file_range_chunk_count > 0,
            "compressed-source replace_cells should preserve untouched XML as temporary-file ranges");
        check(data_sheet_plan->staged_replacement_memory_chunk_count > 0,
            "compressed-source replace_cells should stage replacement payloads in bounded memory chunks");
        check(data_sheet_plan->indexed_source_entry_scanned_source_cell_count == 9,
            "compressed-source replace_cells should report its scanned source cells");
        check(data_sheet_plan->indexed_source_entry_matched_replacement_count == 2,
            "compressed-source replace_cells should report every matched target");
    }

    editor.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>compressed direct-range patch</t></is></c>",
        "compressed-source replace_cells should still patch text cells correctly");
    check_contains(data_sheet, "<c r=\"C3\"><f>A1+B1</f></c>",
        "compressed-source replace_cells should still patch formula cells correctly");
    check_not_contains(data_sheet, "old-a1",
        "compressed-source replace_cells should remove replaced source payload");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "compressed-source replace_cells should preserve untouched worksheet bytes");
}
#endif

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

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(editor);
    check(!has_note_containing(output_plan.notes, {"indexed source-entry direct-range"}),
        "replace_cells Insert policy with missing targets should not claim strict direct-range");
    check(has_note_containing(output_plan.notes,
              {"one source-order PackageReader ZIP-entry scan"}),
        "replace_cells Insert policy should expose the single-pass source transform");
    const auto* data_sheet_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml");
    check(data_sheet_plan != nullptr,
        "replace_cells Insert policy output plan should include the edited worksheet");
    if (data_sheet_plan != nullptr) {
        check(!data_sheet_plan->indexed_source_entry_direct_range,
            "replace_cells Insert policy with missing targets should not expose direct-range telemetry");
        check(data_sheet_plan->single_pass_worksheet_transform,
            "replace_cells Insert policy should expose single-pass telemetry");
        check(data_sheet_plan->single_pass_scanned_source_cell_count == 9,
            "replace_cells Insert policy single-pass source count mismatch");
        check(data_sheet_plan->single_pass_matched_replacement_count == 1,
            "replace_cells Insert policy single-pass matched count mismatch");
        check(data_sheet_plan->single_pass_inserted_cell_count == 2,
            "replace_cells Insert policy single-pass inserted count mismatch");
        check(data_sheet_plan->single_pass_staged_output_bytes > 0,
            "replace_cells Insert policy should expose staged output bytes");
        check(data_sheet_plan->single_pass_transform_us > 0,
            "replace_cells Insert policy should expose microsecond transform telemetry");
        check(data_sheet_plan->single_pass_transform_pass_through_batch_count > 0
                && data_sheet_plan->single_pass_transform_pass_through_batch_count
                    < data_sheet_plan->single_pass_transform_pass_through_batched_cell_count,
            "replace_cells Insert policy should batch consecutive pass-through cells");
        check(data_sheet_plan->single_pass_transform_pass_through_batched_cell_count
                    <= data_sheet_plan->single_pass_scanned_source_cell_count
                && data_sheet_plan->single_pass_transform_pass_through_batched_bytes > 0,
            "replace_cells Insert policy should expose bounded pass-through traffic");
        check(data_sheet_plan->single_pass_transform_pass_through_batch_peak_cell_count > 1,
            "replace_cells Insert policy should expose a multi-cell pass-through batch");
        check(data_sheet_plan->single_pass_source_canonical_inline_string_fast_path_count > 0
                && data_sheet_plan->single_pass_source_canonical_inline_string_fast_path_count
                    <= data_sheet_plan->single_pass_source_simple_inline_string_fast_path_count,
            "replace_cells Insert policy should expose canonical inline-string fast-path traffic");
        check(data_sheet_plan->single_pass_source_canonical_inline_string_fast_path_bytes
                    > data_sheet_plan->single_pass_source_canonical_inline_string_fast_path_count
                && data_sheet_plan->single_pass_source_canonical_inline_string_fast_path_bytes
                    <= data_sheet_plan->single_pass_source_simple_inline_string_fast_path_bytes,
            "replace_cells Insert policy canonical bytes should fit aggregate inline traffic");
        check(data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_count > 0
                && data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_count
                    <= data_sheet_plan->single_pass_source_complete_cell_coalesced_count,
            "replace_cells Insert policy should expose canonical complete-cell traffic");
        check(data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_bytes
                    > data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_count
                && data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_bytes
                    <= data_sheet_plan->single_pass_source_complete_cell_coalesced_bytes,
            "replace_cells Insert policy canonical cell bytes should fit coalesced traffic");
        check(data_sheet_plan->single_pass_source_canonical_complete_cell_inline_string_count > 0
                && data_sheet_plan->single_pass_source_canonical_complete_cell_inline_string_count
                    <= data_sheet_plan->single_pass_source_canonical_complete_cell_fast_path_count,
            "replace_cells Insert policy should expose typed canonical inline cells");
        check(data_sheet_plan->single_pass_output_append_call_count
                > data_sheet_plan->single_pass_output_flush_count,
            "replace_cells Insert policy should coalesce output event fragments");
        check(data_sheet_plan->single_pass_output_flush_count > 0,
            "replace_cells Insert policy should flush bounded output batches");
        check(data_sheet_plan->single_pass_output_peak_buffer_bytes > 0
                && data_sheet_plan->single_pass_output_peak_buffer_bytes <= 256U * 1024U,
            "replace_cells Insert policy output buffer should stay within 256 KiB");
        check(data_sheet_plan->single_pass_relationship_scan_input_call_count
                <= data_sheet_plan->single_pass_output_flush_count,
            "replace_cells Insert policy scanner batches should fit output flushes");
        check(data_sheet_plan->single_pass_relationship_scan_input_bytes > 0
                && data_sheet_plan->single_pass_relationship_scan_input_bytes
                    < data_sheet_plan->single_pass_staged_output_bytes,
            "replace_cells Insert policy should scan metadata instead of cell XML");
        check(data_sheet_plan->single_pass_relationship_scan_boundary_carry_count
                <= data_sheet_plan->single_pass_relationship_scan_input_call_count,
            "replace_cells Insert policy scanner carries should fit input calls");
        check(data_sheet_plan->single_pass_relationship_scan_slow_path_tag_count
                < data_sheet_plan->single_pass_scanned_source_cell_count,
            "replace_cells Insert policy should limit relationship attribute slow paths");
        check(data_sheet_plan->single_pass_relationship_scan_us
                    + data_sheet_plan->single_pass_temporary_write_us
                    + data_sheet_plan->single_pass_crc32_us
                <= data_sheet_plan->single_pass_transform_us,
            "replace_cells Insert policy sink timings should fit within transform time");
        check(data_sheet_plan->single_pass_fused_crc32,
            "replace_cells Insert policy should fuse CRC32 into transform output");
        check(data_sheet_plan->single_pass_crc32_segment_count >= 2,
            "replace_cells Insert policy should expose fused CRC32 file segments");
        check(data_sheet_plan->staged_replacement_file_range_chunk_count == 2,
            "replace_cells Insert policy should stage sequential temporary-file ranges");
        check(data_sheet_plan->staged_replacement_memory_chunk_count == 1,
            "replace_cells Insert policy should stage one bounded dimension chunk");
    }

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
        const std::size_t targeted_xml_bytes =
            editor.estimated_pending_targeted_cell_replacement_xml_bytes();
        check(editor.pending_change_count() == 1,
            "replace_cells should queue one public edit before materialization guards");
        check(editor.pending_targeted_cell_replacement_count() == 1,
            "replace_cells should expose targeted diagnostics before materialization guards");
        check(editor.pending_targeted_cell_replacement_worksheet_names()
                == std::vector<std::string>{"Data"},
            "replace_cells should expose Data targeted diagnostics before materialization guards");
        check(targeted_xml_bytes > 0,
            "replace_cells should expose targeted XML bytes before materialization guards");
        check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
            "worksheet() should reject a sheet after replace_cells");
        check(!editor.last_edit_error().has_value(),
            "worksheet() after replace_cells should not update last_edit_error");
        check(threw_fastxlsx_error([&] { (void)editor.try_worksheet("Data"); }),
            "try_worksheet() should reject a sheet after replace_cells");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet() after replace_cells should not update last_edit_error");
        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("whole")}}); }),
            "replace_sheet_data should reject a sheet after replace_cells");
        check(editor.pending_targeted_cell_replacement_count() == 1,
            "mode guard failures should preserve targeted cell diagnostics");
        check(editor.pending_targeted_cell_replacement_worksheet_names()
                == std::vector<std::string>{"Data"},
            "mode guard failures should preserve targeted cell worksheet names");
        check(editor.estimated_pending_targeted_cell_replacement_xml_bytes()
                == targeted_xml_bytes,
            "mode guard failures should preserve targeted XML byte diagnostics");
        check(editor.pending_change_count() == 1,
            "mode guard failures should preserve targeted public edit count");
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

void test_replace_cells_after_full_calculation_preserves_materialization_guard()
{
    const std::filesystem::path source =
        write_patch_cells_source("workbook-editor-public-patch-cells-full-calc-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("workbook-editor-public-patch-cells-full-calc-guard-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before replace_cells should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before replace_cells should count one metadata edit");

    editor.replace_cells("Data",
        {{{1, 1}, fastxlsx::CellValue::text("full-calc targeted patch")}});
    const std::size_t targeted_xml_bytes =
        editor.estimated_pending_targeted_cell_replacement_xml_bytes();
    check(!editor.last_edit_error().has_value(),
        "replace_cells after full-calc request should keep diagnostics clear");
    check(editor.pending_change_count() == 2,
        "replace_cells after full-calc request should count metadata plus targeted patch");
    check(editor.pending_targeted_cell_replacement_count() == 1,
        "replace_cells after full-calc request should expose targeted diagnostics");
    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == std::vector<std::string>{"Data"},
        "replace_cells after full-calc request should expose Data targeted diagnostics");
    check(targeted_xml_bytes > 0,
        "replace_cells after full-calc request should expose targeted XML bytes");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "worksheet() should reject full-calc queued replace_cells");
    check(!editor.last_edit_error().has_value(),
        "worksheet() full-calc replace_cells guard should not update last_edit_error");
    check(threw_fastxlsx_error([&] { (void)editor.try_worksheet("Data"); }),
        "try_worksheet() should reject full-calc queued replace_cells");
    check(!editor.last_edit_error().has_value(),
        "try_worksheet() full-calc replace_cells guard should not update last_edit_error");
    check(editor.pending_change_count() == 2,
        "full-calc replace_cells guard should preserve public edit count");
    check(editor.pending_targeted_cell_replacement_count() == 1,
        "full-calc replace_cells guard should preserve targeted count");
    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == std::vector<std::string>{"Data"},
        "full-calc replace_cells guard should preserve targeted worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes()
            == targeted_xml_bytes,
        "full-calc replace_cells guard should preserve targeted XML byte diagnostics");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(data_sheet,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>full-calc targeted patch</t></is></c>",
        "full-calc queued replace_cells save should persist the targeted patch");
    check_not_contains(data_sheet, "old-a1",
        "full-calc queued replace_cells save should remove the old target payload");
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc queued replace_cells save should persist workbook calc metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc queued replace_cells save should not invent calcChain.xml");
}

void test_replace_cells_after_renamed_full_calculation_materialization_rejects_targeted_patch()
{
    {
        const std::filesystem::path source =
            write_patch_cells_source("workbook-editor-public-patch-cells-renamed-full-calc-dirty-guard-source.xlsx");
        const std::filesystem::path output =
            artifact("workbook-editor-public-patch-cells-renamed-full-calc-dirty-guard-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "RenamedTargetedGuardData");
        editor.request_full_calculation();
        fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedTargetedGuardData");
        sheet.set_cell(1, 1,
            fastxlsx::CellValue::text("renamed-full-calc-targeted-dirty"));

        const std::size_t cell_count_before = sheet.cell_count();
        const std::size_t memory_before = sheet.estimated_memory_usage();
        check(editor.pending_change_count() == 2,
            "renamed full-calc targeted dirty guard setup should queue rename plus metadata");
        check(editor.pending_materialized_worksheet_names()
                == std::vector<std::string>{"RenamedTargetedGuardData"},
            "renamed full-calc targeted dirty guard setup should expose the planned dirty name");

        check(threw_fastxlsx_error([&] {
            editor.replace_cells("RenamedTargetedGuardData",
                {{{1, 2}, fastxlsx::CellValue::text("rejected targeted patch")}});
        }), "replace_cells should reject renamed full-calc dirty materialized worksheet");
        check_patch_cells_materialized_guard_error(
            editor, "renamed full-calc dirty targeted rejection");
        check_patch_cells_clean_diagnostics(
            editor, "renamed full-calc dirty targeted rejection");
        check(!editor.has_pending_targeted_cell_replacement("RenamedTargetedGuardData"),
            "renamed full-calc dirty targeted rejection should not report planned targeted patches");
        check(editor.pending_change_count() == 2,
            "renamed full-calc dirty targeted rejection should preserve rename plus metadata count");
        check(editor.pending_materialized_worksheet_names()
                == std::vector<std::string>{"RenamedTargetedGuardData"},
            "renamed full-calc dirty targeted rejection should preserve the planned dirty name");
        check(editor.pending_materialized_cell_count() == cell_count_before,
            "renamed full-calc dirty targeted rejection should preserve dirty cell diagnostics");
        check(editor.estimated_pending_materialized_memory_usage() == memory_before,
            "renamed full-calc dirty targeted rejection should preserve dirty memory diagnostics");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                "renamed full-calc dirty targeted rejection should keep one summary");
            if (summaries.size() == 1) {
                const auto& summary = summaries[0];
                check(summary.source_name == "Data" &&
                        summary.planned_name == "RenamedTargetedGuardData",
                    "renamed full-calc dirty targeted rejection should keep source/planned names");
                check(summary.renamed && summary.materialized_dirty,
                    "renamed full-calc dirty targeted rejection should keep rename and dirty flags");
                check(!summary.targeted_cells_replaced &&
                        summary.targeted_cell_replacement_count == 0,
                    "renamed full-calc dirty targeted rejection should not report targeted state");
            }
        }

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "renamed full-calc dirty targeted rejection save should clean the handle");
        check(editor.pending_change_count() == 3,
            "renamed full-calc dirty targeted rejection save should count rename, metadata, and handoff");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
            "renamed full-calc dirty targeted rejection save should persist calc metadata");
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedTargetedGuardData")",
            "renamed full-calc dirty targeted rejection save should persist planned name");
        check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "renamed full-calc dirty targeted rejection save should not restore source name");
        check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
            "renamed full-calc dirty targeted rejection save should not invent calcChain.xml");
        const std::string& data_sheet = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(data_sheet, "renamed-full-calc-targeted-dirty",
            "renamed full-calc dirty targeted rejection save should persist materialized edit");
        check_not_contains(data_sheet, "rejected targeted patch",
            "renamed full-calc dirty targeted rejection save should not leak rejected payload");
    }

    {
        const std::filesystem::path source =
            write_patch_cells_source("workbook-editor-public-patch-cells-renamed-full-calc-clean-guard-source.xlsx");
        const std::filesystem::path output =
            artifact("workbook-editor-public-patch-cells-renamed-full-calc-clean-guard-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "RenamedTargetedCleanGuardData");
        editor.request_full_calculation();
        fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedTargetedCleanGuardData");
        const fastxlsx::CellValue original = sheet.get_cell(1, 1);
        check(original.kind() == fastxlsx::CellValueKind::Text &&
                original.text_value() == "old-a1",
            "renamed full-calc clean targeted guard setup should read source cells");

        check(threw_fastxlsx_error([&] {
            editor.replace_cells("RenamedTargetedCleanGuardData",
                {{{1, 2}, fastxlsx::CellValue::text("rejected clean targeted patch")}});
        }), "replace_cells should reject renamed full-calc clean materialized worksheet");
        check_patch_cells_materialized_guard_error(
            editor, "renamed full-calc clean targeted rejection");
        check_patch_cells_clean_diagnostics(
            editor, "renamed full-calc clean targeted rejection");
        check(!editor.has_pending_targeted_cell_replacement("RenamedTargetedCleanGuardData"),
            "renamed full-calc clean targeted rejection should not report planned targeted patches");
        check(!sheet.has_pending_changes(),
            "renamed full-calc clean targeted rejection should keep the handle clean");
        check(editor.pending_change_count() == 2,
            "renamed full-calc clean targeted rejection should preserve rename plus metadata count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "renamed full-calc clean targeted rejection should keep dirty names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "renamed full-calc clean targeted rejection should keep dirty cells empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "renamed full-calc clean targeted rejection should keep dirty memory empty");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                "renamed full-calc clean targeted rejection should keep one summary");
            if (summaries.size() == 1) {
                const auto& summary = summaries[0];
                check(summary.source_name == "Data" &&
                        summary.planned_name == "RenamedTargetedCleanGuardData",
                    "renamed full-calc clean targeted rejection should keep source/planned names");
                check(summary.renamed && !summary.materialized_dirty,
                    "renamed full-calc clean targeted rejection should keep a clean rename summary");
                check(!summary.targeted_cells_replaced &&
                        summary.targeted_cell_replacement_count == 0,
                    "renamed full-calc clean targeted rejection should not report targeted state");
            }
        }

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "renamed full-calc clean targeted rejection save should keep the handle clean");
        check(editor.pending_change_count() == 2,
            "renamed full-calc clean targeted rejection save should not add a handoff");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
            "renamed full-calc clean targeted rejection save should persist calc metadata");
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedTargetedCleanGuardData")",
            "renamed full-calc clean targeted rejection save should persist planned name");
        check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "renamed full-calc clean targeted rejection save should not restore source name");
        check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
            "renamed full-calc clean targeted rejection save should not invent calcChain.xml");
        check(output_entries.at("xl/worksheets/sheet1.xml") ==
                source_entries.at("xl/worksheets/sheet1.xml"),
            "renamed full-calc clean targeted rejection save should preserve source worksheet bytes");
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "rejected clean targeted patch",
            "renamed full-calc clean targeted rejection save should not leak rejected payload");
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
        test_replace_cells_insert_policy_uses_direct_range_when_all_targets_exist();
        test_replace_cells_keeps_transformer_for_worksheet_relationships();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_replace_cells_on_compressed_source_uses_bounded_direct_range_staging();
#endif
        test_replace_cells_rejects_missing_target_without_public_state_pollution();
        test_replace_cells_rejects_unknown_missing_cell_policy_without_public_state_pollution();
        test_replace_cells_insert_policy_patches_existing_and_inserts_missing_cells_and_rows();
        test_replace_cells_can_follow_up_on_upserted_planned_cells();
        test_replace_cells_mode_mixing_guards_and_empty_noop();
        test_replace_cells_after_full_calculation_preserves_materialization_guard();
        test_replace_cells_after_renamed_full_calculation_materialization_rejects_targeted_patch();
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
