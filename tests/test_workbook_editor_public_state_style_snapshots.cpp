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

void test_public_worksheet_editor_snapshots_preserve_source_style_handles()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-post-noop-noop-output.xlsx");
    const std::filesystem::path reopened_post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-output.xlsx");
    const std::filesystem::path reopened_post_noop_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-noop-output.xlsx");
    const std::filesystem::path reopened_post_noop_clear_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-clear-output.xlsx");
    const std::filesystem::path reopened_post_noop_clear_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-clear-noop-output.xlsx");
    const std::filesystem::path reopened_post_noop_clear_reedit_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-clear-reedit-output.xlsx");
    const std::filesystem::path reopened_post_noop_clear_reedit_noop_output = artifact(
        "fastxlsx-workbook-editor-public-snapshot-source-style-reopened-post-noop-clear-reedit-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        {
            fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
            non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
            styled_sheet.append_row({
                fastxlsx::CellView::number(1.0).with_style(non_default_style),
                fastxlsx::CellView::text("unstyled-b1"),
            });
            styled_sheet.append_row({fastxlsx::CellView::text("unstyled-a2")});
        }
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const auto check_styled_a1_snapshot =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 1.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                std::string(message_prefix) + " should expose source style handle on A1");
        };
    const auto check_styled_blank_a1_snapshot =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                std::string(message_prefix) + " should expose source style handle on blank A1");
        };
    const auto check_unstyled_b1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "unstyled-b1" &&
                    !snapshot.value.has_style(),
                std::string(message_prefix) + " should keep B1 unstyled");
        };
    const auto check_post_noop_a1_snapshot =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 2.5 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                std::string(message_prefix) +
                    " should expose source style handle on post-noop A1");
        };
    const auto check_post_noop_b1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "post-noop-b1" &&
                    !snapshot.value.has_style(),
                std::string(message_prefix) + " should keep post-noop B1 unstyled");
        };
    const auto check_reedited_a1_snapshot =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view message_prefix) {
            check(snapshot.reference.row == 1 && snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 4.75 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                std::string(message_prefix) +
                    " should expose source style handle on re-edited A1");
        };
    const auto inspect_saved_snapshot_output =
        [non_default_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            const auto check_saved_blank =
                [non_default_style](
                    const fastxlsx::WorksheetCellSnapshot& snapshot,
                    std::string_view message_prefix) {
                    check(snapshot.reference.row == 1 && snapshot.reference.column == 1 &&
                            snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                            snapshot.value.has_style() &&
                            snapshot.value.style_id().value() == non_default_style.value(),
                        std::string(message_prefix) +
                            " should read the saved styled blank A1");
                };
            const auto check_saved_unstyled_b1 =
                [](const fastxlsx::WorksheetCellSnapshot& snapshot,
                    std::string_view message_prefix) {
                    check(snapshot.reference.row == 1 && snapshot.reference.column == 2 &&
                            snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                            snapshot.value.text_value() == "unstyled-b1" &&
                            !snapshot.value.has_style(),
                        std::string(message_prefix) + " should keep saved B1 unstyled");
                };
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style reopened sparse_cells should keep three records");
            check_saved_blank(all_cells[0],
                "snapshot source-style reopened sparse_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "snapshot source-style reopened row_cells should keep row-one records");
            check_saved_blank(row_one[0],
                "snapshot source-style reopened row_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                "snapshot source-style reopened column_cells should keep column-one records");
            check_saved_blank(column_one[0],
                "snapshot source-style reopened column_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
                reopened_sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 1});
            check(range_cells.size() == 1,
                "snapshot source-style reopened range sparse_cells should keep A1");
            check_saved_blank(range_cells[0],
                "snapshot source-style reopened range sparse_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> strict_a1_cells =
                reopened_sheet.sparse_cells("A1:B1");
            check(strict_a1_cells.size() == 2,
                "snapshot source-style reopened A1 sparse_cells should keep row-one records");
            check_saved_blank(strict_a1_cells[0],
                "snapshot source-style reopened A1 sparse_cells");
            check_saved_unstyled_b1(strict_a1_cells[1],
                "snapshot source-style reopened A1 sparse_cells");
            const std::array<fastxlsx::WorksheetCellReference, 3> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 2,
                "snapshot source-style reopened coordinate batch should skip missing cells");
            check_saved_blank(requested_cells[0],
                "snapshot source-style reopened coordinate batch");
            check_saved_unstyled_b1(requested_cells[1],
                "snapshot source-style reopened coordinate batch");
            const std::vector<fastxlsx::WorksheetCellSnapshot> initializer_cells =
                reopened_sheet.sparse_cells({
                    {1, 1},
                    {1, 2},
                });
            check(initializer_cells.size() == 2,
                "snapshot source-style reopened initializer sparse_cells should keep row-one records");
            check_saved_blank(initializer_cells[0],
                "snapshot source-style reopened initializer sparse_cells");
            check_saved_unstyled_b1(initializer_cells[1],
                "snapshot source-style reopened initializer sparse_cells");
        };
    const auto inspect_post_noop_snapshot_output =
        [check_post_noop_a1_snapshot, check_post_noop_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style post-noop reopened sparse_cells should keep three records");
            check_post_noop_a1_snapshot(all_cells[0],
                "snapshot source-style post-noop reopened sparse_cells");
            check_post_noop_b1_snapshot(all_cells[1],
                "snapshot source-style post-noop reopened sparse_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "snapshot source-style post-noop reopened row_cells should keep row-one records");
            check_post_noop_a1_snapshot(row_one[0],
                "snapshot source-style post-noop reopened row_cells");
            check_post_noop_b1_snapshot(row_one[1],
                "snapshot source-style post-noop reopened row_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                "snapshot source-style post-noop reopened column_cells should keep column-one records");
            check_post_noop_a1_snapshot(column_one[0],
                "snapshot source-style post-noop reopened column_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
                reopened_sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 1});
            check(range_cells.size() == 1,
                "snapshot source-style post-noop reopened range sparse_cells should keep A1");
            check_post_noop_a1_snapshot(range_cells[0],
                "snapshot source-style post-noop reopened range sparse_cells");
            const std::vector<fastxlsx::WorksheetCellSnapshot> strict_a1_cells =
                reopened_sheet.sparse_cells("A1:B1");
            check(strict_a1_cells.size() == 2,
                "snapshot source-style post-noop reopened A1 sparse_cells should keep row-one records");
            check_post_noop_a1_snapshot(strict_a1_cells[0],
                "snapshot source-style post-noop reopened A1 sparse_cells");
            check_post_noop_b1_snapshot(strict_a1_cells[1],
                "snapshot source-style post-noop reopened A1 sparse_cells");
            const std::array<fastxlsx::WorksheetCellReference, 3> requested_refs {
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                reopened_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 2,
                "snapshot source-style post-noop reopened coordinate batch should skip missing cells");
            check_post_noop_a1_snapshot(requested_cells[0],
                "snapshot source-style post-noop reopened coordinate batch");
            check_post_noop_b1_snapshot(requested_cells[1],
                "snapshot source-style post-noop reopened coordinate batch");
            const std::vector<fastxlsx::WorksheetCellSnapshot> initializer_cells =
                reopened_sheet.sparse_cells({
                    {1, 1},
                    {1, 2},
                });
            check(initializer_cells.size() == 2,
                "snapshot source-style post-noop reopened initializer sparse_cells should keep row-one records");
            check_post_noop_a1_snapshot(initializer_cells[0],
                "snapshot source-style post-noop reopened initializer sparse_cells");
            check_post_noop_b1_snapshot(initializer_cells[1],
                "snapshot source-style post-noop reopened initializer sparse_cells");
        };
    const auto inspect_original_source_snapshot_output =
        [check_styled_a1_snapshot, check_unstyled_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style source reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style source reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style source reopened sparse_cells should keep three records");
            check_styled_a1_snapshot(all_cells[0],
                "snapshot source-style source reopened sparse_cells");
            check_unstyled_b1_snapshot(all_cells[1],
                "snapshot source-style source reopened sparse_cells");
            check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 1 &&
                    all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    all_cells[2].value.text_value() == "unstyled-a2" &&
                    !all_cells[2].value.has_style(),
                "snapshot source-style source reopened sparse_cells should keep A2 unstyled");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "snapshot source-style source reopened row_cells should keep row-one records");
            check_styled_a1_snapshot(row_one[0],
                "snapshot source-style source reopened row_cells");
            check_unstyled_b1_snapshot(row_one[1],
                "snapshot source-style source reopened row_cells");
        };
    const auto inspect_reopened_post_noop_snapshot_output =
        [check_post_noop_a1_snapshot, check_post_noop_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style reopened post-noop output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style reopened post-noop output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style reopened post-noop sparse_cells should keep three records");
            check_post_noop_a1_snapshot(all_cells[0],
                "snapshot source-style reopened post-noop sparse_cells");
            check_post_noop_b1_snapshot(all_cells[1],
                "snapshot source-style reopened post-noop sparse_cells");
            check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 1 &&
                    all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    all_cells[2].value.text_value() == "reopened-post-noop-a2" &&
                    !all_cells[2].value.has_style(),
                "snapshot source-style reopened post-noop sparse_cells should keep edited A2 unstyled");
        };
    const auto inspect_reopened_post_noop_clear_output =
        [check_styled_blank_a1_snapshot, check_post_noop_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style reopened post-noop clear output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style reopened post-noop clear output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style reopened post-noop clear sparse_cells should keep three records");
            check_styled_blank_a1_snapshot(all_cells[0],
                "snapshot source-style reopened post-noop clear sparse_cells");
            check_post_noop_b1_snapshot(all_cells[1],
                "snapshot source-style reopened post-noop clear sparse_cells");
            check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 1 &&
                    all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    all_cells[2].value.text_value() == "reopened-post-noop-a2" &&
                    !all_cells[2].value.has_style(),
                "snapshot source-style reopened post-noop clear sparse_cells should keep edited A2 unstyled");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "snapshot source-style reopened post-noop clear row_cells should keep row-one records");
            check_styled_blank_a1_snapshot(row_one[0],
                "snapshot source-style reopened post-noop clear row_cells");
            check_post_noop_b1_snapshot(row_one[1],
                "snapshot source-style reopened post-noop clear row_cells");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                "snapshot source-style reopened post-noop clear column_cells should keep column-one records");
            check_styled_blank_a1_snapshot(column_one[0],
                "snapshot source-style reopened post-noop clear column_cells");
        };
    const auto inspect_reopened_post_noop_clear_reedit_output =
        [check_reedited_a1_snapshot, check_post_noop_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "snapshot source-style reopened post-noop clear reedit output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                "snapshot source-style reopened post-noop clear reedit output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
                reopened_sheet.sparse_cells();
            check(all_cells.size() == 3,
                "snapshot source-style reopened post-noop clear reedit sparse_cells should keep three records");
            check_reedited_a1_snapshot(all_cells[0],
                "snapshot source-style reopened post-noop clear reedit sparse_cells");
            check_post_noop_b1_snapshot(all_cells[1],
                "snapshot source-style reopened post-noop clear reedit sparse_cells");
            check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 1 &&
                    all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    all_cells[2].value.text_value() == "reopened-post-noop-a2" &&
                    !all_cells[2].value.has_style(),
                "snapshot source-style reopened post-noop clear reedit sparse_cells should keep edited A2 unstyled");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "snapshot source-style reopened post-noop clear reedit row_cells should keep row-one records");
            check_reedited_a1_snapshot(row_one[0],
                "snapshot source-style reopened post-noop clear reedit row_cells");
            check_post_noop_b1_snapshot(row_one[1],
                "snapshot source-style reopened post-noop clear reedit row_cells");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                "snapshot source-style reopened post-noop clear reedit column_cells should keep column-one records");
            check_reedited_a1_snapshot(column_one[0],
                "snapshot source-style reopened post-noop clear reedit column_cells");
        };

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells = sheet.sparse_cells();
    check(all_cells.size() == 3,
        "snapshot source-style sparse_cells should return all represented records");
    check_styled_a1_snapshot(all_cells[0], "snapshot source-style sparse_cells");
    check_unstyled_b1_snapshot(all_cells[1], "snapshot source-style sparse_cells");
    check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 1 &&
            all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[2].value.text_value() == "unstyled-a2" &&
            !all_cells[2].value.has_style(),
        "snapshot source-style sparse_cells should keep A2 unstyled");

    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 1});
    check(range_cells.size() == 1,
        "snapshot source-style range sparse_cells should return A1");
    check_styled_a1_snapshot(range_cells[0],
        "snapshot source-style range sparse_cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> a1_range_cells =
        sheet.sparse_cells("A1:B1");
    check(a1_range_cells.size() == 2,
        "snapshot source-style A1 range sparse_cells should return row-one records");
    check_styled_a1_snapshot(a1_range_cells[0],
        "snapshot source-style A1 range sparse_cells");
    check_unstyled_b1_snapshot(a1_range_cells[1],
        "snapshot source-style A1 range sparse_cells");

    const std::array<fastxlsx::WorksheetCellReference, 3> requested_cells {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_snapshots =
        sheet.sparse_cells(requested_cells);
    check(requested_snapshots.size() == 2,
        "snapshot source-style coordinate batch should skip missing cells");
    check_styled_a1_snapshot(requested_snapshots[0],
        "snapshot source-style coordinate batch");
    check_unstyled_b1_snapshot(requested_snapshots[1],
        "snapshot source-style coordinate batch");

    const std::vector<fastxlsx::WorksheetCellSnapshot> initializer_snapshots =
        sheet.sparse_cells({
            {1, 1},
            {1, 2},
            {3, 3},
        });
    check(initializer_snapshots.size() == 2,
        "snapshot source-style initializer batch should skip missing cells");
    check_styled_a1_snapshot(initializer_snapshots[0],
        "snapshot source-style initializer batch");
    check_unstyled_b1_snapshot(initializer_snapshots[1],
        "snapshot source-style initializer batch");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2,
        "snapshot source-style row_cells should return row-one records");
    check_styled_a1_snapshot(row_one[0], "snapshot source-style row_cells");
    check_unstyled_b1_snapshot(row_one[1], "snapshot source-style row_cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one = sheet.column_cells(1);
    check(column_one.size() == 2,
        "snapshot source-style column_cells should return column-one records");
    check_styled_a1_snapshot(column_one[0], "snapshot source-style column_cells");
    check(column_one[1].reference.row == 2 && column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "unstyled-a2" &&
            !column_one[1].value.has_style(),
        "snapshot source-style column_cells should keep A2 unstyled");

    sheet.clear_cell_value(1, 1);
    const std::vector<fastxlsx::WorksheetCellSnapshot> cleared_row_one =
        sheet.row_cells(1);
    check(cleared_row_one.size() == 2,
        "snapshot source-style clear should preserve row-one records");
    check_styled_blank_a1_snapshot(cleared_row_one[0],
        "snapshot source-style clear row_cells");
    check_unstyled_b1_snapshot(cleared_row_one[1],
        "snapshot source-style clear row_cells");
    check(sheet.has_pending_changes(),
        "snapshot source-style clear should dirty the materialized sheet");
    check(editor.pending_materialized_cell_count() == 3,
        "snapshot source-style clear should keep aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "snapshot source-style clear dirty summary");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_blank =
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("/>)";
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style save should leave the source workbook unchanged");
    check_contains(worksheet_xml, styled_blank,
        "snapshot source-style save should persist styled blank A1");
    check_contains(worksheet_xml, "unstyled-b1",
        "snapshot source-style save should keep unstyled B1 text");
    check_contains(worksheet_xml, "unstyled-a2",
        "snapshot source-style save should keep unstyled A2 text");
    check_reopened_clean_sheet_output(output, "Styled",
        "snapshot source-style save", inspect_saved_snapshot_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "snapshot source-style no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "snapshot source-style no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "snapshot source-style no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "snapshot source-style no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "snapshot source-style no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "snapshot source-style no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "snapshot source-style no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "snapshot source-style no-op output should match the first output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style no-op save should leave the source workbook unchanged");
    check_reopened_clean_sheet_output(noop_output, "Styled",
        "snapshot source-style no-op save", inspect_saved_snapshot_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "snapshot source-style second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "snapshot source-style second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "snapshot source-style second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "snapshot source-style second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "snapshot source-style second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "snapshot source-style second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "snapshot source-style second no-op save");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "snapshot source-style second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "snapshot source-style second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style second no-op save should leave the source workbook unchanged");
    check_reopened_clean_sheet_output(second_noop_output, "Styled",
        "snapshot source-style second no-op save", inspect_saved_snapshot_output);

    sheet.set_cell_value("A1", fastxlsx::CellValue::number(2.5));
    sheet.set_cell_value("B1", fastxlsx::CellValue::text("post-noop-b1"));
    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_one =
        sheet.row_cells(1);
    check(post_noop_row_one.size() == 2,
        "snapshot source-style post-noop edit should keep row-one records");
    check_post_noop_a1_snapshot(post_noop_row_one[0],
        "snapshot source-style post-noop edit row_cells");
    check_post_noop_b1_snapshot(post_noop_row_one[1],
        "snapshot source-style post-noop edit row_cells");
    check(sheet.has_pending_changes(),
        "snapshot source-style post-noop edit should dirty the saved handle");
    check(editor.pending_materialized_cell_count() == 3,
        "snapshot source-style post-noop edit should keep aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 1,
        "snapshot source-style post-noop edit dirty summary");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "snapshot source-style post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "snapshot source-style post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "snapshot source-style post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "snapshot source-style post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "snapshot source-style post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "snapshot source-style post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "snapshot source-style post-noop save should leave the second no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style post-noop save should leave the source workbook unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)",
        "snapshot source-style post-noop save should persist styled value-only A1");
    check_contains(post_noop_xml, "post-noop-b1",
        "snapshot source-style post-noop save should persist unstyled B1 edit");
    check_contains(post_noop_xml, "unstyled-a2",
        "snapshot source-style post-noop save should keep unstyled A2 text");
    check_not_contains(post_noop_xml, styled_blank,
        "snapshot source-style post-noop save should replace the prior styled blank");
    check_reopened_clean_sheet_output(post_noop_output, "Styled",
        "snapshot source-style post-noop save", inspect_post_noop_snapshot_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "snapshot source-style post-noop no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "snapshot source-style post-noop no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style post-noop no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "snapshot source-style post-noop no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "snapshot source-style post-noop no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "snapshot source-style post-noop no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "snapshot source-style post-noop no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "snapshot source-style post-noop no-op save");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "snapshot source-style post-noop no-op save should leave the post-noop output unchanged");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "snapshot source-style post-noop no-op output should match the post-noop output");
    check_reopened_clean_sheet_output(post_noop_noop_output, "Styled",
        "snapshot source-style post-noop no-op save", inspect_post_noop_snapshot_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style post-noop no-op save should leave the source workbook unchanged");
    check_reopened_clean_sheet_output(source, "Styled",
        "snapshot source-style source after post-noop no-op save",
        inspect_original_source_snapshot_output);

    fastxlsx::WorkbookEditor reopened_editor =
        fastxlsx::WorkbookEditor::open(post_noop_noop_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Styled");
    reopened_sheet.set_cell_value("A2",
        fastxlsx::CellValue::text("reopened-post-noop-a2"));
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
        reopened_sheet.column_cells(1);
    check(reopened_column_one.size() == 2,
        "snapshot source-style reopened post-noop edit should keep column-one records");
    check_post_noop_a1_snapshot(reopened_column_one[0],
        "snapshot source-style reopened post-noop edit column_cells");
    check(reopened_column_one[1].reference.row == 2 &&
            reopened_column_one[1].reference.column == 1 &&
            reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_column_one[1].value.text_value() == "reopened-post-noop-a2" &&
            !reopened_column_one[1].value.has_style(),
        "snapshot source-style reopened post-noop edit should keep A2 unstyled");
    check(reopened_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop edit should dirty the reopened handle");
    check(reopened_editor.pending_materialized_cell_count() == 3,
        "snapshot source-style reopened post-noop edit should keep aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        reopened_editor, reopened_sheet, "Styled", 0,
        "snapshot source-style reopened post-noop edit dirty summary");

    reopened_editor.save_as(reopened_post_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop save should clean the reopened handle");
    check(reopened_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop save should record one materialized handoff");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop save should keep dirty diagnostics clear");
    check(reopened_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor,
        "snapshot source-style reopened post-noop save should not queue replacement diagnostics");
    check(!reopened_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_noop_output) == post_noop_noop_entries,
        "snapshot source-style reopened post-noop save should leave the no-op output unchanged");
    const auto reopened_post_noop_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_output);
    const std::string reopened_post_noop_xml =
        reopened_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_post_noop_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)",
        "snapshot source-style reopened post-noop save should keep styled A1");
    check_contains(reopened_post_noop_xml, "post-noop-b1",
        "snapshot source-style reopened post-noop save should keep unstyled B1");
    check_contains(reopened_post_noop_xml, "reopened-post-noop-a2",
        "snapshot source-style reopened post-noop save should persist edited A2");
    check_reopened_clean_sheet_output(reopened_post_noop_output, "Styled",
        "snapshot source-style reopened post-noop save",
        inspect_reopened_post_noop_snapshot_output);

    const WorkbookEditorPublicCatalogSnapshot reopened_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened_editor);
    const WorkbookEditorPublicSaveStateSnapshot reopened_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened_editor);
    reopened_editor.save_as(reopened_post_noop_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop no-op save should keep reopened handle clean");
    check(reopened_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop no-op save should not record another handoff");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop no-op save should keep dirty diagnostics clear");
    check(reopened_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_editor,
        "snapshot source-style reopened post-noop no-op save should not queue replacement diagnostics");
    check(!reopened_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        reopened_editor,
        reopened_save_state_before_noop,
        "snapshot source-style reopened post-noop no-op save");
    check_workbook_editor_public_catalog_preserved(
        reopened_editor,
        reopened_catalog_before_noop,
        "snapshot source-style reopened post-noop no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop no-op save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_noop_output) == post_noop_noop_entries,
        "snapshot source-style reopened post-noop no-op save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_output) == reopened_post_noop_entries,
        "snapshot source-style reopened post-noop no-op save should leave the edited output unchanged");
    const auto reopened_post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_noop_output);
    check(reopened_post_noop_noop_entries == reopened_post_noop_entries,
        "snapshot source-style reopened post-noop no-op output should match the edited output");
    check_reopened_clean_sheet_output(reopened_post_noop_noop_output, "Styled",
        "snapshot source-style reopened post-noop no-op save",
        inspect_reopened_post_noop_snapshot_output);

    fastxlsx::WorkbookEditor reopened_clear_editor =
        fastxlsx::WorkbookEditor::open(reopened_post_noop_noop_output);
    fastxlsx::WorksheetEditor reopened_clear_sheet =
        reopened_clear_editor.worksheet("Styled");
    reopened_clear_sheet.clear_cell_value("A1");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_clear_row_one =
        reopened_clear_sheet.row_cells(1);
    check(reopened_clear_row_one.size() == 2,
        "snapshot source-style reopened post-noop clear should keep row-one records");
    check_styled_blank_a1_snapshot(reopened_clear_row_one[0],
        "snapshot source-style reopened post-noop clear row_cells");
    check_post_noop_b1_snapshot(reopened_clear_row_one[1],
        "snapshot source-style reopened post-noop clear row_cells");
    check(reopened_clear_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear should dirty the fresh handle");
    check(reopened_clear_editor.pending_materialized_cell_count() == 3,
        "snapshot source-style reopened post-noop clear should keep aggregate materialized count");

    reopened_clear_editor.save_as(reopened_post_noop_clear_output);
    check(!reopened_clear_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear save should clean the fresh handle");
    check(reopened_clear_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop clear save should record one materialized handoff");
    check(reopened_clear_editor.pending_materialized_worksheet_names().empty() &&
            reopened_clear_editor.pending_materialized_cell_count() == 0 &&
            reopened_clear_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop clear save should keep dirty diagnostics clear");
    check(reopened_clear_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop clear save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_clear_editor,
        "snapshot source-style reopened post-noop clear save should not queue replacement diagnostics");
    check(!reopened_clear_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop clear save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop clear save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_noop_output) ==
            reopened_post_noop_noop_entries,
        "snapshot source-style reopened post-noop clear save should leave the prior no-op output unchanged");
    const auto reopened_post_noop_clear_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_clear_output);
    const std::string reopened_post_noop_clear_xml =
        reopened_post_noop_clear_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_post_noop_clear_xml, styled_blank,
        "snapshot source-style reopened post-noop clear save should persist styled blank A1");
    check_contains(reopened_post_noop_clear_xml, "post-noop-b1",
        "snapshot source-style reopened post-noop clear save should keep unstyled B1");
    check_contains(reopened_post_noop_clear_xml, "reopened-post-noop-a2",
        "snapshot source-style reopened post-noop clear save should keep edited A2");
    check_not_contains(reopened_post_noop_clear_xml, R"(<v>2.5</v>)",
        "snapshot source-style reopened post-noop clear save should remove the prior A1 value");
    check_reopened_clean_sheet_output(reopened_post_noop_clear_output, "Styled",
        "snapshot source-style reopened post-noop clear save",
        inspect_reopened_post_noop_clear_output);

    const WorkbookEditorPublicCatalogSnapshot reopened_clear_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened_clear_editor);
    const WorkbookEditorPublicSaveStateSnapshot reopened_clear_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened_clear_editor);
    reopened_clear_editor.save_as(reopened_post_noop_clear_noop_output);
    check(!reopened_clear_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear no-op save should keep fresh handle clean");
    check(reopened_clear_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop clear no-op save should not record another handoff");
    check(reopened_clear_editor.pending_materialized_worksheet_names().empty() &&
            reopened_clear_editor.pending_materialized_cell_count() == 0 &&
            reopened_clear_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop clear no-op save should keep dirty diagnostics clear");
    check(reopened_clear_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop clear no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_clear_editor,
        "snapshot source-style reopened post-noop clear no-op save should not queue replacement diagnostics");
    check(!reopened_clear_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop clear no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        reopened_clear_editor,
        reopened_clear_save_state_before_noop,
        "snapshot source-style reopened post-noop clear no-op save");
    check_workbook_editor_public_catalog_preserved(
        reopened_clear_editor,
        reopened_clear_catalog_before_noop,
        "snapshot source-style reopened post-noop clear no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop clear no-op save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_clear_output) ==
            reopened_post_noop_clear_entries,
        "snapshot source-style reopened post-noop clear no-op save should leave the clear output unchanged");
    const auto reopened_post_noop_clear_noop_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_clear_noop_output);
    check(reopened_post_noop_clear_noop_entries == reopened_post_noop_clear_entries,
        "snapshot source-style reopened post-noop clear no-op output should match the clear output");
    check_reopened_clean_sheet_output(reopened_post_noop_clear_noop_output, "Styled",
        "snapshot source-style reopened post-noop clear no-op save",
        inspect_reopened_post_noop_clear_output);

    fastxlsx::WorkbookEditor reopened_clear_reedit_editor =
        fastxlsx::WorkbookEditor::open(reopened_post_noop_clear_noop_output);
    fastxlsx::WorksheetEditor reopened_clear_reedit_sheet =
        reopened_clear_reedit_editor.worksheet("Styled");
    reopened_clear_reedit_sheet.set_cell_value("A1", fastxlsx::CellValue::number(4.75));
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_clear_reedit_row_one =
        reopened_clear_reedit_sheet.row_cells(1);
    check(reopened_clear_reedit_row_one.size() == 2,
        "snapshot source-style reopened post-noop clear reedit should keep row-one records");
    check_reedited_a1_snapshot(reopened_clear_reedit_row_one[0],
        "snapshot source-style reopened post-noop clear reedit row_cells");
    check_post_noop_b1_snapshot(reopened_clear_reedit_row_one[1],
        "snapshot source-style reopened post-noop clear reedit row_cells");
    check(reopened_clear_reedit_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear reedit should dirty the fresh handle");
    check(reopened_clear_reedit_editor.pending_materialized_cell_count() == 3,
        "snapshot source-style reopened post-noop clear reedit should keep aggregate materialized count");

    reopened_clear_reedit_editor.save_as(reopened_post_noop_clear_reedit_output);
    check(!reopened_clear_reedit_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear reedit save should clean the fresh handle");
    check(reopened_clear_reedit_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop clear reedit save should record one materialized handoff");
    check(reopened_clear_reedit_editor.pending_materialized_worksheet_names().empty() &&
            reopened_clear_reedit_editor.pending_materialized_cell_count() == 0 &&
            reopened_clear_reedit_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop clear reedit save should keep dirty diagnostics clear");
    check(reopened_clear_reedit_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop clear reedit save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_clear_reedit_editor,
        "snapshot source-style reopened post-noop clear reedit save should not queue replacement diagnostics");
    check(!reopened_clear_reedit_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop clear reedit save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop clear reedit save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_clear_noop_output) ==
            reopened_post_noop_clear_noop_entries,
        "snapshot source-style reopened post-noop clear reedit save should leave the prior no-op output unchanged");
    const auto reopened_post_noop_clear_reedit_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_clear_reedit_output);
    const std::string reopened_post_noop_clear_reedit_xml =
        reopened_post_noop_clear_reedit_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_post_noop_clear_reedit_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>4.75</v></c>)",
        "snapshot source-style reopened post-noop clear reedit save should persist styled A1");
    check_contains(reopened_post_noop_clear_reedit_xml, "post-noop-b1",
        "snapshot source-style reopened post-noop clear reedit save should keep unstyled B1");
    check_contains(reopened_post_noop_clear_reedit_xml, "reopened-post-noop-a2",
        "snapshot source-style reopened post-noop clear reedit save should keep edited A2");
    check_not_contains(reopened_post_noop_clear_reedit_xml, styled_blank,
        "snapshot source-style reopened post-noop clear reedit save should replace the styled blank");
    check_not_contains(reopened_post_noop_clear_reedit_xml, R"(<v>2.5</v>)",
        "snapshot source-style reopened post-noop clear reedit save should not revive the prior A1 value");
    check_reopened_clean_sheet_output(reopened_post_noop_clear_reedit_output, "Styled",
        "snapshot source-style reopened post-noop clear reedit save",
        inspect_reopened_post_noop_clear_reedit_output);

    const WorkbookEditorPublicCatalogSnapshot reopened_clear_reedit_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened_clear_reedit_editor);
    const WorkbookEditorPublicSaveStateSnapshot reopened_clear_reedit_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened_clear_reedit_editor);
    reopened_clear_reedit_editor.save_as(reopened_post_noop_clear_reedit_noop_output);
    check(!reopened_clear_reedit_sheet.has_pending_changes(),
        "snapshot source-style reopened post-noop clear reedit no-op save should keep fresh handle clean");
    check(reopened_clear_reedit_editor.pending_change_count() == 1,
        "snapshot source-style reopened post-noop clear reedit no-op save should not record another handoff");
    check(reopened_clear_reedit_editor.pending_materialized_worksheet_names().empty() &&
            reopened_clear_reedit_editor.pending_materialized_cell_count() == 0 &&
            reopened_clear_reedit_editor.estimated_pending_materialized_memory_usage() == 0,
        "snapshot source-style reopened post-noop clear reedit no-op save should keep dirty diagnostics clear");
    check(reopened_clear_reedit_editor.pending_worksheet_edits().empty(),
        "snapshot source-style reopened post-noop clear reedit no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reopened_clear_reedit_editor,
        "snapshot source-style reopened post-noop clear reedit no-op save should not queue replacement diagnostics");
    check(!reopened_clear_reedit_editor.last_edit_error().has_value(),
        "snapshot source-style reopened post-noop clear reedit no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        reopened_clear_reedit_editor,
        reopened_clear_reedit_save_state_before_noop,
        "snapshot source-style reopened post-noop clear reedit no-op save");
    check_workbook_editor_public_catalog_preserved(
        reopened_clear_reedit_editor,
        reopened_clear_reedit_catalog_before_noop,
        "snapshot source-style reopened post-noop clear reedit no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "snapshot source-style reopened post-noop clear reedit no-op save should leave the source workbook unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_clear_noop_output) ==
            reopened_post_noop_clear_noop_entries,
        "snapshot source-style reopened post-noop clear reedit no-op save should leave the prior no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(reopened_post_noop_clear_reedit_output) ==
            reopened_post_noop_clear_reedit_entries,
        "snapshot source-style reopened post-noop clear reedit no-op save should leave the re-edit output unchanged");
    const auto reopened_post_noop_clear_reedit_noop_entries =
        fastxlsx::test::read_zip_entries(reopened_post_noop_clear_reedit_noop_output);
    check(reopened_post_noop_clear_reedit_noop_entries == reopened_post_noop_clear_reedit_entries,
        "snapshot source-style reopened post-noop clear reedit no-op output should match the re-edit output");
    check_reopened_clean_sheet_output(reopened_post_noop_clear_reedit_noop_output, "Styled",
        "snapshot source-style reopened post-noop clear reedit no-op save",
        inspect_reopened_post_noop_clear_reedit_output);
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_snapshots_preserve_source_style_handles();
        std::cout << "WorkbookEditor public-state style snapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state style snapshot test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
