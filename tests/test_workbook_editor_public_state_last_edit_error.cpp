#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, std::string_view message)
{
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
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

void check_cell_range_equals(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column,
    std::string_view message)
{
    check(range.has_value() && range->first_row == first_row
            && range->first_column == first_column && range->last_row == last_row
            && range->last_column == last_column,
        message);
}

std::filesystem::path artifact(std::string_view filename)
{
    return fastxlsx::test::artifact_path(filename);
}

bool catalog_entries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& left,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].source_name != right[index].source_name
            || left[index].planned_name != right[index].planned_name
            || left[index].renamed != right[index].renamed) {
            return false;
        }
    }
    return true;
}

bool edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& left,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.source_name != rhs.source_name
            || lhs.planned_name != rhs.planned_name
            || lhs.renamed != rhs.renamed
            || lhs.sheet_data_replaced != rhs.sheet_data_replaced
            || lhs.targeted_cells_replaced != rhs.targeted_cells_replaced
            || lhs.replacement_cell_count != rhs.replacement_cell_count
            || lhs.estimated_replacement_memory_usage
                != rhs.estimated_replacement_memory_usage
            || lhs.materialized_dirty != rhs.materialized_dirty
            || lhs.materialized_cell_count != rhs.materialized_cell_count
            || lhs.estimated_materialized_memory_usage
                != rhs.estimated_materialized_memory_usage) {
            return false;
        }
    }
    return true;
}

struct WorkbookEditorPublicCatalogSnapshot {
    std::vector<std::string> source_names;
    std::vector<std::string> planned_names;
    std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog;
};

WorkbookEditorPublicCatalogSnapshot workbook_editor_public_catalog_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.source_worksheet_names(),
        editor.worksheet_names(),
        editor.worksheet_catalog(),
    };
}

void check_workbook_editor_public_catalog_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicCatalogSnapshot& before,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.source_worksheet_names() == before.source_names,
        prefix + " should preserve source worksheet names");
    check(editor.worksheet_names() == before.planned_names,
        prefix + " should preserve planned worksheet names");
    check(catalog_entries_equal(editor.worksheet_catalog(), before.catalog),
        prefix + " should preserve worksheet catalog");
}

struct WorkbookEditorPublicSaveStateSnapshot {
    bool has_pending_changes{};
    std::size_t pending_change_count{};
    std::vector<std::string> materialized_names;
    std::size_t materialized_cell_count{};
    std::size_t materialized_memory{};
    std::size_t replacement_cell_count{};
    std::size_t replacement_memory{};
    std::vector<std::string> replacement_names;
    std::size_t targeted_cell_count{};
    std::vector<std::string> targeted_names;
    std::size_t targeted_xml_bytes{};
    std::optional<std::string> last_edit_error;
    std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries;
};

WorkbookEditorPublicSaveStateSnapshot workbook_editor_public_save_state_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.has_pending_changes(),
        editor.pending_change_count(),
        editor.pending_materialized_worksheet_names(),
        editor.pending_materialized_cell_count(),
        editor.estimated_pending_materialized_memory_usage(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.pending_targeted_cell_replacement_count(),
        editor.pending_targeted_cell_replacement_worksheet_names(),
        editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
        editor.last_edit_error(),
        editor.pending_worksheet_edits(),
    };
}

void check_workbook_editor_public_save_state_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicSaveStateSnapshot& before,
    std::string_view scenario)
{
    const WorkbookEditorPublicSaveStateSnapshot after =
        workbook_editor_public_save_state_snapshot(editor);
    const std::string prefix(scenario);
    check(after.has_pending_changes == before.has_pending_changes
            && after.pending_change_count == before.pending_change_count,
        prefix + " should preserve pending state");
    check(after.materialized_names == before.materialized_names
            && after.materialized_cell_count == before.materialized_cell_count
            && after.materialized_memory == before.materialized_memory,
        prefix + " should preserve materialized diagnostics");
    check(after.replacement_cell_count == before.replacement_cell_count
            && after.replacement_memory == before.replacement_memory
            && after.replacement_names == before.replacement_names,
        prefix + " should preserve replacement diagnostics");
    check(after.targeted_cell_count == before.targeted_cell_count
            && after.targeted_names == before.targeted_names
            && after.targeted_xml_bytes == before.targeted_xml_bytes,
        prefix + " should preserve targeted replacement diagnostics");
    check(after.last_edit_error == before.last_edit_error,
        prefix + " should preserve last_edit_error");
    check(edit_summaries_equal(after.summaries, before.summaries),
        prefix + " should preserve worksheet edit summaries");
}

void check_workbook_editor_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty(),
        prefix + " should expose no replacement diagnostics");
    check(editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !editor.has_pending_targeted_cell_replacement("Data")
            && !editor.has_pending_targeted_cell_replacement("Styled"),
        prefix + " should expose no targeted replacement diagnostics");
}

void check_public_state_single_named_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view worksheet_name,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    const std::string prefix(scenario);
    const std::string expected_name(worksheet_name);
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory_usage = sheet.estimated_memory_usage();

    check(editor.has_pending_changes()
            && editor.pending_change_count() == expected_pending_change_count,
        prefix + " should expose dirty materialized public state");
    check(editor.last_edit_error() == expected_last_edit_error,
        prefix + " should expose expected last_edit_error");
    check_workbook_editor_no_replacement_diagnostics(editor, scenario);
    check(editor.pending_materialized_worksheet_names()
            == std::vector<std::string> {expected_name}
            && editor.pending_materialized_cell_count() == expected_cell_count
            && editor.estimated_pending_materialized_memory_usage()
                == expected_memory_usage,
        prefix + " should expose dirty materialized diagnostics");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == expected_name
                && summary.planned_name == expected_name && !summary.renamed,
            prefix + " summary should identify the worksheet");
        check(!summary.sheet_data_replaced && !summary.targeted_cells_replaced
                && summary.replacement_cell_count == 0
                && summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should expose no replacement state");
        check(summary.materialized_dirty
                && summary.materialized_cell_count == expected_cell_count
                && summary.estimated_materialized_memory_usage
                    == expected_memory_usage,
            prefix + " summary should match dirty materialized state");
    }
}

void check_reopened_clean_sheet_output(
    const std::filesystem::path& output,
    std::string_view sheet_name,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet(sheet_name);
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value()
            && !reopened_editor.has_pending_changes()
            && !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize cleanly");
    check(reopened_editor.pending_change_count() == 0
            && reopened_editor.pending_materialized_cell_count() == 0
            && reopened_editor.estimated_pending_materialized_memory_usage() == 0
            && reopened_editor.pending_materialized_worksheet_names().empty()
            && reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should expose no materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " reopened output");

    inspect(reopened_sheet);

    check(!reopened_editor.last_edit_error().has_value()
            && !reopened_editor.has_pending_changes()
            && !reopened_sheet.has_pending_changes()
            && reopened_editor.pending_change_count() == 0
            && reopened_editor.pending_materialized_cell_count() == 0
            && reopened_editor.estimated_pending_materialized_memory_usage() == 0
            && reopened_editor.pending_materialized_worksheet_names().empty()
            && reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should remain clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor, prefix + " reopened readback");
}

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


void check_public_state_single_data_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", expected_pending_change_count, scenario,
        expected_last_edit_error);
}


void check_workbook_editor_public_no_pending_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should keep public pending state empty");
}

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output, std::string_view scenario)
{
    check_reopened_clean_sheet_output(
        output, "Data", scenario,
        [](fastxlsx::WorksheetEditor& sheet) {
            check(sheet.cell_count() == 3,
                "default Data output should keep three source cells");
            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 2,
                "default Data output should keep source bounds");
            const auto cells = sheet.sparse_cells();
            check(cells.size() == 3
                    && cells[0].reference.row == 1
                    && cells[0].reference.column == 1
                    && cells[0].value.kind() == fastxlsx::CellValueKind::Text
                    && cells[0].value.text_value() == "placeholder-a1"
                    && cells[1].reference.row == 1
                    && cells[1].reference.column == 2
                    && cells[1].value.kind() == fastxlsx::CellValueKind::Number
                    && cells[1].value.number_value() == 1.0
                    && cells[2].reference.row == 2
                    && cells[2].reference.column == 1
                    && cells[2].value.kind() == fastxlsx::CellValueKind::Text
                    && cells[2].value.text_value() == "placeholder-a2",
                "default Data output should preserve source sparse values");
        });
}

bool workbook_editor_edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.source_name != right.source_name
            || left.planned_name != right.planned_name
            || left.renamed != right.renamed
            || left.sheet_data_replaced != right.sheet_data_replaced
            || left.replacement_cell_count != right.replacement_cell_count
            || left.estimated_replacement_memory_usage
                != right.estimated_replacement_memory_usage
            || left.materialized_dirty != right.materialized_dirty
            || left.materialized_cell_count != right.materialized_cell_count
            || left.estimated_materialized_memory_usage
                != right.estimated_materialized_memory_usage) {
            return false;
        }
    }
    return true;
}

void check_public_inspection_preserves_last_edit_error(
    fastxlsx::WorkbookEditor& editor, const std::optional<std::string>& expected)
{
    const WorkbookEditorPublicCatalogSnapshot catalog_before =
        workbook_editor_public_catalog_snapshot(editor);
    auto check_inspection_state = [&](std::string_view api_name) {
        const std::string prefix(api_name);
        check(editor.last_edit_error() == expected,
            prefix + " should not update last_edit_error");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before, prefix);
    };

    (void)editor.worksheet_names();
    check_inspection_state("worksheet_names");

    (void)editor.has_worksheet("Data");
    check_inspection_state("has_worksheet");

    (void)editor.source_worksheet_names();
    check_inspection_state("source_worksheet_names");

    (void)editor.has_source_worksheet("Data");
    check_inspection_state("has_source_worksheet");

    (void)editor.has_pending_changes();
    check_inspection_state("has_pending_changes");

    (void)editor.pending_change_count();
    check_inspection_state("pending_change_count");

    (void)editor.pending_replacement_cell_count();
    check_inspection_state("pending_replacement_cell_count");

    (void)editor.pending_replacement_worksheet_names();
    check_inspection_state("pending_replacement_worksheet_names");

    (void)editor.pending_targeted_cell_replacement_count();
    check_inspection_state("pending_targeted_cell_replacement_count");

    (void)editor.pending_targeted_cell_replacement_worksheet_names();
    check_inspection_state("pending_targeted_cell_replacement_worksheet_names");

    (void)editor.pending_materialized_worksheet_names();
    check_inspection_state("pending_materialized_worksheet_names");

    (void)editor.pending_materialized_cell_count();
    check_inspection_state("pending_materialized_cell_count");

    (void)editor.estimated_pending_materialized_memory_usage();
    check_inspection_state("estimated_pending_materialized_memory_usage");

    (void)editor.has_pending_replacement("Data");
    check_inspection_state("has_pending_replacement");

    (void)editor.estimated_pending_replacement_memory_usage();
    check_inspection_state("estimated_pending_replacement_memory_usage");

    (void)editor.has_pending_targeted_cell_replacement("Data");
    check_inspection_state("has_pending_targeted_cell_replacement");

    (void)editor.estimated_pending_targeted_cell_replacement_xml_bytes();
    check_inspection_state("estimated_pending_targeted_cell_replacement_xml_bytes");

    (void)editor.pending_worksheet_edits();
    check_inspection_state("pending_worksheet_edits");

    (void)editor.worksheet_catalog();
    check_inspection_state("worksheet_catalog");

    (void)editor.formula_reference_audits();
    check_inspection_state("formula_reference_audits");

    (void)editor.source_formula_reference_audits();
    check_inspection_state("source_formula_reference_audits");

    (void)editor.defined_name_formula_reference_audits();
    check_inspection_state("defined_name_formula_reference_audits");
}

void check_saved_default_data_overwrite_snapshots(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& handle,
    std::size_t expected_pending_count,
    std::string_view scenario,
    std::string_view expected_a1_text)
{
    const std::string prefix(scenario);
    const std::string expected_a1(expected_a1_text);

    check(handle.cell_count() == 3,
        prefix + " should keep the represented sparse count");
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = handle.sparse_cells();
    check(cells.size() == 3,
        prefix + " should expose the three represented records");
    if (cells.size() == 3) {
        check(cells[0].reference.row == 1 &&
                cells[0].reference.column == 1 &&
                cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[0].value.text_value() == expected_a1,
            prefix + " should keep overwritten A1 first");
        check(cells[1].reference.row == 1 &&
                cells[1].reference.column == 2 &&
                cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                cells[1].value.number_value() == 1.0,
            prefix + " should keep source-backed B1 second");
        check(cells[2].reference.row == 2 &&
                cells[2].reference.column == 1 &&
                cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[2].value.text_value() == "placeholder-a2",
            prefix + " should keep source-backed A2 last");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        handle.sparse_cells("A1:D4");
    check(range_cells.size() == 3,
        prefix + " range sparse_cells should expose the three represented records");
    if (range_cells.size() == 3) {
        check(range_cells[0].reference.row == 1 &&
                range_cells[0].reference.column == 1 &&
                range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[0].value.text_value() == expected_a1,
            prefix + " range sparse_cells should keep overwritten A1 first");
        check(range_cells[1].reference.row == 1 &&
                range_cells[1].reference.column == 2 &&
                range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                range_cells[1].value.number_value() == 1.0,
            prefix + " range sparse_cells should keep source-backed B1 second");
        check(range_cells[2].reference.row == 2 &&
                range_cells[2].reference.column == 1 &&
                range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[2].value.text_value() == "placeholder-a2",
            prefix + " range sparse_cells should keep source-backed A2 last");
    }
    const std::array<fastxlsx::WorksheetCellReference, 6> requested_refs {
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {3, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        handle.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        prefix + " requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
    if (requested_cells.size() == 4) {
        check(requested_cells[0].reference.row == 2 &&
                requested_cells[0].reference.column == 1 &&
                requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[0].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should keep A2 first");
        check(requested_cells[1].reference.row == 1 &&
                requested_cells[1].reference.column == 2 &&
                requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                requested_cells[1].value.number_value() == 1.0,
            prefix + " requested sparse_cells should keep B1 after skipped D4");
        check(requested_cells[2].reference.row == 1 &&
                requested_cells[2].reference.column == 1 &&
                requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[2].value.text_value() == expected_a1,
            prefix + " requested sparse_cells should keep overwritten A1 in requested order");
        check(requested_cells[3].reference.row == 2 &&
                requested_cells[3].reference.column == 1 &&
                requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[3].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should preserve duplicate A2");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = handle.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_one[0].value.text_value() == expected_a1 &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        prefix + " should keep row-one overwritten text and source number");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two = handle.row_cells(2);
    check(row_two.size() == 1 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "placeholder-a2",
        prefix + " should keep row-two source text");
    check(handle.row_cells(3).empty(),
        prefix + " should keep the gap row empty");
    check(handle.row_cells(4).empty(),
        prefix + " should keep rejected D4 row empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        handle.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[0].value.text_value() == expected_a1 &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should keep column-one overwritten and source cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        handle.column_cells(2);
    check(column_two.size() == 1 &&
            column_two[0].reference.row == 1 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            column_two[0].value.number_value() == 1.0,
        prefix + " should keep column-two source number");
    check(handle.column_cells(3).empty(),
        prefix + " should keep the gap column empty");
    check(handle.column_cells(4).empty(),
        prefix + " should keep rejected D4 absent");

    check(!handle.try_cell("D4").has_value(),
        prefix + " should keep rejected D4 absent");
    check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
        prefix + " should keep source bounds");
    check(!handle.has_pending_changes(),
        prefix + " should keep the handle clean");
    check(editor.pending_change_count() == expected_pending_count,
        prefix + " should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should keep dirty materialized names empty");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should keep dirty materialized cells empty");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should keep dirty materialized memory empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep dirty summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should keep replacement diagnostics empty");
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
}

void check_reopened_editor_clean_public_state(
    const fastxlsx::WorkbookEditor& reopened_editor,
    std::string_view scenario,
    std::string_view stage)
{
    const std::string prefix =
        std::string(scenario) + " reopened " + std::string(stage);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes(),
        prefix + " should keep editor state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " should not expose dirty worksheet names");
}

void check_cell_value_matches(
    const fastxlsx::CellValue& actual,
    const fastxlsx::CellValue& expected,
    std::string_view message)
{
    const std::string prefix(message);
    check(actual.kind() == expected.kind(),
        prefix + " should keep value kind");
    if (actual.kind() == expected.kind()) {
        switch (expected.kind()) {
        case fastxlsx::CellValueKind::Blank:
            break;
        case fastxlsx::CellValueKind::Number:
            check(actual.number_value() == expected.number_value(),
                prefix + " should keep numeric payload");
            break;
        case fastxlsx::CellValueKind::Text:
        case fastxlsx::CellValueKind::Formula:
        case fastxlsx::CellValueKind::Error:
            check(actual.text_value() == expected.text_value(),
                prefix + " should keep text payload");
            break;
        case fastxlsx::CellValueKind::Boolean:
            check(actual.boolean_value() == expected.boolean_value(),
                prefix + " should keep boolean payload");
            break;
        }
    }

    check(actual.has_style() == expected.has_style(),
        prefix + " should keep style handle presence");
    if (actual.has_style() && expected.has_style()) {
        check(actual.style_id().value() == expected.style_id().value(),
            prefix + " should keep style handle value");
    }
}

void check_reopened_invalid_snapshot_read_failures_are_clean(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    const std::size_t baseline_cell_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    const std::optional<fastxlsx::CellRange> baseline_used_range =
        sheet.used_range();
    const std::vector<fastxlsx::WorksheetCellSnapshot> baseline_sparse =
        sheet.sparse_cells();
    const auto check_used_range_matches =
        [](const std::optional<fastxlsx::CellRange>& actual,
            const std::optional<fastxlsx::CellRange>& expected,
            std::string_view message) {
            const std::string range_prefix(message);
            check(actual.has_value() == expected.has_value(),
                range_prefix + " should preserve used range presence");
            if (actual.has_value() && expected.has_value()) {
                check(actual->first_row == expected->first_row &&
                        actual->first_column == expected->first_column &&
                        actual->last_row == expected->last_row &&
                        actual->last_column == expected->last_column,
                    range_prefix + " should preserve used range bounds");
            }
        };
    const auto check_snapshot_sequence_matches =
        [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& actual,
            const std::vector<fastxlsx::WorksheetCellSnapshot>& expected,
            std::string_view message) {
            const std::string sequence_prefix(message);
            check(actual.size() == expected.size(),
                sequence_prefix + " should preserve snapshot size");
            if (actual.size() == expected.size()) {
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    check(actual[i].reference.row == expected[i].reference.row &&
                            actual[i].reference.column ==
                                expected[i].reference.column,
                        sequence_prefix + " should preserve snapshot reference");
                    check_cell_value_matches(
                        actual[i].value,
                        expected[i].value,
                        sequence_prefix + " should preserve snapshot value");
                }
            }
        };
    std::vector<fastxlsx::WorksheetCellSnapshot> baseline_scalar_probes;
    if (!baseline_sparse.empty()) {
        baseline_scalar_probes.push_back(baseline_sparse.front());
        if (baseline_sparse.front().reference.row !=
                baseline_sparse.back().reference.row ||
            baseline_sparse.front().reference.column !=
                baseline_sparse.back().reference.column) {
            baseline_scalar_probes.push_back(baseline_sparse.back());
        }
    }
    struct IndexedSnapshotProbe {
        std::uint32_t index = 0;
        std::vector<fastxlsx::WorksheetCellSnapshot> cells;
    };
    std::vector<std::uint32_t> baseline_row_indexes;
    std::vector<std::uint32_t> baseline_column_indexes;
    const auto add_unique_index =
        [](std::vector<std::uint32_t>& indexes, std::uint32_t index) {
            for (const std::uint32_t existing : indexes) {
                if (existing == index) {
                    return;
                }
            }
            indexes.push_back(index);
        };
    for (const fastxlsx::WorksheetCellSnapshot& probe : baseline_scalar_probes) {
        add_unique_index(baseline_row_indexes, probe.reference.row);
        add_unique_index(baseline_column_indexes, probe.reference.column);
    }
    std::vector<IndexedSnapshotProbe> baseline_row_probes;
    for (const std::uint32_t row : baseline_row_indexes) {
        baseline_row_probes.push_back(
            IndexedSnapshotProbe {row, sheet.row_cells(row)});
    }
    std::vector<IndexedSnapshotProbe> baseline_column_probes;
    for (const std::uint32_t column : baseline_column_indexes) {
        baseline_column_probes.push_back(
            IndexedSnapshotProbe {column, sheet.column_cells(column)});
    }
    for (const fastxlsx::WorksheetCellSnapshot& probe : baseline_scalar_probes) {
        check(sheet.contains_cell(probe.reference.row, probe.reference.column),
            prefix + " baseline contains_cell should find represented scalar");
        const std::optional<fastxlsx::CellValue> scalar =
            sheet.try_cell(probe.reference.row, probe.reference.column);
        check(scalar.has_value(),
            prefix + " baseline try_cell should read represented scalar");
        if (scalar.has_value()) {
            check_cell_value_matches(
                *scalar, probe.value, prefix + " baseline try_cell payload");
        }
        const fastxlsx::CellValue required_scalar =
            sheet.get_cell(probe.reference.row, probe.reference.column);
        check_cell_value_matches(
            required_scalar, probe.value, prefix + " baseline get_cell payload");
    }
    const std::array<fastxlsx::WorksheetCellReference, 5> missing_probe_candidates {
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {2, 2},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
    };
    std::optional<fastxlsx::WorksheetCellReference> baseline_missing_probe;
    for (const fastxlsx::WorksheetCellReference& candidate :
        missing_probe_candidates) {
        if (!sheet.contains_cell(candidate.row, candidate.column)) {
            baseline_missing_probe = candidate;
            break;
        }
    }
    if (baseline_missing_probe.has_value()) {
        check(!sheet.try_cell(
                  baseline_missing_probe->row,
                  baseline_missing_probe->column)
                  .has_value(),
            prefix + " baseline try_cell should keep missing scalar empty");
    }
    std::vector<fastxlsx::WorksheetCellReference> baseline_requested_cells;
    for (const fastxlsx::WorksheetCellSnapshot& probe : baseline_scalar_probes) {
        baseline_requested_cells.push_back(probe.reference);
    }
    if (baseline_missing_probe.has_value()) {
        baseline_requested_cells.push_back(*baseline_missing_probe);
    }
    if (!baseline_scalar_probes.empty()) {
        baseline_requested_cells.push_back(baseline_scalar_probes.front().reference);
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot>
        baseline_used_range_sparse =
            baseline_used_range.has_value()
                ? sheet.sparse_cells(*baseline_used_range)
                : std::vector<fastxlsx::WorksheetCellSnapshot> {};
    std::vector<fastxlsx::WorksheetCellSnapshot> baseline_requested_sparse;
    if (!baseline_requested_cells.empty()) {
        baseline_requested_sparse =
            sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
                baseline_requested_cells.data(), baseline_requested_cells.size()));
    }

    const std::array<fastxlsx::WorksheetCellReference, 4>
        invalid_scalar_coordinates {
            fastxlsx::WorksheetCellReference {0, 1},
            fastxlsx::WorksheetCellReference {1, 0},
            fastxlsx::WorksheetCellReference {1048577, 1},
            fastxlsx::WorksheetCellReference {1, 16385},
        };
    for (const fastxlsx::WorksheetCellReference& coordinate :
        invalid_scalar_coordinates) {
        check(threw_fastxlsx_error([&] {
            (void)sheet.contains_cell(coordinate.row, coordinate.column);
        }), prefix + " invalid coordinate contains_cell should throw");
        check(threw_fastxlsx_error([&] {
            (void)sheet.try_cell(coordinate.row, coordinate.column);
        }), prefix + " invalid coordinate try_cell should throw");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell(coordinate.row, coordinate.column);
        }), prefix + " invalid coordinate get_cell should throw");
    }
    const std::array<std::string_view, 9> invalid_scalar_a1_references {
        "",
        "a1",
        "XFE1",
        "A1048577",
        "A0",
        "A01",
        "A1:B2",
        "$A$1",
        "Data!A1",
    };
    for (const std::string_view reference : invalid_scalar_a1_references) {
        check(threw_fastxlsx_error([&] {
            (void)sheet.contains_cell(reference);
        }), prefix + " invalid A1 contains_cell should throw");
        check(threw_fastxlsx_error([&] {
            (void)sheet.try_cell(reference);
        }), prefix + " invalid A1 try_cell should throw");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell(reference);
        }), prefix + " invalid A1 get_cell should throw");
    }
    if (baseline_missing_probe.has_value()) {
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell(
                baseline_missing_probe->row,
                baseline_missing_probe->column);
        }), prefix + " missing get_cell should throw");
    }

    check(!editor.last_edit_error().has_value(),
        prefix + " invalid scalar reads should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        prefix + " invalid scalar reads should keep the sheet clean");
    check(!editor.has_pending_changes(),
        prefix + " invalid scalar reads should keep the editor clean");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_replacement_cell_count() == 0 &&
            editor.estimated_pending_replacement_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        prefix + " invalid scalar reads should not expose dirty diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_replacement_worksheet_names().empty(),
        prefix + " invalid scalar reads should not expose dirty worksheet names");
    check(sheet.cell_count() == baseline_cell_count,
        prefix + " invalid scalar reads should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        prefix + " invalid scalar reads should preserve sparse memory estimate");
    check_used_range_matches(
        sheet.used_range(),
        baseline_used_range,
        prefix + " invalid scalar reads should preserve used range");
    check_snapshot_sequence_matches(
        sheet.sparse_cells(),
        baseline_sparse,
        prefix + " invalid scalar reads should preserve full sparse snapshot");
    if (baseline_used_range.has_value()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(*baseline_used_range),
            baseline_used_range_sparse,
            prefix + " invalid scalar reads should preserve used-range sparse snapshot");
    }
    if (!baseline_requested_cells.empty()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
                baseline_requested_cells.data(), baseline_requested_cells.size())),
            baseline_requested_sparse,
            prefix + " invalid scalar reads should preserve requested sparse snapshot");
    }
    for (const fastxlsx::WorksheetCellSnapshot& probe : baseline_scalar_probes) {
        const fastxlsx::CellValue scalar_after_invalid_scalar_reads =
            sheet.get_cell(probe.reference.row, probe.reference.column);
        check_cell_value_matches(
            scalar_after_invalid_scalar_reads,
            probe.value,
            prefix + " invalid scalar reads should preserve get_cell payload");
    }
    for (const IndexedSnapshotProbe& probe : baseline_row_probes) {
        check_snapshot_sequence_matches(
            sheet.row_cells(probe.index),
            probe.cells,
            prefix + " invalid scalar reads should preserve row_cells snapshot");
    }
    for (const IndexedSnapshotProbe& probe : baseline_column_probes) {
        check_snapshot_sequence_matches(
            sheet.column_cells(probe.index),
            probe.cells,
            prefix + " invalid scalar reads should preserve column_cells snapshot");
    }

    const std::array<fastxlsx::CellRange, 4> invalid_snapshot_ranges {
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {3, 3, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385},
    };
    for (const fastxlsx::CellRange invalid_range : invalid_snapshot_ranges) {
        check(threw_fastxlsx_error([&] {
            (void)sheet.sparse_cells(invalid_range);
        }), prefix + " invalid CellRange sparse_cells should throw");
    }
    const std::array<std::string_view, 9> invalid_a1_snapshot_ranges {
        "",
        "a1:B2",
        "A1:b2",
        "XFE1:XFE2",
        "A1:A1048577",
        "A1:B2:C3",
        "B2:A1",
        "$A$1:$B$2",
        "Data!A1:B2",
    };
    for (const std::string_view invalid_range : invalid_a1_snapshot_ranges) {
        check(threw_fastxlsx_error([&] {
            (void)sheet.sparse_cells(invalid_range);
        }), prefix + " invalid A1 range sparse_cells should throw");
    }
    const std::array<fastxlsx::WorksheetCellReference, 0>
        empty_requested_cells {};
    check(sheet.sparse_cells(empty_requested_cells).empty(),
        prefix + " empty coordinate-batch sparse_cells should stay empty");
    const std::array<fastxlsx::WorksheetCellReference, 3> invalid_requested_cells {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1048577, 1},
        fastxlsx::WorksheetCellReference {1, 16385},
    };
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(invalid_requested_cells);
    }), prefix + " invalid coordinate-batch sparse_cells should throw");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        prefix + " invalid row_cells should throw");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(1048577); }),
        prefix + " overflowing row_cells should throw");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(0); }),
        prefix + " invalid column_cells should throw");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(16385); }),
        prefix + " overflowing column_cells should throw");

    check(!editor.last_edit_error().has_value(),
        prefix + " invalid snapshot reads should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        prefix + " invalid snapshot reads should keep the sheet clean");
    check(!editor.has_pending_changes(),
        prefix + " invalid snapshot reads should keep the editor clean");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_replacement_cell_count() == 0 &&
            editor.estimated_pending_replacement_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        prefix + " invalid snapshot reads should not expose dirty diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_replacement_worksheet_names().empty(),
        prefix + " invalid snapshot reads should not expose dirty worksheet names");

    check(sheet.cell_count() == baseline_cell_count,
        prefix + " invalid snapshot reads should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        prefix + " invalid snapshot reads should preserve sparse memory estimate");
    check_used_range_matches(
        sheet.used_range(),
        baseline_used_range,
        prefix + " invalid snapshot reads should preserve used range");
    const std::vector<fastxlsx::WorksheetCellSnapshot> sparse_after_invalid_reads =
        sheet.sparse_cells();
    check(sparse_after_invalid_reads.size() == baseline_sparse.size(),
        prefix + " invalid snapshot reads should preserve full sparse snapshot size");
    check_snapshot_sequence_matches(
        sparse_after_invalid_reads,
        baseline_sparse,
        prefix + " invalid snapshot reads should preserve full sparse snapshot");
    if (baseline_used_range.has_value()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(*baseline_used_range),
            baseline_used_range_sparse,
            prefix + " invalid snapshot reads should preserve used-range sparse snapshot");
    }
    if (!baseline_requested_cells.empty()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
                baseline_requested_cells.data(), baseline_requested_cells.size())),
            baseline_requested_sparse,
            prefix + " invalid snapshot reads should preserve requested sparse snapshot");
    }
    if (!baseline_sparse.empty() &&
        sparse_after_invalid_reads.size() == baseline_sparse.size()) {
        check(sparse_after_invalid_reads.front().reference.row ==
                    baseline_sparse.front().reference.row &&
                sparse_after_invalid_reads.front().reference.column ==
                    baseline_sparse.front().reference.column &&
                sparse_after_invalid_reads.front().value.kind() ==
                    baseline_sparse.front().value.kind() &&
                sparse_after_invalid_reads.back().reference.row ==
                    baseline_sparse.back().reference.row &&
                sparse_after_invalid_reads.back().reference.column ==
                    baseline_sparse.back().reference.column &&
                sparse_after_invalid_reads.back().value.kind() ==
                    baseline_sparse.back().value.kind(),
            prefix + " invalid snapshot reads should preserve sparse snapshot endpoints");
        check_cell_value_matches(
            sparse_after_invalid_reads.front().value,
            baseline_sparse.front().value,
            prefix + " invalid snapshot reads should preserve first sparse value");
        check_cell_value_matches(
            sparse_after_invalid_reads.back().value,
            baseline_sparse.back().value,
            prefix + " invalid snapshot reads should preserve last sparse value");
    }
    for (const fastxlsx::WorksheetCellSnapshot& probe : baseline_scalar_probes) {
        check(sheet.contains_cell(probe.reference.row, probe.reference.column),
            prefix + " invalid snapshot reads should preserve contains_cell for represented scalar");
        const std::optional<fastxlsx::CellValue> scalar_after_invalid_reads =
            sheet.try_cell(probe.reference.row, probe.reference.column);
        check(scalar_after_invalid_reads.has_value(),
            prefix + " invalid snapshot reads should preserve try_cell presence");
        if (scalar_after_invalid_reads.has_value()) {
            check_cell_value_matches(
                *scalar_after_invalid_reads,
                probe.value,
                prefix + " invalid snapshot reads should preserve try_cell payload");
        }
        const fastxlsx::CellValue required_scalar_after_invalid_reads =
            sheet.get_cell(probe.reference.row, probe.reference.column);
        check_cell_value_matches(
            required_scalar_after_invalid_reads,
            probe.value,
            prefix + " invalid snapshot reads should preserve get_cell payload");
    }
    for (const IndexedSnapshotProbe& probe : baseline_row_probes) {
        check_snapshot_sequence_matches(
            sheet.row_cells(probe.index),
            probe.cells,
            prefix + " invalid snapshot reads should preserve row_cells snapshot");
    }
    for (const IndexedSnapshotProbe& probe : baseline_column_probes) {
        check_snapshot_sequence_matches(
            sheet.column_cells(probe.index),
            probe.cells,
            prefix + " invalid snapshot reads should preserve column_cells snapshot");
    }
    if (baseline_missing_probe.has_value()) {
        check(!sheet.contains_cell(
                  baseline_missing_probe->row,
                  baseline_missing_probe->column),
            prefix + " invalid snapshot reads should preserve missing contains_cell state");
        check(!sheet.try_cell(
                  baseline_missing_probe->row,
                  baseline_missing_probe->column)
                  .has_value(),
            prefix + " invalid snapshot reads should preserve missing try_cell state");
        check(threw_fastxlsx_error([&] {
            (void)sheet.get_cell(
                baseline_missing_probe->row,
                baseline_missing_probe->column);
        }), prefix + " invalid snapshot reads should preserve missing get_cell failure");
    }
    check(!editor.last_edit_error().has_value(),
        prefix + " scalar reads after invalid snapshots should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        prefix + " scalar reads after invalid snapshots should keep the sheet clean");
    check(!editor.has_pending_changes(),
        prefix + " scalar reads after invalid snapshots should keep the editor clean");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_replacement_cell_count() == 0 &&
            editor.estimated_pending_replacement_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        prefix + " scalar reads after invalid snapshots should not expose dirty diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_replacement_worksheet_names().empty(),
        prefix + " scalar reads after invalid snapshots should not expose dirty worksheet names");
    check(sheet.cell_count() == baseline_cell_count,
        prefix + " scalar reads after invalid snapshots should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        prefix + " scalar reads after invalid snapshots should preserve sparse memory estimate");
    check_used_range_matches(
        sheet.used_range(),
        baseline_used_range,
        prefix + " scalar reads after invalid snapshots should preserve used range");
    check_snapshot_sequence_matches(
        sheet.sparse_cells(),
        baseline_sparse,
        prefix + " scalar reads after invalid snapshots should preserve full sparse snapshot");
    if (baseline_used_range.has_value()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(*baseline_used_range),
            baseline_used_range_sparse,
            prefix + " scalar reads after invalid snapshots should preserve used-range sparse snapshot");
    }
    if (!baseline_requested_cells.empty()) {
        check_snapshot_sequence_matches(
            sheet.sparse_cells(std::span<const fastxlsx::WorksheetCellReference>(
                baseline_requested_cells.data(), baseline_requested_cells.size())),
            baseline_requested_sparse,
            prefix + " scalar reads after invalid snapshots should preserve requested sparse snapshot");
    }
    for (const IndexedSnapshotProbe& probe : baseline_row_probes) {
        check_snapshot_sequence_matches(
            sheet.row_cells(probe.index),
            probe.cells,
            prefix + " scalar reads after invalid snapshots should preserve row_cells snapshot");
    }
    for (const IndexedSnapshotProbe& probe : baseline_column_probes) {
        check_snapshot_sequence_matches(
            sheet.column_cells(probe.index),
            probe.cells,
            prefix + " scalar reads after invalid snapshots should preserve column_cells snapshot");
    }
}

void check_reopened_last_error_recovery_output(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options,
    const char* expected_a1_text = "fixed")
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);

    check_reopened_editor_clean_public_state(
        reopened_editor, "last-error recovery", "output");
    check(!reopened_sheet.has_pending_changes(),
        "last-error recovery reopened output should materialize a clean sheet");
    check(reopened_sheet.cell_count() == 3,
        "last-error recovery reopened output should keep the source sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
        "last-error recovery reopened output should keep the source used range");

    const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
    check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
            reopened_a1.text_value() == expected_a1_text,
        "last-error recovery reopened output should read back the successful overwrite");
    const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
    check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_b1.number_value() == 1.0,
        "last-error recovery reopened output should keep source-backed B1");
    const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
    check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
            reopened_a2.text_value() == "placeholder-a2",
        "last-error recovery reopened output should keep source-backed A2");
    check(!reopened_sheet.try_cell("D4").has_value(),
        "last-error recovery reopened output should keep rejected D4 absent");

    const auto check_reopened_recovery_sparse =
        [expected_a1_text](
            const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view message_prefix) {
            check(cells.size() == 3 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == expected_a1_text &&
                    cells[1].reference.row == 1 &&
                    cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0 &&
                    cells[2].reference.row == 2 &&
                    cells[2].reference.column == 1 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[2].value.text_value() == "placeholder-a2",
                std::string(message_prefix) +
                    " should expose row-major recovered state");
        };
    check_reopened_recovery_sparse(
        reopened_sheet.sparse_cells(),
        "last-error recovery reopened full sparse_cells");
    check_reopened_recovery_sparse(
        reopened_sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}),
        "last-error recovery reopened CellRange sparse_cells");
    check_reopened_recovery_sparse(
        reopened_sheet.sparse_cells("A1:B2"),
        "last-error recovery reopened A1 range sparse_cells");
    const std::array<fastxlsx::WorksheetCellReference, 5> reopened_requested_cells {
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_requested_sparse =
        reopened_sheet.sparse_cells(reopened_requested_cells);
    check(reopened_requested_sparse.size() == 4 &&
            reopened_requested_sparse[0].reference.row == 2 &&
            reopened_requested_sparse[0].reference.column == 1 &&
            reopened_requested_sparse[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_requested_sparse[0].value.text_value() == "placeholder-a2" &&
            reopened_requested_sparse[1].reference.row == 1 &&
            reopened_requested_sparse[1].reference.column == 2 &&
            reopened_requested_sparse[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_requested_sparse[1].value.number_value() == 1.0 &&
            reopened_requested_sparse[2].reference.row == 1 &&
            reopened_requested_sparse[2].reference.column == 1 &&
            reopened_requested_sparse[2].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_requested_sparse[2].value.text_value() == expected_a1_text &&
            reopened_requested_sparse[3].reference.row == 1 &&
            reopened_requested_sparse[3].reference.column == 1 &&
            reopened_requested_sparse[3].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_requested_sparse[3].value.text_value() == expected_a1_text,
        "last-error recovery reopened coordinate sparse_cells should skip rejected D4 and preserve requested order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
        reopened_sheet.row_cells(1);
    check(reopened_row_one.size() == 2 &&
            reopened_row_one[0].reference.row == 1 &&
            reopened_row_one[0].reference.column == 1 &&
            reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_one[0].value.text_value() == expected_a1_text &&
            reopened_row_one[1].reference.row == 1 &&
            reopened_row_one[1].reference.column == 2 &&
            reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_row_one[1].value.number_value() == 1.0,
        "last-error recovery reopened row_cells should expose fixed A1 and source B1");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
        reopened_sheet.column_cells(1);
    check(reopened_column_one.size() == 2 &&
            reopened_column_one[0].reference.row == 1 &&
            reopened_column_one[0].reference.column == 1 &&
            reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_column_one[0].value.text_value() == expected_a1_text &&
            reopened_column_one[1].reference.row == 2 &&
            reopened_column_one[1].reference.column == 1 &&
            reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_column_one[1].value.text_value() == "placeholder-a2",
        "last-error recovery reopened column_cells should expose fixed A1 and source A2");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
        reopened_sheet.column_cells(2);
    check(reopened_column_two.size() == 1 &&
            reopened_column_two[0].reference.row == 1 &&
            reopened_column_two[0].reference.column == 2 &&
            reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_column_two[0].value.number_value() == 1.0,
        "last-error recovery reopened column_cells should expose source B1");
    check(reopened_sheet.column_cells(4).empty(),
        "last-error recovery reopened column_cells should keep rejected column empty");
    check_reopened_invalid_snapshot_read_failures_are_clean(
        reopened_editor,
        reopened_sheet,
        "last-error recovery reopened output");
    check_reopened_editor_clean_public_state(
        reopened_editor, "last-error recovery", "readback");
    check(!reopened_sheet.has_pending_changes(),
        "last-error recovery reopened readback should keep sheet state clean");
}

void check_reopened_mixed_last_error_recovery_output(
    const std::filesystem::path& output,
    const char* expected_untouched_a1_text = "mixed-diagnostic-recovered")
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened_editor.worksheet("Data");
    fastxlsx::WorksheetEditor reopened_untouched =
        reopened_editor.worksheet("Untouched");

    check_reopened_editor_clean_public_state(
        reopened_editor, "mixed diagnostic recovery", "output");
    check(!reopened_data.has_pending_changes() &&
            !reopened_untouched.has_pending_changes(),
        "mixed diagnostic recovery reopened output should materialize clean sheets");

    check(reopened_data.cell_count() == 3,
        "mixed diagnostic recovery reopened Data should keep source sparse count");
    check_cell_range_equals(reopened_data.used_range(), 1, 1, 2, 2,
        "mixed diagnostic recovery reopened Data should keep the source used range");
    const fastxlsx::CellValue reopened_data_a1 = reopened_data.get_cell("A1");
    check(reopened_data_a1.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_a1.text_value() == "placeholder-a1",
        "mixed diagnostic recovery reopened Data should keep source-backed A1");
    const fastxlsx::CellValue reopened_data_b1 = reopened_data.get_cell("B1");
    check(reopened_data_b1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_data_b1.number_value() == 1.0,
        "mixed diagnostic recovery reopened Data should keep source-backed B1");
    const fastxlsx::CellValue reopened_data_a2 = reopened_data.get_cell("A2");
    check(reopened_data_a2.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_a2.text_value() == "placeholder-a2",
        "mixed diagnostic recovery reopened Data should keep source-backed A2");

    const auto check_reopened_data_sparse =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view message_prefix) {
            check(cells.size() == 3 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "placeholder-a1" &&
                    cells[1].reference.row == 1 &&
                    cells[1].reference.column == 2 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[1].value.number_value() == 1.0 &&
                    cells[2].reference.row == 2 &&
                    cells[2].reference.column == 1 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[2].value.text_value() == "placeholder-a2",
                std::string(message_prefix) +
                    " should expose source row-major state");
        };
    check_reopened_data_sparse(
        reopened_data.sparse_cells(),
        "mixed diagnostic recovery reopened Data full sparse_cells");
    check_reopened_data_sparse(
        reopened_data.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}),
        "mixed diagnostic recovery reopened Data CellRange sparse_cells");
    check_reopened_data_sparse(
        reopened_data.sparse_cells("A1:B2"),
        "mixed diagnostic recovery reopened Data A1 range sparse_cells");
    const std::array<fastxlsx::WorksheetCellReference, 5> reopened_data_requested_cells {
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 1},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_data_requested_sparse =
        reopened_data.sparse_cells(reopened_data_requested_cells);
    check(reopened_data_requested_sparse.size() == 4 &&
            reopened_data_requested_sparse[0].reference.row == 2 &&
            reopened_data_requested_sparse[0].reference.column == 1 &&
            reopened_data_requested_sparse[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_requested_sparse[0].value.text_value() == "placeholder-a2" &&
            reopened_data_requested_sparse[1].reference.row == 1 &&
            reopened_data_requested_sparse[1].reference.column == 2 &&
            reopened_data_requested_sparse[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_data_requested_sparse[1].value.number_value() == 1.0 &&
            reopened_data_requested_sparse[2].reference.row == 1 &&
            reopened_data_requested_sparse[2].reference.column == 1 &&
            reopened_data_requested_sparse[2].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_requested_sparse[2].value.text_value() == "placeholder-a1" &&
            reopened_data_requested_sparse[3].reference.row == 1 &&
            reopened_data_requested_sparse[3].reference.column == 1 &&
            reopened_data_requested_sparse[3].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_requested_sparse[3].value.text_value() == "placeholder-a1",
        "mixed diagnostic recovery reopened Data coordinate sparse_cells should skip missing D4 and preserve requested order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_data_row_one =
        reopened_data.row_cells(1);
    check(reopened_data_row_one.size() == 2 &&
            reopened_data_row_one[0].reference.row == 1 &&
            reopened_data_row_one[0].reference.column == 1 &&
            reopened_data_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_row_one[0].value.text_value() == "placeholder-a1" &&
            reopened_data_row_one[1].reference.row == 1 &&
            reopened_data_row_one[1].reference.column == 2 &&
            reopened_data_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_data_row_one[1].value.number_value() == 1.0,
        "mixed diagnostic recovery reopened Data row_cells should expose source row one");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_data_row_two =
        reopened_data.row_cells(2);
    check(reopened_data_row_two.size() == 1 &&
            reopened_data_row_two[0].reference.row == 2 &&
            reopened_data_row_two[0].reference.column == 1 &&
            reopened_data_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_row_two[0].value.text_value() == "placeholder-a2",
        "mixed diagnostic recovery reopened Data row_cells should expose source row two");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_data_column_one =
        reopened_data.column_cells(1);
    check(reopened_data_column_one.size() == 2 &&
            reopened_data_column_one[0].reference.row == 1 &&
            reopened_data_column_one[0].reference.column == 1 &&
            reopened_data_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_column_one[0].value.text_value() == "placeholder-a1" &&
            reopened_data_column_one[1].reference.row == 2 &&
            reopened_data_column_one[1].reference.column == 1 &&
            reopened_data_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_data_column_one[1].value.text_value() == "placeholder-a2",
        "mixed diagnostic recovery reopened Data column_cells should expose source column one");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_data_column_two =
        reopened_data.column_cells(2);
    check(reopened_data_column_two.size() == 1 &&
            reopened_data_column_two[0].reference.row == 1 &&
            reopened_data_column_two[0].reference.column == 2 &&
            reopened_data_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_data_column_two[0].value.number_value() == 1.0,
        "mixed diagnostic recovery reopened Data column_cells should expose source column two");

    check(reopened_untouched.cell_count() == 1,
        "mixed diagnostic recovery reopened Untouched should expose replacement count");
    check_cell_range_equals(reopened_untouched.used_range(), 1, 1, 1, 1,
        "mixed diagnostic recovery reopened Untouched should expose replacement range");
    const fastxlsx::CellValue reopened_untouched_a1 =
        reopened_untouched.get_cell("A1");
    check(reopened_untouched_a1.kind() == fastxlsx::CellValueKind::Text &&
            reopened_untouched_a1.text_value() == expected_untouched_a1_text,
        "mixed diagnostic recovery reopened Untouched should read back replacement text");
    check(!reopened_untouched.try_cell("B1").has_value(),
        "mixed diagnostic recovery reopened Untouched should not keep old B1");

    const auto check_reopened_untouched_sparse =
        [expected_untouched_a1_text](
            const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
            std::string_view message_prefix) {
            check(cells.size() == 1 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == expected_untouched_a1_text,
                std::string(message_prefix) +
                    " should expose replacement A1 only");
        };
    check_reopened_untouched_sparse(
        reopened_untouched.sparse_cells(),
        "mixed diagnostic recovery reopened Untouched full sparse_cells");
    check_reopened_untouched_sparse(
        reopened_untouched.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2}),
        "mixed diagnostic recovery reopened Untouched CellRange sparse_cells");
    check_reopened_untouched_sparse(
        reopened_untouched.sparse_cells("A1:B1"),
        "mixed diagnostic recovery reopened Untouched A1 range sparse_cells");
    const std::array<fastxlsx::WorksheetCellReference, 4>
        reopened_untouched_requested_cells {
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {4, 4},
            fastxlsx::WorksheetCellReference {1, 1},
        };
    const std::vector<fastxlsx::WorksheetCellSnapshot>
        reopened_untouched_requested_sparse =
            reopened_untouched.sparse_cells(reopened_untouched_requested_cells);
    check(reopened_untouched_requested_sparse.size() == 2 &&
            reopened_untouched_requested_sparse[0].reference.row == 1 &&
            reopened_untouched_requested_sparse[0].reference.column == 1 &&
            reopened_untouched_requested_sparse[0].value.kind() ==
                fastxlsx::CellValueKind::Text &&
            reopened_untouched_requested_sparse[0].value.text_value() ==
                expected_untouched_a1_text &&
            reopened_untouched_requested_sparse[1].reference.row == 1 &&
            reopened_untouched_requested_sparse[1].reference.column == 1 &&
            reopened_untouched_requested_sparse[1].value.kind() ==
                fastxlsx::CellValueKind::Text &&
            reopened_untouched_requested_sparse[1].value.text_value() ==
                expected_untouched_a1_text,
        "mixed diagnostic recovery reopened Untouched coordinate sparse_cells should skip old B1 and preserve duplicate A1");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_untouched_row_one =
        reopened_untouched.row_cells(1);
    check(reopened_untouched_row_one.size() == 1 &&
            reopened_untouched_row_one[0].reference.row == 1 &&
            reopened_untouched_row_one[0].reference.column == 1 &&
            reopened_untouched_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_untouched_row_one[0].value.text_value() ==
                expected_untouched_a1_text,
        "mixed diagnostic recovery reopened row_cells should expose replacement A1 only");
    const std::vector<fastxlsx::WorksheetCellSnapshot>
        reopened_untouched_column_one = reopened_untouched.column_cells(1);
    check(reopened_untouched_column_one.size() == 1 &&
            reopened_untouched_column_one[0].reference.row == 1 &&
            reopened_untouched_column_one[0].reference.column == 1 &&
            reopened_untouched_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_untouched_column_one[0].value.text_value() ==
                expected_untouched_a1_text,
        "mixed diagnostic recovery reopened column_cells should expose replacement A1 only");
    check_reopened_invalid_snapshot_read_failures_are_clean(
        reopened_editor,
        reopened_data,
        "mixed diagnostic recovery reopened Data output");
    check_reopened_invalid_snapshot_read_failures_are_clean(
        reopened_editor,
        reopened_untouched,
        "mixed diagnostic recovery reopened Untouched output");
    check_reopened_editor_clean_public_state(
        reopened_editor, "mixed diagnostic recovery", "readback");
    check(!reopened_data.has_pending_changes() &&
            !reopened_untouched.has_pending_changes(),
        "mixed diagnostic recovery reopened readback should keep sheet states clean");
}

void test_public_worksheet_editor_last_edit_error_replaces_failed_mutation_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-last-error-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-reacquired-post-noop-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-last-error-replace-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    const std::size_t baseline_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(!sheet.try_cell("D4").has_value(),
        "last-error replacement test precondition should use a missing D4 cell");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-reference-payload"));
    }), "invalid A1 mutation should seed last_edit_error");
    check(editor.last_edit_error().has_value(),
        "invalid A1 mutation should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorksheetEditor cell reference is invalid",
            "invalid A1 diagnostic should be visible");
    }
    const std::optional<std::string> invalid_reference_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, invalid_reference_error);

    bool memory_failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("replacement-memory-diagnostic"));
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "memory guardrail failure should expose CellStore diagnostic");
    }
    check(memory_failed,
        "memory guardrail failure should replace invalid-reference diagnostic");
    check(editor.last_edit_error().has_value(),
        "memory guardrail failure should keep last_edit_error populated");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore memory_budget_bytes guardrail exceeded",
            "latest diagnostic should be the memory-budget failure");
        check_not_contains(*editor.last_edit_error(),
            "WorksheetEditor cell reference is invalid",
            "memory-budget failure should replace the old invalid-reference diagnostic");
    }
    const std::optional<std::string> memory_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, memory_error);

    bool coordinate_failed = false;
    try {
        sheet.erase_cell(1048577, 1);
    } catch (const fastxlsx::FastXlsxError& error) {
        coordinate_failed = true;
        check_contains(error.what(), "WorksheetEditor cell coordinate is invalid",
            "invalid coordinate erase should expose coordinate diagnostic");
    }
    check(coordinate_failed,
        "invalid coordinate erase should replace memory-budget diagnostic");
    check(editor.last_edit_error().has_value(),
        "invalid coordinate erase should keep last_edit_error populated");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorksheetEditor cell coordinate is invalid",
            "latest diagnostic should be the invalid coordinate failure");
        check_not_contains(*editor.last_edit_error(),
            "memory_budget_bytes guardrail exceeded",
            "coordinate failure should replace the old memory-budget diagnostic");
    }

    check(!sheet.has_pending_changes(),
        "replaced failure diagnostics should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "replaced failure diagnostics should not dirty the editor");
    check(editor.pending_materialized_worksheet_names().empty(),
        "replaced failure diagnostics should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "replaced failure diagnostics should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "replaced failure diagnostics should not expose dirty materialized memory");
    check(sheet.cell_count() == baseline_count,
        "replaced failure diagnostics should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "replaced failure diagnostics should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "replaced failure diagnostics should keep the rejected D4 cell absent");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "replaced failure diagnostics should leave the source package unchanged");

    sheet.set_cell("A1", fastxlsx::CellValue::text("fixed"));
    check(!editor.last_edit_error().has_value(),
        "successful in-budget mutation should clear replaced failure diagnostic");
    check(sheet.has_pending_changes(),
        "successful in-budget mutation should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful in-budget mutation should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite after diagnostic replacement should keep sparse count stable");
    const std::size_t recovery_memory = sheet.estimated_memory_usage();
    check(editor.estimated_pending_materialized_memory_usage() == recovery_memory,
        "successful overwrite after diagnostic replacement should expose recovered memory");

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "last-error replacement recovery save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "fixed",
        "successful mutation after diagnostic replacement should persist");
    check_not_contains(worksheet_xml, "invalid-reference-payload",
        "invalid-reference payload should not leak into output");
    check_not_contains(worksheet_xml, "replacement-memory-diagnostic",
        "memory-budget rejected payload should not leak into output");
    check_not_contains(worksheet_xml, R"(r="D4")",
        "memory-budget rejected D4 should not leak into output");
    check_reopened_last_error_recovery_output(output, options);

    fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Data", options);
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "last-error recovery",
        "strict-options reacquire");
    check(!reacquired_sheet.has_pending_changes(),
        "last-error recovery strict-options reacquire should keep the sheet clean");
    check(reacquired_sheet.cell_count() == baseline_count,
        "last-error recovery strict-options reacquire should keep the sparse count stable");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "last-error recovery strict-options reacquire should stay within the original budget");
    const fastxlsx::CellValue reacquired_a1 = reacquired_sheet.get_cell("A1");
    check(reacquired_a1.kind() == fastxlsx::CellValueKind::Text &&
            reacquired_a1.text_value() == "fixed",
        "last-error recovery strict-options reacquire should read the successful overwrite");
    check(!reacquired_sheet.try_cell("D4").has_value(),
        "last-error recovery strict-options reacquire should keep rejected D4 absent");
    check_saved_default_data_overwrite_snapshots(
        reacquired_editor,
        reacquired_sheet,
        0,
        "last-error recovery strict-options reacquired handle",
        "fixed");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_noop,
        "last-error recovery strict-options reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_noop,
        "last-error recovery strict-options reacquired noop save");
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "last-error recovery",
        "strict-options reacquired noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "last-error recovery strict-options reacquired noop save should keep the sheet clean");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "last-error recovery strict-options reacquired noop save should stay within the original budget");
    check_saved_default_data_overwrite_snapshots(
        reacquired_editor,
        reacquired_sheet,
        0,
        "last-error recovery strict-options reacquired noop saved handle",
        "fixed");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "last-error recovery strict-options reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "last-error recovery strict-options reacquired noop save should leave the saved input unchanged");
    check_reopened_last_error_recovery_output(reacquired_noop_output, options);

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_second_noop,
        "last-error recovery strict-options reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_second_noop,
        "last-error recovery strict-options reacquired second noop save");
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "last-error recovery",
        "strict-options reacquired second noop save");
    check(!reacquired_sheet.has_pending_changes(),
        "last-error recovery strict-options reacquired second noop save should keep the sheet clean");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "last-error recovery strict-options reacquired second noop save should stay within the original budget");
    check_saved_default_data_overwrite_snapshots(
        reacquired_editor,
        reacquired_sheet,
        0,
        "last-error recovery strict-options reacquired second noop saved handle",
        "fixed");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "last-error recovery strict-options reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "last-error recovery strict-options reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "last-error recovery strict-options reacquired second noop save should leave the saved input unchanged");
    check_reopened_last_error_recovery_output(
        reacquired_second_noop_output, options);

    reacquired_sheet.set_cell("A1", fastxlsx::CellValue::text("final"));
    check(!reacquired_editor.last_edit_error().has_value(),
        "last-error recovery strict-options reacquired post-noop overwrite should keep diagnostics clear");
    check(reacquired_sheet.has_pending_changes(),
        "last-error recovery strict-options reacquired post-noop overwrite should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "last-error recovery strict-options reacquired post-noop overwrite should dirty the editor");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "last-error recovery strict-options reacquired post-noop overwrite should stay within the original budget");
    check(reacquired_editor.pending_materialized_cell_count() == baseline_count,
        "last-error recovery strict-options reacquired post-noop overwrite should keep sparse count stable");
    check_public_state_single_data_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        0,
        "last-error recovery strict-options reacquired post-noop overwrite");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "last-error recovery strict-options reacquired post-noop overwrite should not queue replacement diagnostics");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "last-error recovery strict-options reacquired post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "last-error recovery strict-options reacquired post-noop save should keep one handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "last-error recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "last-error recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "last-error recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "last-error recovery strict-options reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "last-error recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
    check(!reacquired_editor.last_edit_error().has_value(),
        "last-error recovery strict-options reacquired post-noop save should keep diagnostics clear");
    check_saved_default_data_overwrite_snapshots(
        reacquired_editor,
        reacquired_sheet,
        1,
        "last-error recovery strict-options reacquired post-noop saved handle",
        "final");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, "final",
        "last-error recovery strict-options reacquired post-noop save should persist the later overwrite");
    check_not_contains(reacquired_post_noop_xml, "invalid-reference-payload",
        "last-error recovery strict-options reacquired post-noop save should not leak the invalid-reference payload");
    check_not_contains(reacquired_post_noop_xml, "replacement-memory-diagnostic",
        "last-error recovery strict-options reacquired post-noop save should not leak the memory-budget payload");
    check_not_contains(reacquired_post_noop_xml, R"(r="D4")",
        "last-error recovery strict-options reacquired post-noop save should keep rejected D4 absent");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "last-error recovery strict-options reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) == reacquired_second_noop_entries,
        "last-error recovery strict-options reacquired post-noop save should leave the second noop output unchanged");
    check(reacquired_noop_entries == output_entries,
        "last-error recovery strict-options reacquired post-noop save should leave the first noop output stable");
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "last-error recovery strict-options reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "last-error recovery strict-options reacquired post-noop save should leave the saved input unchanged");
    check_reopened_last_error_recovery_output(
        reacquired_post_noop_output, options, "final");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "last-error replacement noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "last-error replacement noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "last-error replacement noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "last-error replacement noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "last-error replacement noop save should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "last-error replacement noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "last-error replacement noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "last-error replacement noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "last-error replacement noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "last-error replacement noop save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "last-error replacement noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "last-error replacement noop save should leave the source package unchanged");
    check_reopened_last_error_recovery_output(noop_output, options);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "last-error replacement second noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "last-error replacement second noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "last-error replacement second noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "last-error replacement second noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "last-error replacement second noop save should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "last-error replacement second noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "last-error replacement second noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "last-error replacement second noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "last-error replacement second noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "last-error replacement second noop save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "last-error replacement second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "last-error replacement second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "last-error replacement second noop save should leave the source package unchanged");
    check_reopened_last_error_recovery_output(second_noop_output, options);
}

void test_public_workbook_editor_last_edit_error_replaces_mixed_edit_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-mixed-last-error-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-reacquired-post-noop-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-mixed-last-error-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    bool replacement_failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::text("missing-replacement-payload")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        replacement_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "missing replacement should seed last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "replacement last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "Missing",
                "replacement diagnostic should mention the missing sheet");
            check_contains(*last_error, "current planned catalog",
                "replacement diagnostic should retain planned-catalog context");
        }
    }
    check(replacement_failed,
        "missing replacement should fail before mixed diagnostic replacement");
    const std::optional<std::string> replacement_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);

    bool rename_failed = false;
    try {
        editor.rename_sheet("Data", "Bad/Name");
    } catch (const fastxlsx::FastXlsxError& error) {
        rename_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid rename should replace replacement last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "rename last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "Bad/Name",
                "rename diagnostic should mention the rejected sheet name");
            check_not_contains(*last_error, "Missing",
                "rename diagnostic should replace the missing replacement diagnostic");
        }
    }
    check(rename_failed,
        "invalid rename should fail during mixed diagnostic replacement");
    const std::optional<std::string> rename_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, rename_error);

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    bool mutation_failed = false;
    try {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-mutation-payload"));
    } catch (const fastxlsx::FastXlsxError& error) {
        mutation_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid WorksheetEditor mutation should replace rename last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "WorksheetEditor mutation last_edit_error should match the thrown diagnostic");
            check_contains(*last_error, "WorksheetEditor cell reference is invalid",
                "WorksheetEditor mutation diagnostic should mention the invalid reference");
            check_not_contains(*last_error, "Bad/Name",
                "WorksheetEditor mutation should replace the rename diagnostic");
            check_not_contains(*last_error, "Missing",
                "WorksheetEditor mutation should not retain the older replacement diagnostic");
        }
    }
    check(mutation_failed,
        "invalid WorksheetEditor mutation should fail during mixed diagnostic replacement");

    check(!sheet.has_pending_changes(),
        "mixed failed edits should leave the materialized session clean");
    check(!editor.has_pending_changes(),
        "mixed failed edits should leave the editor clean");
    check(editor.pending_change_count() == 0,
        "mixed failed edits should not add public pending changes");
    check(editor.pending_replacement_worksheet_names().empty(),
        "mixed failed edits should not leave pending replacements");
    check(editor.pending_materialized_worksheet_names().empty(),
        "mixed failed edits should not leave dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "mixed failed edits should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "mixed failed edits should not leave dirty materialized memory");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mixed failed edits should leave the source package unchanged");

    editor.replace_sheet_data("Untouched",
        {{fastxlsx::CellValue::text("mixed-diagnostic-recovered")}});
    check(!editor.last_edit_error().has_value(),
        "successful public edit should clear mixed last_edit_error");
    check(editor.has_pending_changes(),
        "successful recovery replacement should dirty the editor");
    check(editor.pending_change_count() == 1,
        "successful recovery replacement should add one public pending change");
    check(editor.has_pending_replacement("Untouched"),
        "successful recovery replacement should be tracked under the target sheet");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialized Data session should not become dirty after other-sheet recovery");
    check(editor.pending_materialized_cell_count() == 0,
        "clean materialized Data session should not expose dirty cells after other-sheet recovery");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialized Data session should not expose dirty memory after other-sheet recovery");

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mixed diagnostic recovery save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "placeholder-a1",
        "clean Data materialized session should remain copy-original after mixed failures");
    check_not_contains(data_xml, "invalid-mutation-payload",
        "invalid WorksheetEditor payload should not leak into Data output");
    check_contains(untouched_xml, "mixed-diagnostic-recovered",
        "successful recovery replacement should persist on Untouched");
    check_not_contains(untouched_xml, "missing-replacement-payload",
        "failed replacement payload should not leak into output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Bad/Name",
        "failed rename target should not leak into workbook catalog");
    check_reopened_mixed_last_error_recovery_output(output);

    fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_data =
        reacquired_editor.worksheet("Data");
    fastxlsx::WorksheetEditor reacquired_untouched =
        reacquired_editor.worksheet("Untouched");
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "mixed diagnostic recovery",
        "saved-output reacquire");
    check(!reacquired_data.has_pending_changes() &&
            !reacquired_untouched.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquire should keep sheets clean");
    check(reacquired_data.cell_count() == 3,
        "mixed diagnostic recovery saved-output reacquire should keep Data source sparse count");
    check(reacquired_untouched.cell_count() == 1,
        "mixed diagnostic recovery saved-output reacquire should keep replacement sparse count");
    check(!reacquired_untouched.try_cell("B1").has_value(),
        "mixed diagnostic recovery saved-output reacquire should keep old Untouched B1 absent");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_noop,
        "mixed diagnostic recovery saved-output reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_noop,
        "mixed diagnostic recovery saved-output reacquired noop save");
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "mixed diagnostic recovery",
        "saved-output reacquired noop save");
    check(!reacquired_data.has_pending_changes() &&
            !reacquired_untouched.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired noop save should keep sheets clean");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "mixed diagnostic recovery saved-output reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mixed diagnostic recovery saved-output reacquired noop save should leave the saved input unchanged");
    check_reopened_mixed_last_error_recovery_output(reacquired_noop_output);

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor, reacquired_save_state_before_second_noop,
        "mixed diagnostic recovery saved-output reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor, reacquired_catalog_before_second_noop,
        "mixed diagnostic recovery saved-output reacquired second noop save");
    check_reopened_editor_clean_public_state(
        reacquired_editor,
        "mixed diagnostic recovery",
        "saved-output reacquired second noop save");
    check(!reacquired_data.has_pending_changes() &&
            !reacquired_untouched.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired second noop save should keep sheets clean");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "mixed diagnostic recovery saved-output reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "mixed diagnostic recovery saved-output reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mixed diagnostic recovery saved-output reacquired second noop save should leave the saved input unchanged");
    check_reopened_mixed_last_error_recovery_output(
        reacquired_second_noop_output);

    reacquired_untouched.set_cell("A1",
        fastxlsx::CellValue::text("mixed-diagnostic-final"));
    check(!reacquired_editor.last_edit_error().has_value(),
        "mixed diagnostic recovery saved-output reacquired post-noop edit should keep diagnostics clear");
    check(!reacquired_data.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired post-noop edit should keep Data clean");
    check(reacquired_untouched.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired post-noop edit should dirty Untouched");
    check(reacquired_editor.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired post-noop edit should dirty the editor");
    check(reacquired_editor.pending_materialized_cell_count() ==
            reacquired_untouched.cell_count(),
        "mixed diagnostic recovery saved-output reacquired post-noop edit should expose Untouched cells only");
    check_public_state_single_named_dirty_materialized_summary(
        reacquired_editor,
        reacquired_untouched,
        "Untouched",
        0,
        "mixed diagnostic recovery saved-output reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mixed diagnostic recovery saved-output reacquired post-noop edit should not queue replacement diagnostics");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_data.has_pending_changes() &&
            !reacquired_untouched.has_pending_changes(),
        "mixed diagnostic recovery saved-output reacquired post-noop save should keep sheets clean");
    check(reacquired_editor.pending_change_count() == 1,
        "mixed diagnostic recovery saved-output reacquired post-noop save should keep one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "mixed diagnostic recovery saved-output reacquired post-noop save should not expose dirty worksheet names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "mixed diagnostic recovery saved-output reacquired post-noop save should not expose dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "mixed diagnostic recovery saved-output reacquired post-noop save should not expose dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "mixed diagnostic recovery saved-output reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mixed diagnostic recovery saved-output reacquired post-noop save should not queue replacement diagnostics");
    check(!reacquired_editor.last_edit_error().has_value(),
        "mixed diagnostic recovery saved-output reacquired post-noop save should keep diagnostics clear");

    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_data_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    const std::string reacquired_post_noop_untouched_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet2.xml");
    check_contains(reacquired_post_noop_data_xml, "placeholder-a1",
        "mixed diagnostic recovery saved-output reacquired post-noop save should keep Data copy-original");
    check_not_contains(reacquired_post_noop_data_xml, "invalid-mutation-payload",
        "mixed diagnostic recovery saved-output reacquired post-noop save should not leak invalid WorksheetEditor payload");
    check_contains(reacquired_post_noop_untouched_xml, "mixed-diagnostic-final",
        "mixed diagnostic recovery saved-output reacquired post-noop save should persist the later edit");
    check_not_contains(reacquired_post_noop_untouched_xml, "mixed-diagnostic-recovered",
        "mixed diagnostic recovery saved-output reacquired post-noop save should replace the earlier recovery text");
    check_not_contains(reacquired_post_noop_untouched_xml, "missing-replacement-payload",
        "mixed diagnostic recovery saved-output reacquired post-noop save should not leak missing replacement payload");
    check_not_contains(reacquired_post_noop_entries.at("xl/workbook.xml"), "Bad/Name",
        "mixed diagnostic recovery saved-output reacquired post-noop save should not leak failed rename target");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) == reacquired_noop_entries,
        "mixed diagnostic recovery saved-output reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) == reacquired_second_noop_entries,
        "mixed diagnostic recovery saved-output reacquired post-noop save should leave the second noop output unchanged");
    check(reacquired_noop_entries == output_entries,
        "mixed diagnostic recovery saved-output reacquired post-noop save should leave the first noop output stable");
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "mixed diagnostic recovery saved-output reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mixed diagnostic recovery saved-output reacquired post-noop save should leave the saved input unchanged");
    check_reopened_mixed_last_error_recovery_output(
        reacquired_post_noop_output,
        "mixed-diagnostic-final");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_noop =
        editor.pending_worksheet_edits();
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "mixed diagnostic recovery noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "mixed diagnostic recovery noop save should not add a second public handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "mixed diagnostic recovery noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "mixed diagnostic recovery noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "mixed diagnostic recovery noop save should not expose dirty materialized memory");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "mixed diagnostic recovery noop save");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_noop),
        "mixed diagnostic recovery noop save should preserve pending summaries");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "mixed diagnostic recovery noop save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "mixed diagnostic recovery noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mixed diagnostic recovery noop save should leave the source package unchanged");
    check_reopened_mixed_last_error_recovery_output(noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_second_noop =
        editor.pending_worksheet_edits();
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "mixed diagnostic recovery second noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "mixed diagnostic recovery second noop save should not add another public handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "mixed diagnostic recovery second noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "mixed diagnostic recovery second noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "mixed diagnostic recovery second noop save should not expose dirty materialized memory");
    check(!editor.last_edit_error().has_value(),
        "mixed diagnostic recovery second noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "mixed diagnostic recovery second noop save");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_second_noop),
        "mixed diagnostic recovery second noop save should preserve pending summaries");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "mixed diagnostic recovery second noop save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "mixed diagnostic recovery second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "mixed diagnostic recovery second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mixed diagnostic recovery second noop save should leave the source package unchanged");
    check_reopened_mixed_last_error_recovery_output(second_noop_output);
}


} // namespace

int main()
{
    try {
        test_public_worksheet_editor_last_edit_error_replaces_failed_mutation_diagnostics();
        test_public_workbook_editor_last_edit_error_replaces_mixed_edit_diagnostics();
        std::cout << "WorkbookEditor public-state last edit error tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state last edit error test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
