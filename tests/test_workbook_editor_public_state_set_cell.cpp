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

void test_public_worksheet_editor_set_cell_replacement_drops_source_style()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-full-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-full-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-full-style-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-cell-full-tail"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_cell("A1", fastxlsx::CellValue::text("full-replacement-unstyled"));
    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "full-replacement-unstyled" &&
            !live_a1.has_style(),
        "set_cell full replacement should drop the overwritten source style id");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Text &&
            live_b1.text_value() == "set-cell-full-tail" &&
            !live_b1.has_style(),
        "set_cell full replacement should keep non-target source cells unstyled");
    check(sheet.cell_count() == 2,
        "set_cell full replacement should keep represented sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
        "set_cell full replacement should keep row bounds");
    check(sheet.has_pending_changes(),
        "set_cell full replacement should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 2,
        "set_cell full replacement should keep aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_cell full replacement dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_cell full replacement should keep diagnostics clear");

    const auto check_unstyled_set_cell_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "full-replacement-unstyled" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A1");
        };
    const auto check_tail_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-cell-full-tail" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled tail B1");
        };
    const auto inspect_set_cell_replacement_output =
        [check_unstyled_set_cell_projection, check_tail_projection](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 2,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                prefix + " reopened output should keep row bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 2,
                prefix + " reopened sparse_cells should expose two records");
            if (cells.size() == 2) {
                check_unstyled_set_cell_projection(cells[0],
                    prefix + " reopened sparse_cells");
                check_tail_projection(cells[1],
                    prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose row-one records");
            if (row_one.size() == 2) {
                check_unstyled_set_cell_projection(row_one[0],
                    prefix + " reopened row_cells");
                check_tail_projection(row_one[1],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 1,
                prefix + " reopened column_cells should expose A1 only");
            if (column_one.size() == 1) {
                check_unstyled_set_cell_projection(column_one[0],
                    prefix + " reopened column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " reopened column_cells should expose B1 only");
            if (column_two.size() == 1) {
                check_tail_projection(column_two[0],
                    prefix + " reopened column_cells");
            }

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "full-replacement-unstyled" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read replacement A1 without style");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "set-cell-full-tail" &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should keep tail B1 unstyled");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell full replacement save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_cell full replacement save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "set_cell full replacement should keep the projected dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>full-replacement-unstyled</t></is></c>)",
        "set_cell full replacement should persist A1 without a style id");
    check_contains(worksheet_xml, "set-cell-full-tail",
        "set_cell full replacement should preserve non-target cells");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_cell full replacement should not keep the old source style on A1");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "set_cell full replacement should omit the old source numeric value");
    check_reopened_clean_sheet_output(output, "Styled", "set_cell full replacement",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_cell_replacement_output(
                reopened_sheet, "set_cell full replacement");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_cell_replacement_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 2,
                prefix + " should keep the saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 2,
                prefix + " should expose two saved records");
            if (cells.size() == 2) {
                check_unstyled_set_cell_projection(cells[0],
                    prefix + " sparse_cells");
                check_tail_projection(cells[1],
                    prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " should keep row-one snapshots");
            if (row_one.size() == 2) {
                check_unstyled_set_cell_projection(row_one[0],
                    prefix + " row_cells");
                check_tail_projection(row_one[1],
                    prefix + " row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 1,
                prefix + " should expose column-one A1");
            if (column_one.size() == 1) {
                check_unstyled_set_cell_projection(column_one[0],
                    prefix + " column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " should expose column-two B1");
            if (column_two.size() == 1) {
                check_tail_projection(column_two[0],
                    prefix + " column_cells");
            }

            check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
                prefix + " should keep saved bounds");
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized handle clean");
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
        };
    check_set_cell_replacement_saved_snapshot(
        pending_count_after_save, "set_cell full replacement saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell full replacement no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell full replacement no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell full replacement no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell full replacement no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell full replacement no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell full replacement no-op save should keep diagnostics clear");
    check_set_cell_replacement_saved_snapshot(
        pending_count_after_save, "set_cell full replacement no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_cell full replacement no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_cell full replacement no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_cell full replacement no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell full replacement no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_cell full replacement no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_cell_replacement_output(
                reopened_sheet, "set_cell full replacement no-op save");
        });
}

void test_public_worksheet_editor_set_cell_accepts_default_style_id_as_unstyled()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-default-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-default-style-post-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("default-style-tail"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml, R"(<c r="A1" s="1">)",
        "default StyleId source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_cell(
        "A1",
        fastxlsx::CellValue::text("default-style-normalized")
            .with_style(fastxlsx::StyleId {}));

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "default-style-normalized" &&
            !live_a1.has_style(),
        "set_cell explicit default StyleId should normalize replacement A1 to unstyled");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Text &&
            live_b1.text_value() == "default-style-tail" &&
            !live_b1.has_style(),
        "set_cell explicit default StyleId should keep tail B1 unstyled");
    check(sheet.cell_count() == 2,
        "set_cell explicit default StyleId should keep represented sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
        "set_cell explicit default StyleId should keep row bounds");
    check(sheet.has_pending_changes(),
        "set_cell explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 2,
        "set_cell explicit default StyleId should keep aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_cell explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_cell explicit default StyleId should keep diagnostics clear");

    const auto check_unstyled_default_style_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "default-style-normalized" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A1");
        };
    const auto check_default_style_tail_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "default-style-tail" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled tail B1");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("B1"),
        "set_cell explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("A2") && !sheet.contains_cell("D4"),
        "set_cell explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one =
        sheet.row_cells(1);
    check(live_row_one.size() == 2,
        "set_cell explicit default StyleId row_cells should expose row-one records");
    if (live_row_one.size() == 2) {
        check_unstyled_default_style_projection(live_row_one[0],
            "set_cell explicit default StyleId row_cells");
        check_default_style_tail_projection(live_row_one[1],
            "set_cell explicit default StyleId row_cells");
    }
    check(sheet.row_cells(2).empty(),
        "set_cell explicit default StyleId row_cells should keep row two empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one =
        sheet.column_cells(1);
    check(live_column_one.size() == 1,
        "set_cell explicit default StyleId column_cells should expose A1 only");
    if (live_column_one.size() == 1) {
        check_unstyled_default_style_projection(live_column_one[0],
            "set_cell explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two =
        sheet.column_cells(2);
    check(live_column_two.size() == 1,
        "set_cell explicit default StyleId column_cells should expose B1 only");
    if (live_column_two.size() == 1) {
        check_default_style_tail_projection(live_column_two[0],
            "set_cell explicit default StyleId column_cells");
    }
    check(sheet.column_cells(3).empty(),
        "set_cell explicit default StyleId column_cells should keep column three empty");

    const auto inspect_default_style_output =
        [check_unstyled_default_style_projection,
            check_default_style_tail_projection](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 2,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                prefix + " reopened output should keep row bounds");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("B1"),
                prefix + " reopened contains_cell should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("A2") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened contains_cell should keep missing cells absent");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 2,
                prefix + " reopened sparse_cells should expose two records");
            if (cells.size() == 2) {
                check_unstyled_default_style_projection(cells[0],
                    prefix + " reopened sparse_cells");
                check_default_style_tail_projection(cells[1],
                    prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose row-one records");
            if (row_one.size() == 2) {
                check_unstyled_default_style_projection(row_one[0],
                    prefix + " reopened row_cells");
                check_default_style_tail_projection(row_one[1],
                    prefix + " reopened row_cells");
            }
            check(reopened_sheet.row_cells(2).empty(),
                prefix + " reopened row_cells should keep row two empty");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 1,
                prefix + " reopened column_cells should expose A1 only");
            if (column_one.size() == 1) {
                check_unstyled_default_style_projection(column_one[0],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " reopened column_cells should expose B1 only");
            if (column_two.size() == 1) {
                check_default_style_tail_projection(column_two[0],
                    prefix + " reopened column_cells");
            }
            check(reopened_sheet.column_cells(3).empty(),
                prefix + " reopened column_cells should keep column three empty");

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "default-style-normalized" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read A1 without a style handle");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_cell explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "set_cell explicit default StyleId should keep the projected dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>default-style-normalized</t></is></c>)",
        "set_cell explicit default StyleId should persist A1 without a style id");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_cell explicit default StyleId should not keep the old source style on A1");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_cell explicit default StyleId should not write default style ids");
    check_reopened_clean_sheet_output(output, "Styled", "set_cell explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_output(
                reopened_sheet, "set_cell explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_cell explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_cell explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_cell explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_cell explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_output(
                reopened_sheet, "set_cell explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_cell explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_cell explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_cell explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_cell explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_cell explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_cell explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_output(
                reopened_sheet,
                "set_cell explicit default StyleId second no-op save");
        });

    sheet.set_cell(
        "A1",
        fastxlsx::CellValue::formula("B1").with_style(fastxlsx::StyleId {}));
    const fastxlsx::CellValue post_noop_live_a1 = sheet.get_cell("A1");
    check(post_noop_live_a1.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_live_a1.text_value() == "B1" &&
            !post_noop_live_a1.has_style(),
        "set_cell explicit default StyleId post-noop edit should keep A1 formula unstyled");
    check(sheet.cell_count() == 2,
        "set_cell explicit default StyleId post-noop edit should keep sparse count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
        "set_cell explicit default StyleId post-noop edit should keep row bounds");
    check(sheet.has_pending_changes(),
        "set_cell explicit default StyleId post-noop edit should dirty the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell explicit default StyleId post-noop edit should not record a handoff before save");
    check(editor.pending_materialized_cell_count() == 2,
        "set_cell explicit default StyleId post-noop edit should expose dirty sparse count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_cell explicit default StyleId post-noop edit dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_cell explicit default StyleId post-noop edit should keep diagnostics clear");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell explicit default StyleId post-noop save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_cell explicit default StyleId post-noop save should record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell explicit default StyleId post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell explicit default StyleId post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell explicit default StyleId post-noop save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell explicit default StyleId post-noop save should keep diagnostics clear");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell explicit default StyleId post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_cell explicit default StyleId post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_cell explicit default StyleId post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "set_cell explicit default StyleId post-noop save should leave the second no-op output unchanged");
    check(post_noop_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_cell explicit default StyleId post-noop save should preserve source styles.xml bytes");

    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "set_cell explicit default StyleId post-noop save should keep the projected dimension");
    check_contains(post_noop_worksheet_xml, R"(<c r="A1"><f>B1</f></c>)",
        "set_cell explicit default StyleId post-noop save should persist A1 formula without a style id");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>default-style-tail</t></is></c>)",
        "set_cell explicit default StyleId post-noop save should keep B1 unstyled");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A1" s=")",
        "set_cell explicit default StyleId post-noop save should not revive the old source style on A1");
    check_not_contains(post_noop_worksheet_xml, R"(s="0")",
        "set_cell explicit default StyleId post-noop save should not write default style ids");
    check_not_contains(post_noop_worksheet_xml, "default-style-normalized",
        "set_cell explicit default StyleId post-noop save should replace the earlier A1 text");

    check_reopened_clean_sheet_output(
        post_noop_output, "Styled",
        "set_cell explicit default StyleId post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 2,
                "set_cell explicit default StyleId post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                "set_cell explicit default StyleId post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "set_cell explicit default StyleId post-noop reopened row_cells should expose row one");
            if (row_one.size() == 2) {
                check(row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        row_one[0].value.text_value() == "B1" &&
                        !row_one[0].value.has_style(),
                    "set_cell explicit default StyleId post-noop reopened row_cells should expose unstyled A1 formula");
                check_default_style_tail_projection(row_one[1],
                    "set_cell explicit default StyleId post-noop reopened row_cells");
            }
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a1.text_value() == "B1" &&
                    !reopened_a1.has_style(),
                "set_cell explicit default StyleId post-noop reopened output should read A1 formula without a style handle");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "default-style-tail" &&
                    !reopened_b1.has_style(),
                "set_cell explicit default StyleId post-noop reopened output should keep B1 unstyled");
        });
}

void test_public_worksheet_editor_set_cell_value_accepts_default_style_id_as_style_preserving_value_edit()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-default-style-second-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("value-only-default-tail"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("value-only-default-a2"),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml, R"(<c r="A1" s="1">)",
        "set_cell_value explicit default StyleId source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_cell_value(
        "A1",
        fastxlsx::CellValue::number(2.5).with_style(fastxlsx::StyleId {}));
    sheet.set_cell_value(
        1, 2, fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}));
    sheet.set_cell_value(
        2, 3, fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {}));

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Number &&
            live_a1.number_value() == 2.5 &&
            live_a1.has_style() &&
            live_a1.style_id().value() == non_default_style.value(),
        "set_cell_value explicit default StyleId should preserve source style on A1");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Blank &&
            !live_b1.has_style(),
        "set_cell_value explicit default StyleId should keep unstyled B1 blank unstyled");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Text &&
            live_a2.text_value() == "value-only-default-a2" &&
            !live_a2.has_style(),
        "set_cell_value explicit default StyleId should keep untouched A2 unstyled");
    const fastxlsx::CellValue live_c2 = sheet.get_cell("C2");
    check(live_c2.kind() == fastxlsx::CellValueKind::Formula &&
            live_c2.text_value() == "A1+B1" &&
            !live_c2.has_style(),
        "set_cell_value explicit default StyleId should insert missing C2 without a style");

    const auto check_value_default_a1_snapshot =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 2.5 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve source-styled A1");
        };
    const auto check_value_default_b1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 blank");
        };
    const auto check_value_default_a2_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "value-only-default-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 text");
        };
    const auto check_value_default_c2_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled C2 formula");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("B1") &&
            sheet.contains_cell("A2") && sheet.contains_cell("C2"),
        "set_cell_value explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("B2") && !sheet.contains_cell("D4"),
        "set_cell_value explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one =
        sheet.row_cells(1);
    check(live_row_one.size() == 2,
        "set_cell_value explicit default StyleId row_cells should expose row one");
    if (live_row_one.size() == 2) {
        check_value_default_a1_snapshot(live_row_one[0],
            "set_cell_value explicit default StyleId row_cells");
        check_value_default_b1_snapshot(live_row_one[1],
            "set_cell_value explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two =
        sheet.row_cells(2);
    check(live_row_two.size() == 2,
        "set_cell_value explicit default StyleId row_cells should expose sparse row two");
    if (live_row_two.size() == 2) {
        check_value_default_a2_snapshot(live_row_two[0],
            "set_cell_value explicit default StyleId row_cells");
        check_value_default_c2_snapshot(live_row_two[1],
            "set_cell_value explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one =
        sheet.column_cells(1);
    check(live_column_one.size() == 2,
        "set_cell_value explicit default StyleId column_cells should expose column one");
    if (live_column_one.size() == 2) {
        check_value_default_a1_snapshot(live_column_one[0],
            "set_cell_value explicit default StyleId column_cells");
        check_value_default_a2_snapshot(live_column_one[1],
            "set_cell_value explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two =
        sheet.column_cells(2);
    check(live_column_two.size() == 1,
        "set_cell_value explicit default StyleId column_cells should expose B1 only");
    if (live_column_two.size() == 1) {
        check_value_default_b1_snapshot(live_column_two[0],
            "set_cell_value explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_three =
        sheet.column_cells(3);
    check(live_column_three.size() == 1,
        "set_cell_value explicit default StyleId column_cells should expose C2 only");
    if (live_column_three.size() == 1) {
        check_value_default_c2_snapshot(live_column_three[0],
            "set_cell_value explicit default StyleId column_cells");
    }
    check(sheet.row_cells(3).empty() && sheet.column_cells(4).empty(),
        "set_cell_value explicit default StyleId sparse views should keep gaps empty");
    check(sheet.cell_count() == 4,
        "set_cell_value explicit default StyleId should keep source plus inserted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "set_cell_value explicit default StyleId should extend bounds to C2");
    check(sheet.has_pending_changes(),
        "set_cell_value explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 4,
        "set_cell_value explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_cell_value explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_cell_value explicit default StyleId should keep diagnostics clear");

    const auto inspect_default_style_value_only_output =
        [non_default_style, check_value_default_a1_snapshot,
            check_value_default_b1_snapshot, check_value_default_a2_snapshot,
            check_value_default_c2_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet,
            std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 4,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                prefix + " reopened output should keep bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 2.5 &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve source style on A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Blank &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should keep B1 blank unstyled");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "value-only-default-a2" &&
                    !reopened_a2.has_style(),
                prefix + " reopened output should keep A2 unstyled");
            const fastxlsx::CellValue reopened_c2 = reopened_sheet.get_cell("C2");
            check(reopened_c2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2.text_value() == "A1+B1" &&
                    !reopened_c2.has_style(),
                prefix + " reopened output should keep inserted C2 unstyled");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("B1") &&
                    reopened_sheet.contains_cell("A2") &&
                    reopened_sheet.contains_cell("C2"),
                prefix + " reopened output should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("B2") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose row one");
            if (row_one.size() == 2) {
                check_value_default_a1_snapshot(row_one[0],
                    prefix + " reopened row_cells");
                check_value_default_b1_snapshot(row_one[1],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " reopened row_cells should expose sparse row two");
            if (row_two.size() == 2) {
                check_value_default_a2_snapshot(row_two[0],
                    prefix + " reopened row_cells");
                check_value_default_c2_snapshot(row_two[1],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " reopened column_cells should expose column one");
            if (column_one.size() == 2) {
                check_value_default_a1_snapshot(column_one[0],
                    prefix + " reopened column_cells");
                check_value_default_a2_snapshot(column_one[1],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " reopened column_cells should expose B1 only");
            if (column_two.size() == 1) {
                check_value_default_b1_snapshot(column_two[0],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " reopened column_cells should expose C2 only");
            if (column_three.size() == 1) {
                check_value_default_c2_snapshot(column_three[0],
                    prefix + " reopened column_cells");
            }
            check(reopened_sheet.row_cells(3).empty() &&
                    reopened_sheet.column_cells(4).empty(),
                prefix + " reopened sparse views should keep gaps empty");
        };

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "set_cell_value explicit default StyleId save should clean the materialized worksheet");
    check(editor.pending_change_count() == 1,
        "set_cell_value explicit default StyleId save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell_value explicit default StyleId save should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell_value explicit default StyleId save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell_value explicit default StyleId save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell_value explicit default StyleId save should keep diagnostics clear");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell_value explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_cell_value explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_a1 =
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "set_cell_value explicit default StyleId should project extended bounds");
    check_contains(worksheet_xml, styled_a1,
        "set_cell_value explicit default StyleId should persist source-styled A1");
    check_contains(worksheet_xml, R"(<c r="B1"/>)",
        "set_cell_value explicit default StyleId should persist B1 blank without style");
    check_contains(worksheet_xml, "value-only-default-a2",
        "set_cell_value explicit default StyleId should keep untouched A2 text");
    check_contains(worksheet_xml, R"(<c r="C2"><f>A1+B1</f></c>)",
        "set_cell_value explicit default StyleId should persist C2 formula without style");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "set_cell_value explicit default StyleId should not write a style id on B1");
    check_not_contains(worksheet_xml, R"(<c r="C2" s=")",
        "set_cell_value explicit default StyleId should not write a style id on C2");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_cell_value explicit default StyleId should not write default style ids");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_cell_value explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_value_only_output(
                reopened_sheet, "set_cell_value explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell_value explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell_value explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell_value explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell_value explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell_value explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell_value explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_cell_value explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_cell_value explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_cell_value explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell_value explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_cell_value explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_value_only_output(
                reopened_sheet, "set_cell_value explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell_value explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cell_value explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell_value explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell_value explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cell_value explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell_value explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_cell_value explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_cell_value explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_cell_value explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_cell_value explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_cell_value explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell_value explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_cell_value explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_value_only_output(
                reopened_sheet,
                "set_cell_value explicit default StyleId second no-op save");
        });
}

} // namespace

void check_reopened_default_data_overwrite_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    std::string_view expected_a1_text)
{
    const std::string prefix(scenario);
    const std::string expected_a1(expected_a1_text);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix, expected_a1](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == expected_a1,
                prefix + " reopened output should read overwritten A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should keep source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should keep source-backed A2");
            check(!reopened_sheet.try_cell("D4").has_value(),
                prefix + " reopened output should keep rejected D4 absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_cells =
                reopened_sheet.sparse_cells();
            check(reopened_cells.size() == 3,
                prefix + " reopened sparse_cells should expose all represented cells");
            if (reopened_cells.size() == 3) {
                check(reopened_cells[0].reference.row == 1 &&
                        reopened_cells[0].reference.column == 1 &&
                        reopened_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened sparse_cells should expose overwritten A1 first");
                check(reopened_cells[1].reference.row == 1 &&
                        reopened_cells[1].reference.column == 2 &&
                        reopened_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_cells[1].value.number_value() == 1.0,
                    prefix + " reopened sparse_cells should expose source B1 second");
                check(reopened_cells[2].reference.row == 2 &&
                        reopened_cells[2].reference.column == 1 &&
                        reopened_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened sparse_cells should expose source A2 last");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_range_cells =
                reopened_sheet.sparse_cells("A1:D4");
            check(reopened_range_cells.size() == 3,
                prefix + " reopened range sparse_cells should expose all represented cells");
            if (reopened_range_cells.size() == 3) {
                check(reopened_range_cells[0].reference.row == 1 &&
                        reopened_range_cells[0].reference.column == 1 &&
                        reopened_range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[0].value.text_value() == expected_a1,
                    prefix + " reopened range sparse_cells should expose overwritten A1 first");
                check(reopened_range_cells[1].reference.row == 1 &&
                        reopened_range_cells[1].reference.column == 2 &&
                        reopened_range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_range_cells[1].value.number_value() == 1.0,
                    prefix + " reopened range sparse_cells should expose source B1 second");
                check(reopened_range_cells[2].reference.row == 2 &&
                        reopened_range_cells[2].reference.column == 1 &&
                        reopened_range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_range_cells[2].value.text_value() == "placeholder-a2",
                    prefix + " reopened range sparse_cells should expose source A2 last");
            }
            const std::array<fastxlsx::WorksheetCellReference, 6> reopened_requested_refs {
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {4, 4},
                fastxlsx::WorksheetCellReference {1, 2},
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 3},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_requested_cells =
                reopened_sheet.sparse_cells(reopened_requested_refs);
            check(reopened_requested_cells.size() == 4,
                prefix + " reopened requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
            if (reopened_requested_cells.size() == 4) {
                check(reopened_requested_cells[0].reference.row == 2 &&
                        reopened_requested_cells[0].reference.column == 1 &&
                        reopened_requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[0].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should keep A2 first");
                check(reopened_requested_cells[1].reference.row == 1 &&
                        reopened_requested_cells[1].reference.column == 2 &&
                        reopened_requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_requested_cells[1].value.number_value() == 1.0,
                    prefix + " reopened requested sparse_cells should keep B1 after skipped D4");
                check(reopened_requested_cells[2].reference.row == 1 &&
                        reopened_requested_cells[2].reference.column == 1 &&
                        reopened_requested_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[2].value.text_value() == expected_a1,
                    prefix + " reopened requested sparse_cells should keep overwritten A1 in requested order");
                check(reopened_requested_cells[3].reference.row == 2 &&
                        reopened_requested_cells[3].reference.column == 1 &&
                        reopened_requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_requested_cells[3].value.text_value() == "placeholder-a2",
                    prefix + " reopened requested sparse_cells should preserve duplicate A2");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 2 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == expected_a1 &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_row_one[1].value.number_value() == 1.0,
                prefix + " reopened row_cells should expose overwritten A1 and source B1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 1 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "placeholder-a2",
                prefix + " reopened row_cells should expose source A2");
            check(reopened_sheet.row_cells(3).empty(),
                prefix + " reopened row_cells should keep the gap row empty");
            check(reopened_sheet.row_cells(4).empty(),
                prefix + " reopened row_cells should keep rejected D4 row empty");

            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
                reopened_sheet.column_cells(1);
            check(reopened_column_one.size() == 2 &&
                    reopened_column_one[0].reference.row == 1 &&
                    reopened_column_one[0].reference.column == 1 &&
                    reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[0].value.text_value() == expected_a1 &&
                    reopened_column_one[1].reference.row == 2 &&
                    reopened_column_one[1].reference.column == 1 &&
                    reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[1].value.text_value() == "placeholder-a2",
                prefix + " reopened column_cells should expose overwritten A1 and source A2");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
                reopened_sheet.column_cells(2);
            check(reopened_column_two.size() == 1 &&
                    reopened_column_two[0].reference.row == 1 &&
                    reopened_column_two[0].reference.column == 2 &&
                    reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_column_two[0].value.number_value() == 1.0,
                prefix + " reopened column_cells should expose source B1");
            check(reopened_sheet.column_cells(3).empty(),
                prefix + " reopened column_cells should keep the gap column empty");
            check(reopened_sheet.column_cells(4).empty(),
                prefix + " reopened column_cells should keep rejected D4 absent");
        });
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

void test_public_worksheet_editor_set_cell_value_style_rejection_noop_save()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-style-reject-source.xlsx");
    const std::filesystem::path style_id_source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-style-id-source.xlsx");
    const std::filesystem::path row_column_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-style-reject-output.xlsx");
    const std::filesystem::path row_column_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-style-reject-noop-output.xlsx");
    const std::filesystem::path a1_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-style-reject-output.xlsx");
    const std::filesystem::path a1_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-style-reject-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_id_source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("StyleIds");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    auto check_single_value_style_rejection_noop =
        [&](std::string_view scenario, const std::filesystem::path& output,
            const std::filesystem::path& noop_output,
            const std::function<void(fastxlsx::WorksheetEditor&)>& reject_action) {
            const std::string prefix(scenario);
            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

            bool failed = false;
            try {
                reject_action(sheet);
            } catch (const fastxlsx::FastXlsxError& error) {
                failed = true;
                check_contains(error.what(), "StyleId",
                    prefix + " should expose the unsupported StyleId boundary");
            }
            check(failed,
                prefix + " should reject caller-supplied non-default StyleId values");
            check(editor.last_edit_error().has_value() &&
                    editor.last_edit_error()->find("set_cell_value()") != std::string::npos,
                prefix + " should retain the public edit diagnostic");
            check_workbook_editor_public_no_pending_state(editor, prefix);
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized sheet clean");
            check(sheet.get_cell("A1").text_value() == "placeholder-a1",
                prefix + " should preserve source A1");
            check(sheet.get_cell("B1").number_value() == 1.0,
                prefix + " should preserve source B1");
            check(sheet.get_cell("A2").text_value() == "placeholder-a2",
                prefix + " should preserve source A2");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " should not queue replacement diagnostics");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
                workbook_editor_public_catalog_snapshot(editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
                workbook_editor_public_save_state_snapshot(editor);
            editor.save_as(output);
            check_workbook_editor_public_save_state_preserved(
                editor, save_state_before_save, prefix + " save");
            check_workbook_editor_public_catalog_preserved(
                editor, catalog_before_save, prefix + " save");
            check_workbook_editor_public_no_pending_state(editor, prefix + " save");
            check(!sheet.has_pending_changes(),
                prefix + " save should keep the materialized sheet clean");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " save should not queue replacement diagnostics");
            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            check(output_entries == source_entries,
                prefix + " save should copy source entries");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(output, prefix + " save");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
                workbook_editor_public_catalog_snapshot(editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
                workbook_editor_public_save_state_snapshot(editor);
            editor.save_as(noop_output);
            check_workbook_editor_public_save_state_preserved(
                editor, save_state_before_noop, prefix + " noop save");
            check_workbook_editor_public_catalog_preserved(
                editor, catalog_before_noop, prefix + " noop save");
            check_workbook_editor_public_no_pending_state(editor, prefix + " noop save");
            check(!sheet.has_pending_changes(),
                prefix + " noop save should keep the materialized sheet clean");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " noop save should not queue replacement diagnostics");
            const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
            check(noop_entries == source_entries,
                prefix + " noop save should still copy source entries");
            check(noop_entries == output_entries,
                prefix + " noop output should match the first output");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " noop save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(noop_output, prefix + " noop save");
        };

    check_single_value_style_rejection_noop(
        "set_cell_value row-column style rejection", row_column_output,
        row_column_noop_output,
        [non_default_style](fastxlsx::WorksheetEditor& sheet) {
            sheet.set_cell_value(
                1, 1,
                fastxlsx::CellValue::text("single-value-styled-rejected")
                    .with_style(non_default_style));
        });

    check_single_value_style_rejection_noop(
        "set_cell_value A1 style rejection", a1_output, a1_noop_output,
        [non_default_style](fastxlsx::WorksheetEditor& sheet) {
            sheet.set_cell_value(
                "B1", fastxlsx::CellValue::number(99.0).with_style(non_default_style));
        });
}

void test_public_worksheet_editor_set_cell_value_validation_and_max_cells_noop_save()
{
    auto check_rejected_single_value_noop =
        [](std::string_view scenario, const std::filesystem::path& source,
            const std::filesystem::path& output, const std::filesystem::path& noop_output,
            const std::function<fastxlsx::WorksheetEditor(fastxlsx::WorkbookEditor&)>&
                open_sheet,
            const std::function<void(fastxlsx::WorksheetEditor&)>& reject_action,
            std::string_view expected_diagnostic) {
            const std::string prefix(scenario);
            const auto source_entries = fastxlsx::test::read_zip_entries(source);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = open_sheet(editor);
            const std::size_t baseline_memory = sheet.estimated_memory_usage();

            bool failed = false;
            try {
                reject_action(sheet);
            } catch (const fastxlsx::FastXlsxError& error) {
                failed = true;
                check_contains(error.what(), expected_diagnostic,
                    prefix + " should expose the expected diagnostic");
            }
            check(failed, prefix + " should reject the invalid value edit");
            check(editor.last_edit_error().has_value(),
                prefix + " should retain the public edit diagnostic");
            check(!sheet.has_pending_changes(),
                prefix + " should not dirty the materialized sheet");
            check(sheet.cell_count() == 3,
                prefix + " should preserve sparse cell count");
            check(sheet.estimated_memory_usage() == baseline_memory,
                prefix + " should preserve sparse memory estimate");
            check(sheet.get_cell("A1").text_value() == "placeholder-a1",
                prefix + " should preserve source A1");
            check(sheet.get_cell("B1").number_value() == 1.0,
                prefix + " should preserve source B1");
            check(sheet.get_cell("A2").text_value() == "placeholder-a2",
                prefix + " should preserve source A2");
            check(!sheet.try_cell("C3").has_value(),
                prefix + " should keep rejected or unreachable targets absent");
            check_workbook_editor_public_no_pending_state(editor, prefix);
            check(editor.pending_materialized_worksheet_names().empty() &&
                    editor.pending_materialized_cell_count() == 0 &&
                    editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " should not expose dirty materialized diagnostics");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " should not queue replacement diagnostics");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
                workbook_editor_public_catalog_snapshot(editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
                workbook_editor_public_save_state_snapshot(editor);
            editor.save_as(output);
            check_workbook_editor_public_save_state_preserved(
                editor, save_state_before_save, prefix + " save");
            check_workbook_editor_public_catalog_preserved(
                editor, catalog_before_save, prefix + " save");
            check_workbook_editor_public_no_pending_state(editor, prefix + " save");
            check(!sheet.has_pending_changes(),
                prefix + " save should keep the materialized sheet clean");
            check(editor.pending_materialized_worksheet_names().empty() &&
                    editor.pending_materialized_cell_count() == 0 &&
                    editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " save should keep dirty diagnostics clear");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " save should not queue replacement diagnostics");
            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            check(output_entries == source_entries,
                prefix + " save should copy source entries");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(output, prefix + " save");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
                workbook_editor_public_catalog_snapshot(editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
                workbook_editor_public_save_state_snapshot(editor);
            editor.save_as(noop_output);
            check_workbook_editor_public_save_state_preserved(
                editor, save_state_before_noop, prefix + " noop save");
            check_workbook_editor_public_catalog_preserved(
                editor, catalog_before_noop, prefix + " noop save");
            check_workbook_editor_public_no_pending_state(editor, prefix + " noop save");
            check(!sheet.has_pending_changes(),
                prefix + " noop save should keep the materialized sheet clean");
            check(editor.pending_materialized_worksheet_names().empty() &&
                    editor.pending_materialized_cell_count() == 0 &&
                    editor.estimated_pending_materialized_memory_usage() == 0,
                prefix + " noop save should keep dirty diagnostics clear");
            check_workbook_editor_no_replacement_diagnostics(
                editor, prefix + " noop save should not queue replacement diagnostics");
            const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
            check(noop_entries == source_entries,
                prefix + " noop save should still copy source entries");
            check(noop_entries == output_entries,
                prefix + " noop output should match the first output");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " noop save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(noop_output, prefix + " noop save");
        };

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-invalid-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-invalid-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-invalid-noop-output.xlsx");

        check_rejected_single_value_noop(
            "set_cell_value invalid coordinate", source, output, noop_output,
            [](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data");
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    0, 1, fastxlsx::CellValue::text("set-cell-value-invalid-row"));
            },
            "invalid");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-column-overflow-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-column-overflow-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-column-overflow-noop-output.xlsx");

        check_rejected_single_value_noop(
            "set_cell_value column overflow", source, output, noop_output,
            [](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data");
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    1, 16385,
                    fastxlsx::CellValue::text("set-cell-value-column-overflow"));
            },
            "WorksheetEditor cell coordinate is invalid");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-invalid-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-invalid-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-invalid-noop-output.xlsx");

        check_rejected_single_value_noop(
            "set_cell_value A1 invalid reference", source, output, noop_output,
            [](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data");
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    "a1", fastxlsx::CellValue::text("set-cell-value-a1-invalid"));
            },
            "invalid");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-overflow-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-overflow-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-overflow-noop-output.xlsx");

        check_rejected_single_value_noop(
            "set_cell_value A1 column overflow", source, output, noop_output,
            [](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data");
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    "XFE1", fastxlsx::CellValue::text("set-cell-value-a1-overflow"));
            },
            "WorksheetEditor cell reference column exceeds Excel limits");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-max-reject-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-max-reject-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-max-reject-noop-output.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        check_rejected_single_value_noop(
            "set_cell_value max_cells rejection", source, output, noop_output,
            [options](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data", options);
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    3, 3, fastxlsx::CellValue::text("set-cell-value-max-rejected"));
            },
            "CellStore max_cells guardrail exceeded");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-max-reject-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-max-reject-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-cell-value-a1-max-reject-noop-output.xlsx");

        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        check_rejected_single_value_noop(
            "set_cell_value A1 max_cells rejection", source, output, noop_output,
            [options](fastxlsx::WorkbookEditor& editor) {
                return editor.worksheet("Data", options);
            },
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell_value(
                    "C3", fastxlsx::CellValue::text("set-cell-value-a1-max-rejected"));
            },
            "CellStore max_cells guardrail exceeded");
    }
}

void test_public_worksheet_editor_set_cell_value_memory_budget_failure_preserves_state()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-memory-source.xlsx");
    const std::filesystem::path failure_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-memory-failure-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-memory-recovery-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-value-memory-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();
    const std::size_t baseline_count = sizing_sheet.cell_count();

    fastxlsx::WorksheetEditorOptions options;
    options.memory_budget_bytes = exact_memory_budget;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    const std::string rejected_text =
        std::string("set-cell-value-memory-rejected-") + std::string(512, 'x');

    bool failed = false;
    try {
        sheet.set_cell_value(4, 4, fastxlsx::CellValue::text(rejected_text));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "set_cell_value should expose memory budget diagnostics");
    }
    check(failed,
        "set_cell_value should reject a missing-cell write that exceeds memory_budget_bytes");
    check(editor.last_edit_error().has_value(),
        "failed set_cell_value memory-budget mutation should update last_edit_error");
    check_contains(*editor.last_edit_error(), "CellStore memory_budget_bytes guardrail exceeded",
        "last_edit_error should retain the set_cell_value memory-budget diagnostic");
    check(!sheet.has_pending_changes(),
        "failed set_cell_value memory-budget mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed set_cell_value memory-budget mutation should not dirty the editor");
    check_workbook_editor_public_no_pending_state(
        editor, "failed set_cell_value memory-budget mutation");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed set_cell_value memory-budget mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed set_cell_value memory-budget mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed set_cell_value memory-budget mutation should not expose dirty materialized memory");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "failed set_cell_value memory-budget mutation");
    check(sheet.cell_count() == baseline_count,
        "failed set_cell_value memory-budget mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed set_cell_value memory-budget mutation should preserve sparse memory estimate");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "failed set_cell_value memory-budget mutation should preserve source A1");
    check(sheet.get_cell("B1").number_value() == 1.0,
        "failed set_cell_value memory-budget mutation should preserve source B1");
    check(sheet.get_cell("A2").text_value() == "placeholder-a2",
        "failed set_cell_value memory-budget mutation should preserve source A2");
    check(!sheet.try_cell("D4").has_value(),
        "failed set_cell_value memory-budget mutation should keep rejected target absent");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed set_cell_value memory-budget mutation should leave the source package unchanged");
    const std::optional<std::string> memory_error = editor.last_edit_error();

    editor.save_as(failure_output);
    check(editor.last_edit_error() == memory_error,
        "save_as after failed set_cell_value memory-budget mutation should preserve diagnostics");
    const auto failure_entries = fastxlsx::test::read_zip_entries(failure_output);
    check(failure_entries == source_entries,
        "save_as after failed set_cell_value memory-budget mutation should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "save_as after failed set_cell_value memory-budget mutation should leave the source package unchanged");
    check_reopened_default_data_sheet_output(
        failure_output, "set_cell_value memory-budget failure");

    sheet.set_cell_value("A1", fastxlsx::CellValue::text("value-mb-ok"));
    check(!editor.last_edit_error().has_value(),
        "successful set_cell_value after memory-budget failure should clear diagnostics");
    check(sheet.has_pending_changes(),
        "successful set_cell_value after memory-budget failure should dirty the materialized session");
    check(sheet.cell_count() == baseline_count,
        "successful set_cell_value recovery should preserve sparse cell count");
    check(sheet.estimated_memory_usage() <= exact_memory_budget,
        "successful set_cell_value recovery should stay within the configured memory budget");
    check(sheet.get_cell("A1").text_value() == "value-mb-ok",
        "successful set_cell_value recovery should apply the in-budget value-only payload");
    check(!sheet.try_cell("D4").has_value(),
        "successful set_cell_value recovery should keep the rejected target absent");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "set_cell_value memory-budget recovery dirty summary");

    editor.save_as(recovery_output);
    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell_value memory-budget recovery save should leave the source package unchanged");
    const std::string worksheet_xml = recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "value-mb-ok",
        "set_cell_value memory-budget recovery save should persist the recovery value");
    check_not_contains(worksheet_xml, "set-cell-value-memory-rejected",
        "set_cell_value memory-budget recovery save should omit rejected payloads");
    check_contains(recovery_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_cell_value memory-budget recovery save should preserve untouched worksheets");
    check_reopened_default_data_overwrite_output(
        recovery_output, "set_cell_value memory-budget recovery", "value-mb-ok");

    const std::size_t pending_count_after_recovery_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_cell_value memory-budget recovery no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "set_cell_value memory-budget recovery no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cell_value memory-budget recovery no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cell_value memory-budget recovery no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_cell_value memory-budget recovery no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cell_value memory-budget recovery no-op save should keep diagnostics clear");
    check_saved_default_data_overwrite_snapshots(
        editor, sheet, pending_count_after_recovery_save,
        "set_cell_value memory-budget recovery saved handle", "value-mb-ok");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_cell_value memory-budget recovery no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_cell_value memory-budget recovery no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == recovery_entries,
        "set_cell_value memory-budget recovery no-op output should match recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cell_value memory-budget recovery no-op save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(
        noop_output, "set_cell_value memory-budget recovery no-op save", "value-mb-ok");
}


void test_public_worksheet_editor_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({
            fastxlsx::CellView::number(1.5).with_style(source_style),
            fastxlsx::CellView::text("dirty-style-b1"),
        });
        data.append_row({
            fastxlsx::CellView::text("dirty-style-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="A1" s="1">)",
        "dirty style rejection source fixture should start with a styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const auto check_dirty_a1 =
        [source_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 1.5 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == source_style.value(),
                prefix + " should expose styled source A1");
        };
    const auto check_dirty_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "dirty-style-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose the preserved dirty B1 value");
        };
    const auto check_dirty_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "dirty-style-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose source A2");
        };
    const auto check_dirty_recovered_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "dirty-style-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered dirty A2");
        };
    const auto check_dirty_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 3,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented used range");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("B2"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 3,
                prefix + " sparse_cells should expose preserved records");
            if (cells.size() == 3) {
                check_dirty_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_dirty_a2(cells[2], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_dirty_a1(row_one[0], prefix + " row_cells");
                check_dirty_b1(row_one[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_dirty_a1(column_one[0], prefix + " column_cells");
                check_dirty_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.5 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve styled source A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "dirty-style-kept" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve dirty B1 without a style");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "dirty-style-a2" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve source A2");
        };
    const auto check_dirty_recovery_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 3,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented used range");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("B2"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 3,
                prefix + " sparse_cells should expose preserved records");
            if (cells.size() == 3) {
                check_dirty_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_dirty_recovered_a2(cells[2], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_dirty_a1(row_one[0], prefix + " row_cells");
                check_dirty_b1(row_one[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_dirty_a1(column_one[0], prefix + " column_cells");
                check_dirty_recovered_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.5 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve styled source A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "dirty-style-kept" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve dirty B1 without a style");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "dirty-style-recovered" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve recovered A2 without a style");
        };

    sheet.set_cell_value("B1",
        fastxlsx::CellValue::text("dirty-style-kept")
            .with_style(fastxlsx::StyleId {}));
    check(!editor.last_edit_error().has_value(),
        "dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", 0, "dirty style rejection setup");
    check_dirty_views(sheet, "dirty style rejection setup");

    bool failed = false;
    try {
        sheet.set_cell("C1",
            fastxlsx::CellValue::text("dirty-style-rejected")
                .with_style(source_style));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", 0, "dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-style-kept",
        "dirty style rejection save should persist the prior dirty value");
    check_not_contains(worksheet_xml, "dirty-style-rejected",
        "dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml, R"(<c r="A1" s="1">)",
        "dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "dirty style rejection save should keep dirty B1 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Data", "dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Data", "dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "dirty style rejection noop save");
        });

    sheet.set_cell("A2",
        fastxlsx::CellValue::text("dirty-style-recovered")
            .with_style(fastxlsx::StyleId {}));
    check(!editor.last_edit_error().has_value(),
        "dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Data", pending_count_after_save,
        "dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty style rejection recovery should not queue replacement diagnostics");
    check_dirty_recovery_views(sheet, "dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save, "dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "dirty style rejection recovery save should not queue replacement diagnostics");
    check_dirty_recovery_views(sheet, "dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, "dirty-style-kept",
        "dirty style rejection recovery save should persist dirty B1");
    check_contains(recovery_worksheet_xml, "dirty-style-recovered",
        "dirty style rejection recovery save should persist recovered A2");
    check_not_contains(recovery_worksheet_xml, "dirty-style-a2",
        "dirty style rejection recovery save should replace old source A2 text");
    check_not_contains(recovery_worksheet_xml, "dirty-style-rejected",
        "dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml, R"(<c r="A1" s="1">)",
        "dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B1" s=")",
        "dirty style rejection recovery save should keep dirty B1 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="A2" s=")",
        "dirty style rejection recovery save should keep recovered A2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Data", "dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_recovery_views(
                reopened_sheet, "dirty style rejection recovery save");
        });

    const std::size_t pending_count_after_recovery_save =
        editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_recovery_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(recovery_noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_recovery_noop,
        "dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_dirty_recovery_views(
        sheet, "dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "dirty style rejection recovery noop output should match the recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Data",
        "dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_recovery_views(
                reopened_sheet, "dirty style rejection recovery noop save");
        });
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

void check_blank_guardrail_overwrite_handle_snapshots(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_pending_count,
    std::string_view scenario,
    std::optional<std::string_view> expected_a1_text)
{
    const std::string prefix(scenario);

    const auto check_expected_a1_value =
        [&](const fastxlsx::CellValue& value, std::string_view label) {
            if (expected_a1_text.has_value()) {
                check(value.kind() == fastxlsx::CellValueKind::Text &&
                        value.text_value() == *expected_a1_text,
                    std::string(label) + " should expose the expected A1 text");
            } else {
                check(value.kind() == fastxlsx::CellValueKind::Blank,
                    std::string(label) + " should expose A1 as an explicit blank");
            }
        };

    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
    check(!sheet.has_pending_changes(),
        prefix + " should keep the materialized sheet clean");
    check(sheet.cell_count() == 3,
        prefix + " should keep the represented sparse cell count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 2,
        prefix + " should expose the represented sparse bounds");

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == 3,
        prefix + " should expose all represented sparse cells");
    if (cells.size() == 3) {
        check(cells[0].reference.row == 1 &&
                cells[0].reference.column == 1,
            prefix + " should expose A1 first");
        check_expected_a1_value(cells[0].value, prefix + " sparse A1");
        check(cells[1].reference.row == 1 &&
                cells[1].reference.column == 2 &&
                cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                cells[1].value.number_value() == 1.0,
            prefix + " should expose source-backed B1 second");
        check(cells[2].reference.row == 2 &&
                cells[2].reference.column == 1 &&
                cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                cells[2].value.text_value() == "placeholder-a2",
            prefix + " should expose source-backed A2 last");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
        sheet.sparse_cells("A1:D4");
    check(range_cells.size() == 3,
        prefix + " range sparse_cells should expose all represented sparse cells");
    if (range_cells.size() == 3) {
        check(range_cells[0].reference.row == 1 &&
                range_cells[0].reference.column == 1,
            prefix + " range sparse_cells should expose A1 first");
        check_expected_a1_value(
            range_cells[0].value, prefix + " range sparse_cells A1");
        check(range_cells[1].reference.row == 1 &&
                range_cells[1].reference.column == 2 &&
                range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                range_cells[1].value.number_value() == 1.0,
            prefix + " range sparse_cells should expose source-backed B1 second");
        check(range_cells[2].reference.row == 2 &&
                range_cells[2].reference.column == 1 &&
                range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                range_cells[2].value.text_value() == "placeholder-a2",
            prefix + " range sparse_cells should expose source-backed A2 last");
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
        sheet.sparse_cells(requested_refs);
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
                requested_cells[2].reference.column == 1,
            prefix + " requested sparse_cells should keep A1 in requested order");
        check_expected_a1_value(
            requested_cells[2].value, prefix + " requested sparse_cells A1");
        check(requested_cells[3].reference.row == 2 &&
                requested_cells[3].reference.column == 1 &&
                requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                requested_cells[3].value.text_value() == "placeholder-a2",
            prefix + " requested sparse_cells should preserve duplicate A2");
    }

    const fastxlsx::CellValue a1 = sheet.get_cell("A1");
    check_expected_a1_value(a1, prefix + " scalar A1");
    const fastxlsx::CellValue b1 = sheet.get_cell("B1");
    check(b1.kind() == fastxlsx::CellValueKind::Number &&
            b1.number_value() == 1.0,
        prefix + " should keep source-backed B1");
    const fastxlsx::CellValue a2 = sheet.get_cell("A2");
    check(a2.kind() == fastxlsx::CellValueKind::Text &&
            a2.text_value() == "placeholder-a2",
        prefix + " should keep source-backed A2");
    check(!sheet.try_cell("D4").has_value(),
        prefix + " should keep rejected blank D4 absent");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        sheet.row_cells(1);
    check(row_one.size() == 2 &&
            row_one[0].reference.row == 1 &&
            row_one[0].reference.column == 1 &&
            row_one[1].reference.row == 1 &&
            row_one[1].reference.column == 2 &&
            row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            row_one[1].value.number_value() == 1.0,
        prefix + " row_cells should expose A1 and source-backed B1");
    if (row_one.size() == 2) {
        check_expected_a1_value(row_one[0].value, prefix + " row_cells A1");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        sheet.row_cells(2);
    check(row_two.size() == 1 &&
            row_two[0].reference.row == 2 &&
            row_two[0].reference.column == 1 &&
            row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            row_two[0].value.text_value() == "placeholder-a2",
        prefix + " row_cells should expose source-backed A2");
    check(sheet.row_cells(3).empty(),
        prefix + " row_cells should keep the gap row empty");
    check(sheet.row_cells(4).empty(),
        prefix + " row_cells should keep rejected D4 row empty");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
        sheet.column_cells(1);
    check(column_one.size() == 2 &&
            column_one[0].reference.row == 1 &&
            column_one[0].reference.column == 1 &&
            column_one[1].reference.row == 2 &&
            column_one[1].reference.column == 1 &&
            column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            column_one[1].value.text_value() == "placeholder-a2",
        prefix + " column_cells should expose A1 and source-backed A2");
    if (column_one.size() == 2) {
        check_expected_a1_value(column_one[0].value, prefix + " column_cells A1");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
        sheet.column_cells(2);
    check(column_two.size() == 1 &&
            column_two[0].reference.row == 1 &&
            column_two[0].reference.column == 2 &&
            column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            column_two[0].value.number_value() == 1.0,
        prefix + " column_cells should expose source-backed B1");
    check(sheet.column_cells(3).empty(),
        prefix + " column_cells should keep the gap column empty");
    check(sheet.column_cells(4).empty(),
        prefix + " column_cells should keep rejected D4 absent");

    check(editor.pending_change_count() == expected_pending_count,
        prefix + " should keep the expected materialized handoff count");
    check(editor.pending_materialized_worksheet_names().empty(),
        prefix + " should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        prefix + " should not expose dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should not expose dirty materialized summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not queue replacement diagnostics");
}

void check_reopened_blank_guardrail_overwrite_output(
    const std::filesystem::path& output,
    const fastxlsx::WorksheetEditorOptions& options,
    std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet =
        reopened_editor.worksheet("Data", options);
    const std::string prefix(scenario);

    check_reopened_editor_clean_public_state(reopened_editor, prefix, "output");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize a clean sheet");
    check(reopened_sheet.cell_count() == 3,
        prefix + " reopened output should keep the source sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
        prefix + " reopened output should keep the source used range");

    const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
    check(reopened_a1.kind() == fastxlsx::CellValueKind::Blank,
        prefix + " reopened output should read A1 as an explicit blank");
    const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
    check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_b1.number_value() == 1.0,
        prefix + " reopened output should keep source-backed B1");
    const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
    check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
            reopened_a2.text_value() == "placeholder-a2",
        prefix + " reopened output should keep source-backed A2");
    check(!reopened_sheet.try_cell("D4").has_value(),
        prefix + " reopened output should keep rejected blank D4 absent");

    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_cells =
        reopened_sheet.sparse_cells();
    check(reopened_cells.size() == 3,
        prefix + " reopened sparse_cells should expose all represented cells");
    if (reopened_cells.size() == 3) {
        check(reopened_cells[0].reference.row == 1 &&
                reopened_cells[0].reference.column == 1 &&
                reopened_cells[0].value.kind() == fastxlsx::CellValueKind::Blank,
            prefix + " reopened sparse_cells should expose blank A1 first");
        check(reopened_cells[1].reference.row == 1 &&
                reopened_cells[1].reference.column == 2 &&
                reopened_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                reopened_cells[1].value.number_value() == 1.0,
            prefix + " reopened sparse_cells should expose B1 second");
        check(reopened_cells[2].reference.row == 2 &&
                reopened_cells[2].reference.column == 1 &&
                reopened_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                reopened_cells[2].value.text_value() == "placeholder-a2",
            prefix + " reopened sparse_cells should expose A2 last");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_range_cells =
        reopened_sheet.sparse_cells("A1:D4");
    check(reopened_range_cells.size() == 3,
        prefix + " reopened range sparse_cells should expose all represented cells");
    if (reopened_range_cells.size() == 3) {
        check(reopened_range_cells[0].reference.row == 1 &&
                reopened_range_cells[0].reference.column == 1 &&
                reopened_range_cells[0].value.kind() == fastxlsx::CellValueKind::Blank,
            prefix + " reopened range sparse_cells should expose blank A1 first");
        check(reopened_range_cells[1].reference.row == 1 &&
                reopened_range_cells[1].reference.column == 2 &&
                reopened_range_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                reopened_range_cells[1].value.number_value() == 1.0,
            prefix + " reopened range sparse_cells should expose B1 second");
        check(reopened_range_cells[2].reference.row == 2 &&
                reopened_range_cells[2].reference.column == 1 &&
                reopened_range_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                reopened_range_cells[2].value.text_value() == "placeholder-a2",
            prefix + " reopened range sparse_cells should expose A2 last");
    }
    const std::array<fastxlsx::WorksheetCellReference, 6> reopened_requested_refs {
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {3, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_requested_cells =
        reopened_sheet.sparse_cells(reopened_requested_refs);
    check(reopened_requested_cells.size() == 4,
        prefix + " reopened requested sparse_cells should skip rejected/gap coordinates and keep duplicates");
    if (reopened_requested_cells.size() == 4) {
        check(reopened_requested_cells[0].reference.row == 2 &&
                reopened_requested_cells[0].reference.column == 1 &&
                reopened_requested_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                reopened_requested_cells[0].value.text_value() == "placeholder-a2",
            prefix + " reopened requested sparse_cells should keep A2 first");
        check(reopened_requested_cells[1].reference.row == 1 &&
                reopened_requested_cells[1].reference.column == 2 &&
                reopened_requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                reopened_requested_cells[1].value.number_value() == 1.0,
            prefix + " reopened requested sparse_cells should keep B1 after skipped D4");
        check(reopened_requested_cells[2].reference.row == 1 &&
                reopened_requested_cells[2].reference.column == 1 &&
                reopened_requested_cells[2].value.kind() == fastxlsx::CellValueKind::Blank,
            prefix + " reopened requested sparse_cells should keep blank A1 in requested order");
        check(reopened_requested_cells[3].reference.row == 2 &&
                reopened_requested_cells[3].reference.column == 1 &&
                reopened_requested_cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
                reopened_requested_cells[3].value.text_value() == "placeholder-a2",
            prefix + " reopened requested sparse_cells should preserve duplicate A2");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
        reopened_sheet.row_cells(1);
    check(reopened_row_one.size() == 2 &&
            reopened_row_one[0].reference.row == 1 &&
            reopened_row_one[0].reference.column == 1 &&
            reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            reopened_row_one[1].reference.row == 1 &&
            reopened_row_one[1].reference.column == 2 &&
            reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_row_one[1].value.number_value() == 1.0,
        prefix + " reopened row_cells should expose blank A1 and source B1");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
        reopened_sheet.row_cells(2);
    check(reopened_row_two.size() == 1 &&
            reopened_row_two[0].reference.row == 2 &&
            reopened_row_two[0].reference.column == 1 &&
            reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_row_two[0].value.text_value() == "placeholder-a2",
        prefix + " reopened row_cells should expose source-backed A2");
    check(reopened_sheet.row_cells(3).empty(),
        prefix + " reopened row_cells should keep the gap row empty");
    check(reopened_sheet.row_cells(4).empty(),
        prefix + " reopened row_cells should keep rejected D4 row empty");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
        reopened_sheet.column_cells(1);
    check(reopened_column_one.size() == 2 &&
            reopened_column_one[0].reference.row == 1 &&
            reopened_column_one[0].reference.column == 1 &&
            reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            reopened_column_one[1].reference.row == 2 &&
            reopened_column_one[1].reference.column == 1 &&
            reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
            reopened_column_one[1].value.text_value() == "placeholder-a2",
        prefix + " reopened column_cells should expose blank A1 and source A2");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_two =
        reopened_sheet.column_cells(2);
    check(reopened_column_two.size() == 1 &&
            reopened_column_two[0].reference.row == 1 &&
            reopened_column_two[0].reference.column == 2 &&
            reopened_column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
            reopened_column_two[0].value.number_value() == 1.0,
        prefix + " reopened column_cells should expose source-backed B1");
    check(reopened_sheet.column_cells(3).empty(),
        prefix + " reopened column_cells should keep the gap column empty");
    check(reopened_sheet.column_cells(4).empty(),
        prefix + " reopened column_cells should keep rejected blank column empty");
    check_reopened_editor_clean_public_state(reopened_editor, prefix, "readback");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep sheet state clean");
}

void test_public_worksheet_editor_blank_insertions_obey_guardrail_budgets()
{
    const std::filesystem::path max_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-source.xlsx");
    const std::filesystem::path max_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-output.xlsx");
    const std::filesystem::path max_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-noop-output.xlsx");
    const std::filesystem::path max_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-second-noop-output.xlsx");
    const std::filesystem::path max_reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-reacquired-noop-output.xlsx");
    const std::filesystem::path max_reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-reacquired-second-noop-output.xlsx");
    const std::filesystem::path max_reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-max-guard-reacquired-post-noop-output.xlsx");
    const auto max_source_entries = fastxlsx::test::read_zip_entries(max_source);

    fastxlsx::WorkbookEditor max_sizing_editor =
        fastxlsx::WorkbookEditor::open(max_source);
    const fastxlsx::WorksheetEditor max_sizing_sheet =
        max_sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = max_sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor max_editor = fastxlsx::WorkbookEditor::open(max_source);
    fastxlsx::WorksheetEditorOptions max_options;
    max_options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor max_sheet =
        max_editor.worksheet("Data", max_options);

    const std::size_t max_baseline_count = max_sheet.cell_count();
    const std::size_t max_baseline_memory = max_sheet.estimated_memory_usage();
    check(!max_sheet.try_cell("D4").has_value(),
        "blank max_cells guard test precondition should use a missing target cell");

    bool max_blank_failed = false;
    try {
        max_sheet.set_cell("D4", fastxlsx::CellValue::blank());
    } catch (const fastxlsx::FastXlsxError& error) {
        max_blank_failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "explicit blank insertion should expose max_cells diagnostics");
    }
    check(max_blank_failed,
        "explicit blank insertion should obey the exact max_cells guardrail");
    check(max_editor.last_edit_error().has_value(),
        "failed explicit blank max_cells insertion should update last_edit_error");
    check(!max_sheet.has_pending_changes(),
        "failed explicit blank max_cells insertion should not dirty the session");
    check(!max_editor.has_pending_changes(),
        "failed explicit blank max_cells insertion should not dirty the editor");
    check(max_editor.pending_materialized_worksheet_names().empty(),
        "failed explicit blank max_cells insertion should not expose dirty names");
    check(max_editor.pending_materialized_cell_count() == 0,
        "failed explicit blank max_cells insertion should not expose dirty cell count");
    check(max_editor.estimated_pending_materialized_memory_usage() == 0,
        "failed explicit blank max_cells insertion should not expose dirty memory");
    check(max_sheet.cell_count() == max_baseline_count,
        "failed explicit blank max_cells insertion should preserve sparse count");
    check(max_sheet.estimated_memory_usage() == max_baseline_memory,
        "failed explicit blank max_cells insertion should preserve memory estimate");
    check(!max_sheet.try_cell("D4").has_value(),
        "failed explicit blank max_cells insertion should not create a blank D4");
    check(fastxlsx::test::read_zip_entries(max_source) == max_source_entries,
        "failed explicit blank max_cells insertion should leave the source package unchanged");

    max_sheet.set_cell("A1", fastxlsx::CellValue::blank());
    check(!max_editor.last_edit_error().has_value(),
        "successful existing-cell blank overwrite should clear max_cells diagnostic");
    check(max_sheet.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the session");
    check(max_editor.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the editor");
    check(max_editor.pending_materialized_cell_count() == max_baseline_count,
        "existing-cell blank overwrite should keep the sparse count stable");
    const std::size_t max_blank_memory = max_sheet.estimated_memory_usage();
    check(max_editor.estimated_pending_materialized_memory_usage() == max_blank_memory,
        "existing-cell blank overwrite should expose blank dirty memory");
    check(max_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "existing-cell blank overwrite should read back as explicit blank");

    max_editor.save_as(max_output);
    check(fastxlsx::test::read_zip_entries(max_source) == max_source_entries,
        "max_cells blank overwrite save_as should leave the source package unchanged");
    const auto max_output_entries = fastxlsx::test::read_zip_entries(max_output);
    const std::string max_worksheet_xml =
        max_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(max_worksheet_xml, R"(<c r="A1"/>)",
        "existing-cell blank overwrite should save as an empty A1 cell");
    check_not_contains(max_worksheet_xml, "placeholder-a1",
        "blank overwrite should remove the previous A1 text");
    check_not_contains(max_worksheet_xml, R"(r="D4")",
        "rejected blank max_cells insertion should not leak as D4");
    check_reopened_blank_guardrail_overwrite_output(
        max_output,
        max_options,
        "max_cells blank overwrite");

    fastxlsx::WorkbookEditor max_reacquired_editor =
        fastxlsx::WorkbookEditor::open(max_output);
    fastxlsx::WorksheetEditor max_reacquired_sheet =
        max_reacquired_editor.worksheet("Data", max_options);
    check_reopened_editor_clean_public_state(
        max_reacquired_editor,
        "max_cells blank overwrite",
        "strict-options reacquire");
    check(!max_reacquired_sheet.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquire should keep the sheet clean");
    check(max_reacquired_sheet.cell_count() == max_baseline_count,
        "max_cells blank overwrite strict-options reacquire should keep the sparse count stable");
    check(max_reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "max_cells blank overwrite strict-options reacquire should read A1 as blank");
    check(!max_reacquired_sheet.try_cell("D4").has_value(),
        "max_cells blank overwrite strict-options reacquire should keep rejected D4 absent");
    check_blank_guardrail_overwrite_handle_snapshots(
        max_reacquired_editor,
        max_reacquired_sheet,
        0,
        "max_cells blank overwrite strict-options reacquire",
        std::nullopt);

    const WorkbookEditorPublicCatalogSnapshot max_reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(max_reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot max_reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(max_reacquired_editor);
    max_reacquired_editor.save_as(max_reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        max_reacquired_editor, max_reacquired_save_state_before_noop,
        "max_cells blank overwrite strict-options reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        max_reacquired_editor, max_reacquired_catalog_before_noop,
        "max_cells blank overwrite strict-options reacquired noop save");
    check_reopened_editor_clean_public_state(
        max_reacquired_editor,
        "max_cells blank overwrite",
        "strict-options reacquired noop save");
    check(!max_reacquired_sheet.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquired noop save should keep the sheet clean");
    check_blank_guardrail_overwrite_handle_snapshots(
        max_reacquired_editor,
        max_reacquired_sheet,
        0,
        "max_cells blank overwrite strict-options reacquired noop save",
        std::nullopt);
    const auto max_reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(max_reacquired_noop_output);
    check(max_reacquired_noop_entries == max_output_entries,
        "max_cells blank overwrite strict-options reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(max_output) == max_output_entries,
        "max_cells blank overwrite strict-options reacquired noop save should leave the saved input unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        max_reacquired_noop_output,
        max_options,
        "max_cells blank overwrite strict-options reacquired noop save");

    const WorkbookEditorPublicCatalogSnapshot max_reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(max_reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot max_reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(max_reacquired_editor);
    max_reacquired_editor.save_as(max_reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        max_reacquired_editor, max_reacquired_save_state_before_second_noop,
        "max_cells blank overwrite strict-options reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        max_reacquired_editor, max_reacquired_catalog_before_second_noop,
        "max_cells blank overwrite strict-options reacquired second noop save");
    check_reopened_editor_clean_public_state(
        max_reacquired_editor,
        "max_cells blank overwrite",
        "strict-options reacquired second noop save");
    check(!max_reacquired_sheet.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquired second noop save should keep the sheet clean");
    check_blank_guardrail_overwrite_handle_snapshots(
        max_reacquired_editor,
        max_reacquired_sheet,
        0,
        "max_cells blank overwrite strict-options reacquired second noop save",
        std::nullopt);
    const auto max_reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(max_reacquired_second_noop_output);
    check(max_reacquired_second_noop_entries == max_reacquired_noop_entries,
        "max_cells blank overwrite strict-options reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(max_reacquired_noop_output) == max_reacquired_noop_entries,
        "max_cells blank overwrite strict-options reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(max_output) == max_output_entries,
        "max_cells blank overwrite strict-options reacquired second noop save should leave the saved input unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        max_reacquired_second_noop_output,
        max_options,
        "max_cells blank overwrite strict-options reacquired second noop save");

    max_reacquired_sheet.set_cell("A1",
        fastxlsx::CellValue::text("max-blank-final"));
    check(!max_reacquired_editor.last_edit_error().has_value(),
        "max_cells blank overwrite strict-options reacquired post-noop edit should keep diagnostics clear");
    check(max_reacquired_sheet.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquired post-noop edit should dirty the sheet");
    check(max_reacquired_editor.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquired post-noop edit should dirty the editor");
    check(max_reacquired_editor.pending_materialized_cell_count() == max_baseline_count,
        "max_cells blank overwrite strict-options reacquired post-noop edit should keep sparse count stable");
    check(max_reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
            max_reacquired_sheet.get_cell("A1").text_value() == "max-blank-final",
        "max_cells blank overwrite strict-options reacquired post-noop edit should overwrite the explicit blank");
    check_public_state_single_data_dirty_materialized_summary(
        max_reacquired_editor,
        max_reacquired_sheet,
        0,
        "max_cells blank overwrite strict-options reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        max_reacquired_editor,
        "max_cells blank overwrite strict-options reacquired post-noop edit should not queue replacement diagnostics");

    max_reacquired_editor.save_as(max_reacquired_post_noop_output);
    check(!max_reacquired_sheet.has_pending_changes(),
        "max_cells blank overwrite strict-options reacquired post-noop save should clean the sheet");
    check(max_reacquired_editor.pending_change_count() == 1,
        "max_cells blank overwrite strict-options reacquired post-noop save should keep one handoff");
    check(max_reacquired_editor.pending_materialized_worksheet_names().empty(),
        "max_cells blank overwrite strict-options reacquired post-noop save should not expose dirty worksheet names");
    check(max_reacquired_editor.pending_materialized_cell_count() == 0,
        "max_cells blank overwrite strict-options reacquired post-noop save should not expose dirty materialized cells");
    check(max_reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "max_cells blank overwrite strict-options reacquired post-noop save should not expose dirty materialized memory");
    check(max_reacquired_editor.pending_worksheet_edits().empty(),
        "max_cells blank overwrite strict-options reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        max_reacquired_editor,
        "max_cells blank overwrite strict-options reacquired post-noop save should not queue replacement diagnostics");
    check(!max_reacquired_editor.last_edit_error().has_value(),
        "max_cells blank overwrite strict-options reacquired post-noop save should keep diagnostics clear");
    check_blank_guardrail_overwrite_handle_snapshots(
        max_reacquired_editor,
        max_reacquired_sheet,
        1,
        "max_cells blank overwrite strict-options reacquired post-noop save",
        "max-blank-final");
    const auto max_reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(max_reacquired_post_noop_output);
    const std::string max_reacquired_post_noop_xml =
        max_reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(max_reacquired_post_noop_xml, "max-blank-final",
        "max_cells blank overwrite strict-options reacquired post-noop save should persist the later overwrite");
    check_not_contains(max_reacquired_post_noop_xml, "placeholder-a1",
        "max_cells blank overwrite strict-options reacquired post-noop save should not revive old A1 text");
    check_not_contains(max_reacquired_post_noop_xml, R"(r="D4")",
        "max_cells blank overwrite strict-options reacquired post-noop save should keep rejected D4 absent");
    check(fastxlsx::test::read_zip_entries(max_reacquired_noop_output) == max_reacquired_noop_entries,
        "max_cells blank overwrite strict-options reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(max_reacquired_second_noop_output) == max_reacquired_second_noop_entries,
        "max_cells blank overwrite strict-options reacquired post-noop save should leave the second noop output unchanged");
    check(max_reacquired_noop_entries == max_output_entries,
        "max_cells blank overwrite strict-options reacquired post-noop save should leave the first noop output stable");
    check(max_reacquired_second_noop_entries == max_reacquired_noop_entries,
        "max_cells blank overwrite strict-options reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(max_output) == max_output_entries,
        "max_cells blank overwrite strict-options reacquired post-noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        max_reacquired_post_noop_output,
        "max_cells blank overwrite strict-options reacquired post-noop save",
        "max-blank-final");

    const WorkbookEditorPublicCatalogSnapshot max_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(max_editor);
    const WorkbookEditorPublicSaveStateSnapshot max_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(max_editor);
    max_editor.save_as(max_noop_output);
    check(!max_sheet.has_pending_changes(),
        "max_cells blank overwrite noop save should keep the materialized session clean");
    check(max_editor.pending_change_count() == 1,
        "max_cells blank overwrite noop save should not add another handoff");
    check(max_editor.pending_materialized_worksheet_names().empty(),
        "max_cells blank overwrite noop save should not expose dirty worksheet names");
    check(max_editor.pending_materialized_cell_count() == 0,
        "max_cells blank overwrite noop save should not expose dirty materialized cells");
    check(max_editor.estimated_pending_materialized_memory_usage() == 0,
        "max_cells blank overwrite noop save should not expose dirty materialized memory");
    check(max_editor.pending_worksheet_edits().empty(),
        "max_cells blank overwrite noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        max_editor,
        "max_cells blank overwrite noop save should not queue replacement diagnostics");
    check(!max_editor.last_edit_error().has_value(),
        "max_cells blank overwrite noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        max_editor, max_save_state_before_noop,
        "max_cells blank overwrite noop save");
    check_workbook_editor_public_catalog_preserved(
        max_editor, max_catalog_before_noop,
        "max_cells blank overwrite noop save");
    const auto max_noop_entries = fastxlsx::test::read_zip_entries(max_noop_output);
    check(max_noop_entries == max_output_entries,
        "max_cells blank overwrite noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(max_source) == max_source_entries,
        "max_cells blank overwrite noop save should leave the source package unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        max_noop_output,
        max_options,
        "max_cells blank overwrite noop save");

    const WorkbookEditorPublicCatalogSnapshot max_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(max_editor);
    const WorkbookEditorPublicSaveStateSnapshot max_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(max_editor);
    max_editor.save_as(max_second_noop_output);
    check(!max_sheet.has_pending_changes(),
        "max_cells blank overwrite second noop save should keep the materialized session clean");
    check(max_editor.pending_change_count() == 1,
        "max_cells blank overwrite second noop save should not add another handoff");
    check(max_editor.pending_materialized_worksheet_names().empty(),
        "max_cells blank overwrite second noop save should not expose dirty worksheet names");
    check(max_editor.pending_materialized_cell_count() == 0,
        "max_cells blank overwrite second noop save should not expose dirty materialized cells");
    check(max_editor.estimated_pending_materialized_memory_usage() == 0,
        "max_cells blank overwrite second noop save should not expose dirty materialized memory");
    check(max_editor.pending_worksheet_edits().empty(),
        "max_cells blank overwrite second noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        max_editor,
        "max_cells blank overwrite second noop save should not queue replacement diagnostics");
    check(!max_editor.last_edit_error().has_value(),
        "max_cells blank overwrite second noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        max_editor, max_save_state_before_second_noop,
        "max_cells blank overwrite second noop save");
    check_workbook_editor_public_catalog_preserved(
        max_editor, max_catalog_before_second_noop,
        "max_cells blank overwrite second noop save");
    const auto max_second_noop_entries =
        fastxlsx::test::read_zip_entries(max_second_noop_output);
    check(max_second_noop_entries == max_noop_entries,
        "max_cells blank overwrite second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(max_noop_output) == max_noop_entries,
        "max_cells blank overwrite second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(max_source) == max_source_entries,
        "max_cells blank overwrite second noop save should leave the source package unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        max_second_noop_output,
        max_options,
        "max_cells blank overwrite second noop save");

    const std::filesystem::path memory_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-source.xlsx");
    const std::filesystem::path memory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-output.xlsx");
    const std::filesystem::path memory_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-noop-output.xlsx");
    const std::filesystem::path memory_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-second-noop-output.xlsx");
    const std::filesystem::path memory_reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-reacquired-noop-output.xlsx");
    const std::filesystem::path memory_reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-reacquired-second-noop-output.xlsx");
    const std::filesystem::path memory_reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-blank-memory-guard-reacquired-post-noop-output.xlsx");
    const auto memory_source_entries = fastxlsx::test::read_zip_entries(memory_source);

    fastxlsx::WorkbookEditor memory_sizing_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    const fastxlsx::WorksheetEditor memory_sizing_sheet =
        memory_sizing_editor.worksheet("Data");
    const std::size_t exact_memory_budget =
        memory_sizing_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(memory_source);
    fastxlsx::WorksheetEditorOptions memory_options;
    memory_options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor memory_sheet =
        memory_editor.worksheet("Data", memory_options);

    const std::size_t memory_baseline_count = memory_sheet.cell_count();
    const std::size_t memory_baseline_usage =
        memory_sheet.estimated_memory_usage();
    check(!memory_sheet.try_cell("D4").has_value(),
        "blank memory-budget guard test precondition should use a missing target cell");

    bool memory_blank_failed = false;
    try {
        memory_sheet.set_cell("D4", fastxlsx::CellValue::blank());
    } catch (const fastxlsx::FastXlsxError& error) {
        memory_blank_failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "explicit blank insertion should expose memory-budget diagnostics");
    }
    check(memory_blank_failed,
        "explicit blank insertion should obey the exact memory-budget guardrail");
    check(memory_editor.last_edit_error().has_value(),
        "failed explicit blank memory-budget insertion should update last_edit_error");
    check(!memory_sheet.has_pending_changes(),
        "failed explicit blank memory-budget insertion should not dirty the session");
    check(!memory_editor.has_pending_changes(),
        "failed explicit blank memory-budget insertion should not dirty the editor");
    check(memory_editor.pending_materialized_worksheet_names().empty(),
        "failed explicit blank memory-budget insertion should not expose dirty names");
    check(memory_editor.pending_materialized_cell_count() == 0,
        "failed explicit blank memory-budget insertion should not expose dirty cell count");
    check(memory_editor.estimated_pending_materialized_memory_usage() == 0,
        "failed explicit blank memory-budget insertion should not expose dirty memory");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "failed explicit blank memory-budget insertion should preserve sparse count");
    check(memory_sheet.estimated_memory_usage() == memory_baseline_usage,
        "failed explicit blank memory-budget insertion should preserve memory estimate");
    check(!memory_sheet.try_cell("D4").has_value(),
        "failed explicit blank memory-budget insertion should not create a blank D4");
    check(fastxlsx::test::read_zip_entries(memory_source) == memory_source_entries,
        "failed explicit blank memory-budget insertion should leave the source package unchanged");

    memory_sheet.set_cell("A1", fastxlsx::CellValue::blank());
    check(!memory_editor.last_edit_error().has_value(),
        "successful existing-cell blank overwrite should clear memory-budget diagnostic");
    check(memory_sheet.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the memory-budget session");
    check(memory_editor.has_pending_changes(),
        "successful existing-cell blank overwrite should dirty the memory-budget editor");
    check(memory_sheet.cell_count() == memory_baseline_count,
        "existing-cell blank overwrite should keep memory-budget sparse count stable");
    check(memory_editor.pending_materialized_cell_count() == memory_baseline_count,
        "existing-cell blank overwrite should expose memory-budget dirty sparse count");
    check(memory_sheet.estimated_memory_usage() <= exact_memory_budget,
        "existing-cell blank overwrite should stay within exact memory budget");
    const std::size_t memory_blank_memory = memory_sheet.estimated_memory_usage();
    check(memory_editor.estimated_pending_materialized_memory_usage() == memory_blank_memory,
        "existing-cell blank overwrite should expose memory-budget dirty memory");
    check(memory_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "memory-budget blank overwrite should read back as explicit blank");

    memory_editor.save_as(memory_output);
    check(fastxlsx::test::read_zip_entries(memory_source) == memory_source_entries,
        "memory-budget blank overwrite save_as should leave the source package unchanged");
    const auto memory_output_entries =
        fastxlsx::test::read_zip_entries(memory_output);
    const std::string memory_worksheet_xml =
        memory_output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(memory_worksheet_xml, R"(<c r="A1"/>)",
        "memory-budget blank overwrite should save as an empty A1 cell");
    check_not_contains(memory_worksheet_xml, "placeholder-a1",
        "memory-budget blank overwrite should remove the previous A1 text");
    check_not_contains(memory_worksheet_xml, R"(r="D4")",
        "rejected blank memory-budget insertion should not leak as D4");
    check_reopened_blank_guardrail_overwrite_output(
        memory_output,
        memory_options,
        "memory-budget blank overwrite");

    fastxlsx::WorkbookEditor memory_reacquired_editor =
        fastxlsx::WorkbookEditor::open(memory_output);
    fastxlsx::WorksheetEditor memory_reacquired_sheet =
        memory_reacquired_editor.worksheet("Data", memory_options);
    check_reopened_editor_clean_public_state(
        memory_reacquired_editor,
        "memory-budget blank overwrite",
        "strict-options reacquire");
    check(!memory_reacquired_sheet.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquire should keep the sheet clean");
    check(memory_reacquired_sheet.cell_count() == memory_baseline_count,
        "memory-budget blank overwrite strict-options reacquire should keep the sparse count stable");
    check(memory_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "memory-budget blank overwrite strict-options reacquire should stay within the original budget");
    check(memory_reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "memory-budget blank overwrite strict-options reacquire should read A1 as blank");
    check(!memory_reacquired_sheet.try_cell("D4").has_value(),
        "memory-budget blank overwrite strict-options reacquire should keep rejected D4 absent");
    check_blank_guardrail_overwrite_handle_snapshots(
        memory_reacquired_editor,
        memory_reacquired_sheet,
        0,
        "memory-budget blank overwrite strict-options reacquire",
        std::nullopt);

    const WorkbookEditorPublicCatalogSnapshot memory_reacquired_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(memory_reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot memory_reacquired_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(memory_reacquired_editor);
    memory_reacquired_editor.save_as(memory_reacquired_noop_output);
    check_workbook_editor_public_save_state_preserved(
        memory_reacquired_editor, memory_reacquired_save_state_before_noop,
        "memory-budget blank overwrite strict-options reacquired noop save");
    check_workbook_editor_public_catalog_preserved(
        memory_reacquired_editor, memory_reacquired_catalog_before_noop,
        "memory-budget blank overwrite strict-options reacquired noop save");
    check_reopened_editor_clean_public_state(
        memory_reacquired_editor,
        "memory-budget blank overwrite",
        "strict-options reacquired noop save");
    check(!memory_reacquired_sheet.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquired noop save should keep the sheet clean");
    check(memory_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "memory-budget blank overwrite strict-options reacquired noop save should stay within the original budget");
    check_blank_guardrail_overwrite_handle_snapshots(
        memory_reacquired_editor,
        memory_reacquired_sheet,
        0,
        "memory-budget blank overwrite strict-options reacquired noop save",
        std::nullopt);
    const auto memory_reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(memory_reacquired_noop_output);
    check(memory_reacquired_noop_entries == memory_output_entries,
        "memory-budget blank overwrite strict-options reacquired noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(memory_output) == memory_output_entries,
        "memory-budget blank overwrite strict-options reacquired noop save should leave the saved input unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        memory_reacquired_noop_output,
        memory_options,
        "memory-budget blank overwrite strict-options reacquired noop save");

    const WorkbookEditorPublicCatalogSnapshot memory_reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(memory_reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot memory_reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(memory_reacquired_editor);
    memory_reacquired_editor.save_as(memory_reacquired_second_noop_output);
    check_workbook_editor_public_save_state_preserved(
        memory_reacquired_editor, memory_reacquired_save_state_before_second_noop,
        "memory-budget blank overwrite strict-options reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        memory_reacquired_editor, memory_reacquired_catalog_before_second_noop,
        "memory-budget blank overwrite strict-options reacquired second noop save");
    check_reopened_editor_clean_public_state(
        memory_reacquired_editor,
        "memory-budget blank overwrite",
        "strict-options reacquired second noop save");
    check(!memory_reacquired_sheet.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquired second noop save should keep the sheet clean");
    check(memory_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "memory-budget blank overwrite strict-options reacquired second noop save should stay within the original budget");
    check_blank_guardrail_overwrite_handle_snapshots(
        memory_reacquired_editor,
        memory_reacquired_sheet,
        0,
        "memory-budget blank overwrite strict-options reacquired second noop save",
        std::nullopt);
    const auto memory_reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(memory_reacquired_second_noop_output);
    check(memory_reacquired_second_noop_entries == memory_reacquired_noop_entries,
        "memory-budget blank overwrite strict-options reacquired second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(memory_reacquired_noop_output) == memory_reacquired_noop_entries,
        "memory-budget blank overwrite strict-options reacquired second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(memory_output) == memory_output_entries,
        "memory-budget blank overwrite strict-options reacquired second noop save should leave the saved input unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        memory_reacquired_second_noop_output,
        memory_options,
        "memory-budget blank overwrite strict-options reacquired second noop save");

    memory_reacquired_sheet.set_cell("A1",
        fastxlsx::CellValue::text("mini-a1"));
    check(!memory_reacquired_editor.last_edit_error().has_value(),
        "memory-budget blank overwrite strict-options reacquired post-noop edit should keep diagnostics clear");
    check(memory_reacquired_sheet.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquired post-noop edit should dirty the sheet");
    check(memory_reacquired_editor.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquired post-noop edit should dirty the editor");
    check(memory_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "memory-budget blank overwrite strict-options reacquired post-noop edit should stay within the original budget");
    check(memory_reacquired_editor.pending_materialized_cell_count() == memory_baseline_count,
        "memory-budget blank overwrite strict-options reacquired post-noop edit should keep sparse count stable");
    check(memory_reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
            memory_reacquired_sheet.get_cell("A1").text_value() == "mini-a1",
        "memory-budget blank overwrite strict-options reacquired post-noop edit should overwrite the explicit blank");
    check_public_state_single_data_dirty_materialized_summary(
        memory_reacquired_editor,
        memory_reacquired_sheet,
        0,
        "memory-budget blank overwrite strict-options reacquired post-noop edit");
    check_workbook_editor_no_replacement_diagnostics(
        memory_reacquired_editor,
        "memory-budget blank overwrite strict-options reacquired post-noop edit should not queue replacement diagnostics");

    memory_reacquired_editor.save_as(memory_reacquired_post_noop_output);
    check(!memory_reacquired_sheet.has_pending_changes(),
        "memory-budget blank overwrite strict-options reacquired post-noop save should clean the sheet");
    check(memory_reacquired_editor.pending_change_count() == 1,
        "memory-budget blank overwrite strict-options reacquired post-noop save should keep one handoff");
    check(memory_reacquired_editor.pending_materialized_worksheet_names().empty(),
        "memory-budget blank overwrite strict-options reacquired post-noop save should not expose dirty worksheet names");
    check(memory_reacquired_editor.pending_materialized_cell_count() == 0,
        "memory-budget blank overwrite strict-options reacquired post-noop save should not expose dirty materialized cells");
    check(memory_reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget blank overwrite strict-options reacquired post-noop save should not expose dirty materialized memory");
    check(memory_reacquired_editor.pending_worksheet_edits().empty(),
        "memory-budget blank overwrite strict-options reacquired post-noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        memory_reacquired_editor,
        "memory-budget blank overwrite strict-options reacquired post-noop save should not queue replacement diagnostics");
    check(!memory_reacquired_editor.last_edit_error().has_value(),
        "memory-budget blank overwrite strict-options reacquired post-noop save should keep diagnostics clear");
    check_blank_guardrail_overwrite_handle_snapshots(
        memory_reacquired_editor,
        memory_reacquired_sheet,
        1,
        "memory-budget blank overwrite strict-options reacquired post-noop save",
        "mini-a1");
    const auto memory_reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(memory_reacquired_post_noop_output);
    const std::string memory_reacquired_post_noop_xml =
        memory_reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(memory_reacquired_post_noop_xml, "mini-a1",
        "memory-budget blank overwrite strict-options reacquired post-noop save should persist the later overwrite");
    check_not_contains(memory_reacquired_post_noop_xml, "placeholder-a1",
        "memory-budget blank overwrite strict-options reacquired post-noop save should not revive old A1 text");
    check_not_contains(memory_reacquired_post_noop_xml, R"(r="D4")",
        "memory-budget blank overwrite strict-options reacquired post-noop save should keep rejected D4 absent");
    check(fastxlsx::test::read_zip_entries(memory_reacquired_noop_output) == memory_reacquired_noop_entries,
        "memory-budget blank overwrite strict-options reacquired post-noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(memory_reacquired_second_noop_output) == memory_reacquired_second_noop_entries,
        "memory-budget blank overwrite strict-options reacquired post-noop save should leave the second noop output unchanged");
    check(memory_reacquired_noop_entries == memory_output_entries,
        "memory-budget blank overwrite strict-options reacquired post-noop save should leave the first noop output stable");
    check(memory_reacquired_second_noop_entries == memory_reacquired_noop_entries,
        "memory-budget blank overwrite strict-options reacquired post-noop save should leave the second noop output stable");
    check(fastxlsx::test::read_zip_entries(memory_output) == memory_output_entries,
        "memory-budget blank overwrite strict-options reacquired post-noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        memory_reacquired_post_noop_output,
        "memory-budget blank overwrite strict-options reacquired post-noop save",
        "mini-a1");

    const WorkbookEditorPublicCatalogSnapshot memory_catalog_before_noop =
        workbook_editor_public_catalog_snapshot(memory_editor);
    const WorkbookEditorPublicSaveStateSnapshot memory_save_state_before_noop =
        workbook_editor_public_save_state_snapshot(memory_editor);
    memory_editor.save_as(memory_noop_output);
    check(!memory_sheet.has_pending_changes(),
        "memory-budget blank overwrite noop save should keep the materialized session clean");
    check(memory_editor.pending_change_count() == 1,
        "memory-budget blank overwrite noop save should not add another handoff");
    check(memory_editor.pending_materialized_worksheet_names().empty(),
        "memory-budget blank overwrite noop save should not expose dirty worksheet names");
    check(memory_editor.pending_materialized_cell_count() == 0,
        "memory-budget blank overwrite noop save should not expose dirty materialized cells");
    check(memory_editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget blank overwrite noop save should not expose dirty materialized memory");
    check(memory_editor.pending_worksheet_edits().empty(),
        "memory-budget blank overwrite noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        memory_editor,
        "memory-budget blank overwrite noop save should not queue replacement diagnostics");
    check(!memory_editor.last_edit_error().has_value(),
        "memory-budget blank overwrite noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        memory_editor, memory_save_state_before_noop,
        "memory-budget blank overwrite noop save");
    check_workbook_editor_public_catalog_preserved(
        memory_editor, memory_catalog_before_noop,
        "memory-budget blank overwrite noop save");
    const auto memory_noop_entries =
        fastxlsx::test::read_zip_entries(memory_noop_output);
    check(memory_noop_entries == memory_output_entries,
        "memory-budget blank overwrite noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(memory_source) == memory_source_entries,
        "memory-budget blank overwrite noop save should leave the source package unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        memory_noop_output,
        memory_options,
        "memory-budget blank overwrite noop save");

    const WorkbookEditorPublicCatalogSnapshot memory_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(memory_editor);
    const WorkbookEditorPublicSaveStateSnapshot memory_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(memory_editor);
    memory_editor.save_as(memory_second_noop_output);
    check(!memory_sheet.has_pending_changes(),
        "memory-budget blank overwrite second noop save should keep the materialized session clean");
    check(memory_editor.pending_change_count() == 1,
        "memory-budget blank overwrite second noop save should not add another handoff");
    check(memory_editor.pending_materialized_worksheet_names().empty(),
        "memory-budget blank overwrite second noop save should not expose dirty worksheet names");
    check(memory_editor.pending_materialized_cell_count() == 0,
        "memory-budget blank overwrite second noop save should not expose dirty materialized cells");
    check(memory_editor.estimated_pending_materialized_memory_usage() == 0,
        "memory-budget blank overwrite second noop save should not expose dirty materialized memory");
    check(memory_editor.pending_worksheet_edits().empty(),
        "memory-budget blank overwrite second noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        memory_editor,
        "memory-budget blank overwrite second noop save should not queue replacement diagnostics");
    check(!memory_editor.last_edit_error().has_value(),
        "memory-budget blank overwrite second noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        memory_editor, memory_save_state_before_second_noop,
        "memory-budget blank overwrite second noop save");
    check_workbook_editor_public_catalog_preserved(
        memory_editor, memory_catalog_before_second_noop,
        "memory-budget blank overwrite second noop save");
    const auto memory_second_noop_entries =
        fastxlsx::test::read_zip_entries(memory_second_noop_output);
    check(memory_second_noop_entries == memory_noop_entries,
        "memory-budget blank overwrite second noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(memory_noop_output) == memory_noop_entries,
        "memory-budget blank overwrite second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(memory_source) == memory_source_entries,
        "memory-budget blank overwrite second noop save should leave the source package unchanged");
    check_reopened_blank_guardrail_overwrite_output(
        memory_second_noop_output,
        memory_options,
        "memory-budget blank overwrite second noop save");
}


void check_workbook_editor_public_clean_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check_workbook_editor_public_no_pending_state(editor, scenario);
    check_workbook_editor_no_replacement_diagnostics(editor, scenario);
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep last_edit_error empty");
}

void test_public_worksheet_editor_mutation_memory_budget_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mutation-memory-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-reacquired-post-noop-output.xlsx");
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
        "mutation memory-budget test precondition should use a missing target cell");

    bool failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("mutation-memory-oversized"));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "public WorksheetEditor mutation should expose memory budget diagnostics");
    }
    check(failed,
        "public WorksheetEditor should enforce memory_budget_bytes while mutating cells");
    check(editor.last_edit_error().has_value(),
        "failed memory-budget mutation should update last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore memory_budget_bytes guardrail exceeded",
            "last_edit_error should retain the mutation memory-budget diagnostic");
    }
    check(!sheet.has_pending_changes(),
        "failed memory-budget mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed memory-budget mutation should not dirty the editor");
    check_workbook_editor_public_no_pending_state(
        editor, "failed memory-budget mutation");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed memory-budget mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed memory-budget mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed memory-budget mutation should not expose dirty materialized memory");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "failed memory-budget mutation");
    check(sheet.cell_count() == baseline_count,
        "failed memory-budget mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed memory-budget mutation should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "failed memory-budget mutation should not leave the rejected cell readable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed memory-budget mutation should leave the source package unchanged");

    sheet.set_cell("A1", fastxlsx::CellValue::text("tiny"));
    check(!editor.last_edit_error().has_value(),
        "successful in-budget mutation should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful in-budget mutation should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful in-budget mutation should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite should keep the sparse cell count stable");
    const std::size_t recovered_memory = sheet.estimated_memory_usage();
    check(editor.estimated_pending_materialized_memory_usage() == recovered_memory,
        "successful overwrite should expose recovered materialized memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "mutation memory-budget recovery overwrite");

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mutation memory-budget recovery save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "tiny",
        "successful in-budget mutation should persist through save_as");
    check_not_contains(worksheet_xml, "mutation-memory-oversized",
        "rejected memory-budget mutation should not leak into saved output");
    check_reopened_default_data_overwrite_output(output, "mutation memory-budget recovery",
        "tiny");

    fastxlsx::WorkbookEditor reacquired_editor =
        fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Data", options);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation memory-budget recovery same-budget reacquire should keep the sheet clean");
    check(reacquired_sheet.cell_count() == baseline_count,
        "mutation memory-budget recovery same-budget reacquire should keep the sparse count");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "mutation memory-budget recovery same-budget reacquire should stay within budget");
    check(!reacquired_sheet.try_cell("D4").has_value(),
        "mutation memory-budget recovery same-budget reacquire should keep rejected D4 absent");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation memory-budget recovery same-budget reacquire");

    reacquired_editor.save_as(reacquired_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation memory-budget recovery same-budget reacquired noop save should keep the sheet clean");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "mutation memory-budget recovery same-budget reacquired noop save should stay within budget");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation memory-budget recovery same-budget reacquired noop save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "mutation memory-budget recovery same-budget reacquired noop output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation memory-budget recovery same-budget reacquired noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_noop_output,
        "mutation memory-budget recovery same-budget reacquired noop",
        "tiny");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation memory-budget recovery same-budget reacquired second noop save should keep the sheet clean");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "mutation memory-budget recovery same-budget reacquired second noop save should stay within budget");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation memory-budget recovery same-budget reacquired second noop save");
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor,
        reacquired_save_state_before_second_noop,
        "mutation memory-budget recovery same-budget reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor,
        reacquired_catalog_before_second_noop,
        "mutation memory-budget recovery same-budget reacquired second noop save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "mutation memory-budget recovery same-budget reacquired second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation memory-budget recovery same-budget reacquired second noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_second_noop_output,
        "mutation memory-budget recovery same-budget reacquired second noop",
        "tiny");

    reacquired_sheet.set_cell("A1", fastxlsx::CellValue::text("mini"));
    check(reacquired_sheet.has_pending_changes(),
        "mutation memory-budget recovery same-budget post-noop overwrite should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "mutation memory-budget recovery same-budget post-noop overwrite should dirty the editor");
    check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "mutation memory-budget recovery same-budget post-noop overwrite should stay within budget");
    check(reacquired_editor.pending_materialized_cell_count() == baseline_count,
        "mutation memory-budget recovery same-budget post-noop overwrite should keep sparse count stable");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mutation memory-budget recovery same-budget post-noop overwrite");
    check(!reacquired_editor.last_edit_error().has_value(),
        "mutation memory-budget recovery same-budget post-noop overwrite should keep diagnostics clear");
    check_public_state_single_data_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        0,
        "mutation memory-budget recovery same-budget post-noop overwrite");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation memory-budget recovery same-budget post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "mutation memory-budget recovery same-budget post-noop save should record one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "mutation memory-budget recovery same-budget post-noop save should clear dirty materialized names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "mutation memory-budget recovery same-budget post-noop save should clear dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "mutation memory-budget recovery same-budget post-noop save should clear dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "mutation memory-budget recovery same-budget post-noop save should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mutation memory-budget recovery same-budget post-noop save");
    check(!reacquired_editor.last_edit_error().has_value(),
        "mutation memory-budget recovery same-budget post-noop save should keep diagnostics clear");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, "mini",
        "mutation memory-budget recovery same-budget post-noop save should persist the overwrite");
    check_not_contains(reacquired_post_noop_xml, "mutation-memory-oversized",
        "mutation memory-budget recovery same-budget post-noop save should not revive rejected D4");
    check(reacquired_noop_entries == output_entries,
        "mutation memory-budget recovery same-budget post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) ==
            reacquired_second_noop_entries,
        "mutation memory-budget recovery same-budget post-noop save should leave the second no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation memory-budget recovery same-budget post-noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_post_noop_output,
        "mutation memory-budget recovery same-budget post-noop",
        "mini");

    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-memory-second-noop-output.xlsx");
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "successful memory-budget recovery noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "successful memory-budget recovery noop save should not add a second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful memory-budget recovery noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "successful memory-budget recovery noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "successful memory-budget recovery noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "successful memory-budget recovery noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "successful memory-budget recovery noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "mutation memory-budget recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "mutation memory-budget recovery noop save");

    const auto noop_output_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_output_entries == output_entries,
        "successful memory-budget recovery noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "successful memory-budget recovery noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(noop_output, "mutation memory-budget recovery noop",
        "tiny");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "successful memory-budget recovery second noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "successful memory-budget recovery second noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful memory-budget recovery second noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "successful memory-budget recovery second noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "successful memory-budget recovery second noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "successful memory-budget recovery second noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "successful memory-budget recovery second noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "mutation memory-budget recovery second noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "mutation memory-budget recovery second noop save");

    const auto second_noop_output_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_output_entries == noop_output_entries,
        "successful memory-budget recovery second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_output_entries,
        "successful memory-budget recovery second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "successful memory-budget recovery second noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(second_noop_output,
        "mutation memory-budget recovery second noop",
        "tiny");
}

void test_public_worksheet_editor_mutation_max_cells_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-second-noop-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mutation-max-cells-reacquired-post-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = sizing_sheet.cell_count();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = exact_max_cells;
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);

    const std::size_t baseline_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(!sheet.try_cell("D4").has_value(),
        "mutation max-cells test precondition should use a missing target cell");

    bool failed = false;
    try {
        sheet.set_cell("D4", fastxlsx::CellValue::text("mutation-max-cells-rejected"));
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore max_cells guardrail exceeded",
            "public WorksheetEditor mutation should expose max_cells diagnostics");
    }
    check(failed, "public WorksheetEditor should enforce max_cells while mutating cells");
    check(editor.last_edit_error().has_value(),
        "failed max_cells mutation should update last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "CellStore max_cells guardrail exceeded",
            "last_edit_error should retain the mutation max_cells diagnostic");
    }
    check(!sheet.has_pending_changes(),
        "failed max_cells mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed max_cells mutation should not dirty the editor");
    check_workbook_editor_public_no_pending_state(
        editor, "failed max_cells mutation");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed max_cells mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed max_cells mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed max_cells mutation should not expose dirty materialized memory");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "failed max_cells mutation");
    check(sheet.cell_count() == baseline_count,
        "failed max_cells mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed max_cells mutation should preserve sparse memory estimate");
    check(!sheet.try_cell("D4").has_value(),
        "failed max_cells mutation should not leave the rejected cell readable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed max_cells mutation should leave the source package unchanged");

    sheet.set_cell("A1", fastxlsx::CellValue::text("after-max-cells-overwrite"));
    check(!editor.last_edit_error().has_value(),
        "successful overwrite under max_cells should clear last_edit_error");
    check(sheet.has_pending_changes(),
        "successful overwrite under max_cells should dirty the materialized session");
    check(editor.has_pending_changes(),
        "successful overwrite under max_cells should dirty the editor");
    check(editor.pending_materialized_cell_count() == baseline_count,
        "successful overwrite under max_cells should keep the sparse cell count stable");
    const std::size_t recovered_memory = sheet.estimated_memory_usage();
    check(editor.estimated_pending_materialized_memory_usage() == recovered_memory,
        "successful overwrite under max_cells should expose recovered materialized memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "mutation max-cells recovery overwrite");

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "mutation max-cells recovery save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-max-cells-overwrite",
        "successful overwrite under max_cells should persist through save_as");
    check_not_contains(worksheet_xml, "mutation-max-cells-rejected",
        "rejected max_cells mutation should not leak into saved output");
    check_reopened_default_data_overwrite_output(output, "mutation max-cells recovery",
        "after-max-cells-overwrite");

    fastxlsx::WorkbookEditor reacquired_editor =
        fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reacquired_sheet =
        reacquired_editor.worksheet("Data", options);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation max-cells recovery same-budget reacquire should keep the sheet clean");
    check(reacquired_sheet.cell_count() == baseline_count,
        "mutation max-cells recovery same-budget reacquire should keep the sparse count");
    check(!reacquired_sheet.try_cell("D4").has_value(),
        "mutation max-cells recovery same-budget reacquire should keep rejected D4 absent");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation max-cells recovery same-budget reacquire");

    reacquired_editor.save_as(reacquired_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation max-cells recovery same-budget reacquired noop save should keep the sheet clean");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation max-cells recovery same-budget reacquired noop save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "mutation max-cells recovery same-budget reacquired noop output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation max-cells recovery same-budget reacquired noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_noop_output,
        "mutation max-cells recovery same-budget reacquired noop",
        "after-max-cells-overwrite");

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(reacquired_editor);
    reacquired_editor.save_as(reacquired_second_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation max-cells recovery same-budget reacquired second noop save should keep the sheet clean");
    check_workbook_editor_public_clean_state(
        reacquired_editor,
        "mutation max-cells recovery same-budget reacquired second noop save");
    check_workbook_editor_public_save_state_preserved(
        reacquired_editor,
        reacquired_save_state_before_second_noop,
        "mutation max-cells recovery same-budget reacquired second noop save");
    check_workbook_editor_public_catalog_preserved(
        reacquired_editor,
        reacquired_catalog_before_second_noop,
        "mutation max-cells recovery same-budget reacquired second noop save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "mutation max-cells recovery same-budget reacquired second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation max-cells recovery same-budget reacquired second noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_second_noop_output,
        "mutation max-cells recovery same-budget reacquired second noop",
        "after-max-cells-overwrite");

    reacquired_sheet.set_cell(
        "A1", fastxlsx::CellValue::text("after-max-cells-reacquire"));
    check(reacquired_sheet.has_pending_changes(),
        "mutation max-cells recovery same-budget post-noop overwrite should dirty the sheet");
    check(reacquired_editor.has_pending_changes(),
        "mutation max-cells recovery same-budget post-noop overwrite should dirty the editor");
    check(reacquired_editor.pending_materialized_cell_count() == baseline_count,
        "mutation max-cells recovery same-budget post-noop overwrite should keep sparse count stable");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mutation max-cells recovery same-budget post-noop overwrite");
    check(!reacquired_editor.last_edit_error().has_value(),
        "mutation max-cells recovery same-budget post-noop overwrite should keep diagnostics clear");
    check_public_state_single_data_dirty_materialized_summary(
        reacquired_editor,
        reacquired_sheet,
        0,
        "mutation max-cells recovery same-budget post-noop overwrite");

    reacquired_editor.save_as(reacquired_post_noop_output);
    check(!reacquired_sheet.has_pending_changes(),
        "mutation max-cells recovery same-budget post-noop save should clean the sheet");
    check(reacquired_editor.pending_change_count() == 1,
        "mutation max-cells recovery same-budget post-noop save should record one materialized handoff");
    check(reacquired_editor.pending_materialized_worksheet_names().empty(),
        "mutation max-cells recovery same-budget post-noop save should clear dirty materialized names");
    check(reacquired_editor.pending_materialized_cell_count() == 0,
        "mutation max-cells recovery same-budget post-noop save should clear dirty materialized cells");
    check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
        "mutation max-cells recovery same-budget post-noop save should clear dirty materialized memory");
    check(reacquired_editor.pending_worksheet_edits().empty(),
        "mutation max-cells recovery same-budget post-noop save should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        reacquired_editor,
        "mutation max-cells recovery same-budget post-noop save");
    check(!reacquired_editor.last_edit_error().has_value(),
        "mutation max-cells recovery same-budget post-noop save should keep diagnostics clear");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, "after-max-cells-reacquire",
        "mutation max-cells recovery same-budget post-noop save should persist the overwrite");
    check_not_contains(reacquired_post_noop_xml, "mutation-max-cells-rejected",
        "mutation max-cells recovery same-budget post-noop save should not revive rejected D4");
    check(reacquired_noop_entries == output_entries,
        "mutation max-cells recovery same-budget post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) ==
            reacquired_second_noop_entries,
        "mutation max-cells recovery same-budget post-noop save should leave the second no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "mutation max-cells recovery same-budget post-noop save should leave the saved input unchanged");
    check_reopened_default_data_overwrite_output(
        reacquired_post_noop_output,
        "mutation max-cells recovery same-budget post-noop",
        "after-max-cells-reacquire");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "successful max-cells recovery noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "successful max-cells recovery noop save should not add a second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful max-cells recovery noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "successful max-cells recovery noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "successful max-cells recovery noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "successful max-cells recovery noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "successful max-cells recovery noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "mutation max-cells recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "mutation max-cells recovery noop save");

    const auto noop_output_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_output_entries == output_entries,
        "successful max-cells recovery noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "successful max-cells recovery noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(noop_output, "mutation max-cells recovery noop",
        "after-max-cells-overwrite");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "successful max-cells recovery second noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "successful max-cells recovery second noop save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful max-cells recovery second noop save should not leave dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "successful max-cells recovery second noop save should not leave dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "successful max-cells recovery second noop save should not leave dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "successful max-cells recovery second noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "successful max-cells recovery second noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "mutation max-cells recovery second noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "mutation max-cells recovery second noop save");

    const auto second_noop_output_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_output_entries == noop_output_entries,
        "successful max-cells recovery second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_output_entries,
        "successful max-cells recovery second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "successful max-cells recovery second noop save should leave the source package unchanged");
    check_reopened_default_data_overwrite_output(second_noop_output,
        "mutation max-cells recovery second noop",
        "after-max-cells-overwrite");
}


int main()
{
    try {
        test_public_worksheet_editor_set_cell_replacement_drops_source_style();
        test_public_worksheet_editor_set_cell_accepts_default_style_id_as_unstyled();
        test_public_worksheet_editor_set_cell_value_accepts_default_style_id_as_style_preserving_value_edit();
        test_public_worksheet_editor_set_cell_value_style_rejection_noop_save();
        test_public_worksheet_editor_set_cell_value_validation_and_max_cells_noop_save();
        test_public_worksheet_editor_set_cell_value_memory_budget_failure_preserves_state();
        test_public_worksheet_editor_style_rejection_preserves_dirty_session();
        test_public_worksheet_editor_blank_insertions_obey_guardrail_budgets();
        test_public_worksheet_editor_mutation_memory_budget_failure_preserves_state();
        test_public_worksheet_editor_mutation_max_cells_failure_preserves_state();
        std::cout << "WorkbookEditor public-state set cell tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state set cell test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
