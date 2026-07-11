#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
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

struct EditorPublicStateSnapshot {
    std::vector<std::string> source_names;
    std::vector<std::string> planned_names;
    std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog;
    bool has_pending_changes{};
    bool has_unsaved_changes{};
    std::size_t unsaved_change_count{};
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

EditorPublicStateSnapshot snapshot(const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.source_worksheet_names(), editor.worksheet_names(),
        editor.worksheet_catalog(), editor.has_pending_changes(),
        editor.has_unsaved_changes(), editor.unsaved_change_count(),
        editor.pending_change_count(), editor.pending_materialized_worksheet_names(),
        editor.pending_materialized_cell_count(),
        editor.estimated_pending_materialized_memory_usage(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.pending_targeted_cell_replacement_count(),
        editor.pending_targeted_cell_replacement_worksheet_names(),
        editor.estimated_pending_targeted_cell_replacement_xml_bytes(),
        editor.last_edit_error(), editor.pending_worksheet_edits(),
    };
}

void check_snapshot_preserved(const fastxlsx::WorkbookEditor& editor,
    const EditorPublicStateSnapshot& before, std::string_view scenario)
{
    const EditorPublicStateSnapshot after = snapshot(editor);
    const std::string prefix(scenario);
    check(after.source_names == before.source_names
            && after.planned_names == before.planned_names
            && catalog_entries_equal(after.catalog, before.catalog),
        prefix + " should preserve worksheet catalog state");
    check(after.has_pending_changes == before.has_pending_changes
            && after.has_unsaved_changes == before.has_unsaved_changes
            && after.unsaved_change_count == before.unsaved_change_count
            && after.pending_change_count == before.pending_change_count,
        prefix + " should preserve pending and unsaved state");
    check(after.materialized_names == before.materialized_names
            && after.materialized_cell_count == before.materialized_cell_count
            && after.materialized_memory == before.materialized_memory,
        prefix + " should preserve materialized diagnostics");
    check(after.replacement_cell_count == before.replacement_cell_count
            && after.replacement_memory == before.replacement_memory
            && after.replacement_names == before.replacement_names
            && after.targeted_cell_count == before.targeted_cell_count
            && after.targeted_names == before.targeted_names
            && after.targeted_xml_bytes == before.targeted_xml_bytes,
        prefix + " should preserve replacement diagnostics");
    check(after.last_edit_error == before.last_edit_error,
        prefix + " should preserve last_edit_error");
    check(edit_summaries_equal(after.summaries, before.summaries),
        prefix + " should preserve worksheet summaries");
}

std::filesystem::path artifact(std::string_view filename)
{
    return fastxlsx::test::artifact_path(filename);
}

std::filesystem::path write_source(std::string_view filename)
{
    const std::filesystem::path path = artifact(filename);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("placeholder-a1"),
        fastxlsx::CellView::number(1.0)});
    data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep-me"),
        fastxlsx::CellView::number(99.0)});
    writer.close();
    return path;
}

void check_bounds(const fastxlsx::WorksheetEditor& sheet,
    std::uint32_t first_row, std::uint32_t first_column,
    std::uint32_t last_row, std::uint32_t last_column,
    std::string_view scenario)
{
    const auto range = sheet.used_range();
    check(range.has_value() && range->first_row == first_row
            && range->first_column == first_column
            && range->last_row == last_row
            && range->last_column == last_column,
        std::string(scenario) + " should expose expected bounds");
}

void check_clean_saved_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& expected_error,
    std::size_t expected_pending_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 0
            && editor.pending_change_count() == expected_pending_count,
        prefix + " should retain a clean materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should expose no dirty materialized diagnostics");
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty()
            && editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should expose no replacement diagnostics");
    check(editor.last_edit_error() == expected_error,
        prefix + " should preserve expected diagnostics");
}

void check_reopened_output(const std::filesystem::path& path,
    std::string_view scenario, auto&& inspect)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened.has_unsaved_changes()
            && reopened.pending_change_count() == 0
            && !sheet.has_pending_changes() && !reopened.last_edit_error().has_value(),
        std::string(scenario) + " should reopen clean");
    inspect(sheet);
}

void check_two_noop_saves(fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& source,
    const auto& source_entries,
    const auto& output_entries,
    const std::filesystem::path& noop_output,
    const std::filesystem::path& second_noop_output,
    const std::optional<std::string>& expected_error,
    std::string_view scenario,
    auto&& inspect)
{
    const std::string prefix(scenario);
    const std::size_t pending_count = editor.pending_change_count();
    check(pending_count == 1,
        prefix + " should start with one retained handoff");
    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop, prefix + " first no-op save");
    check_clean_saved_state(
        editor, sheet, expected_error, pending_count, prefix + " first no-op save");
    inspect(sheet);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        prefix + " first no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " first no-op should preserve source package");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check_snapshot_preserved(editor, before_second_noop,
        prefix + " second no-op save");
    check_clean_saved_state(editor, sheet, expected_error, pending_count,
        prefix + " second no-op save");
    inspect(sheet);
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        prefix + " second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " second no-op should preserve prior files");
    check_reopened_output(second_noop_output, prefix + " second no-op reopen", inspect);
}

void check_empty_projection(fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 0 && sheet.sparse_cells().empty()
            && sheet.row_cells(1).empty() && sheet.column_cells(1).empty()
            && !sheet.contains_cell("A1") && !sheet.try_cell("A1").has_value()
            && !sheet.used_range().has_value(),
        prefix + " should expose an empty sparse projection");
}

void test_used_range_tracks_sparse_bounds()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-used-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-used-range-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-used-range-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-used-range-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check_bounds(sheet, 1, 1, 2, 2, "source used_range");
    check(!sheet.has_pending_changes() && !editor.last_edit_error().has_value(),
        "source used_range inspection should remain clean");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("used-range-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    check_bounds(sheet, 1, 1, 4, 4, "expanded used_range");
    sheet.erase_cell(4, 4);
    check_bounds(sheet, 1, 1, 3, 2, "shrunk used_range");
    sheet.erase_cell(3, 2);
    check_bounds(sheet, 1, 1, 1, 2, "source-edge used_range");
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-used-range-sentinel"));
    }), "invalid mutation should seed used_range diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    const EditorPublicStateSnapshot before_inspection = snapshot(editor);
    check_bounds(sheet, 1, 1, 1, 2, "used_range after failed mutation");
    check_snapshot_preserved(editor, before_inspection,
        "used_range after failed mutation");

    sheet.erase_cells(fastxlsx::CellRange {1, 1, 1, 2});
    check(!editor.last_edit_error().has_value(),
        "successful erase should clear used_range diagnostic");
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-empty-used-range"));
    }), "invalid mutation should seed empty used_range diagnostic");
    const std::optional<std::string> empty_error = editor.last_edit_error();
    const EditorPublicStateSnapshot before_empty_inspection = snapshot(editor);
    check_empty_projection(sheet, "empty used_range inspection");
    check_snapshot_preserved(editor, before_empty_inspection,
        "empty used_range inspection");

    const auto inspect = [](fastxlsx::WorksheetEditor& inspected) {
        check_empty_projection(inspected, "saved empty projection");
        check(!inspected.try_cell("B1").has_value()
                && !inspected.try_cell("A2").has_value(),
            "saved empty projection should keep all source records erased");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, empty_error, 1, "used_range save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, R"(<dimension ref="A1"/>)",
        "empty used_range save should emit fallback dimension");
    check_not_contains(xml, "placeholder-a1",
        "empty used_range save should omit erased A1");
    check_not_contains(xml, R"(r="B1")",
        "empty used_range save should omit erased B1");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "empty used_range save should preserve untouched sheet");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "used_range save should preserve source package");
    check_reopened_output(output, "used_range reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, empty_error, "used_range", inspect);
}

void check_contains_projection(fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 4 && sheet.contains_cell("A1")
            && sheet.contains_cell("B3") && sheet.contains_cell("D4")
            && !sheet.contains_cell("A2"),
        prefix + " should expose represented and erased cells");
    check_bounds(sheet, 1, 1, 4, 4, scenario);
    check(sheet.get_cell("A1").text_value() == "placeholder-a1"
            && sheet.get_cell("B1").number_value() == 1.0
            && sheet.get_cell("B3").kind() == fastxlsx::CellValueKind::Blank
            && sheet.get_cell("D4").text_value() == "contains-new",
        prefix + " should retain represented values");
}

void test_contains_cell_tracks_represented_state()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-contains-cell-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-contains-cell-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-contains-cell-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-contains-cell-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(sheet.contains_cell(1, 1) && sheet.contains_cell("B1")
            && !sheet.contains_cell(5, 5)
            && !sheet.contains_cell("XFD1048576")
            && !sheet.has_pending_changes() && !editor.last_edit_error().has_value(),
        "contains_cell should inspect source and legal missing coordinates cleanly");
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("contains-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    check(sheet.contains_cell(4, 4) && sheet.contains_cell("B3")
            && !sheet.contains_cell(2, 1)
            && sheet.try_cell("B3")->kind() == fastxlsx::CellValueKind::Blank,
        "contains_cell should track inserted, blank, and erased records");
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-contains-cell-sentinel"));
    }), "invalid mutation should seed contains_cell diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    const EditorPublicStateSnapshot before_inspection = snapshot(editor);
    check(sheet.contains_cell("D4") && !sheet.contains_cell(2, 1),
        "contains_cell should remain usable after failed mutation");
    check_snapshot_preserved(editor, before_inspection,
        "contains_cell after failed mutation");
    const std::size_t count_before_invalid = sheet.cell_count();
    const std::size_t memory_before_invalid = sheet.estimated_memory_usage();
    check(throws_fastxlsx_error([&] { (void)sheet.contains_cell(0, 1); })
            && throws_fastxlsx_error([&] { (void)sheet.contains_cell(1, 16385); })
            && throws_fastxlsx_error([&] { (void)sheet.contains_cell("a1"); })
            && throws_fastxlsx_error([&] { (void)sheet.contains_cell("A1:B2"); }),
        "contains_cell should reject invalid coordinates and references");
    check(sheet.cell_count() == count_before_invalid
            && sheet.estimated_memory_usage() == memory_before_invalid,
        "invalid contains_cell calls should preserve sparse state");
    check_snapshot_preserved(editor, before_inspection,
        "invalid contains_cell inspections");

    const auto inspect = [](fastxlsx::WorksheetEditor& inspected) {
        check_contains_projection(inspected, "contains_cell saved projection");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, prior_error, 1, "contains_cell save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "contains-new",
        "contains_cell save should retain inserted D4");
    check_not_contains(xml, "placeholder-a2",
        "contains_cell save should omit erased A2");
    check_not_contains(xml, "invalid-contains-cell-sentinel",
        "contains_cell save should omit rejected payload");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "contains_cell save should preserve source package");
    check_reopened_output(output, "contains_cell reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, prior_error, "contains_cell", inspect);
}

void check_row_column_projection(fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    const auto row_one = sheet.row_cells(1);
    check(row_one.size() == 3
            && row_one[0].reference.column == 1
            && row_one[0].value.text_value() == "changed-after-row-column-snapshot"
            && row_one[1].reference.column == 2
            && row_one[1].value.number_value() == 1.0
            && row_one[2].reference.column == 3
            && row_one[2].value.kind() == fastxlsx::CellValueKind::Blank,
        prefix + " should expose row one in column order");
    const auto column_one = sheet.column_cells(1);
    check(column_one.size() == 3
            && column_one[0].reference.row == 1
            && column_one[0].value.text_value() == "changed-after-row-column-snapshot"
            && column_one[1].reference.row == 2
            && column_one[1].value.text_value() == "placeholder-a2"
            && column_one[2].reference.row == 3
            && column_one[2].value.text_value() == "column-new",
        prefix + " should expose column one in row order");
    const auto cells = sheet.sparse_cells();
    check(sheet.cell_count() == 6 && cells.size() == 6
            && cells[4].reference.row == 3 && cells[4].reference.column == 1
            && cells[4].value.text_value() == "column-new"
            && cells[5].reference.row == 4 && cells[5].reference.column == 4
            && cells[5].value.text_value() == "outside-row-column",
        prefix + " should retain full sparse ordering and outside D4");
    check(sheet.row_cells(3).size() == 1 && sheet.row_cells(4).size() == 1
            && sheet.column_cells(4).size() == 1,
        prefix + " should expose inserted and outside row/column records");
    check_bounds(sheet, 1, 1, 4, 4, scenario);
}

void test_row_and_column_cells_snapshot()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-row-column-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-row-column-cells-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-row-column-cells-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-cells-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 3, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 1, fastxlsx::CellValue::text("column-new"));
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("outside-row-column"));
    const auto row_one = sheet.row_cells(1);
    const auto column_one = sheet.column_cells(1);
    check(row_one.size() == 3 && row_one[0].value.text_value() == "placeholder-a1"
            && row_one[1].value.number_value() == 1.0
            && row_one[2].value.kind() == fastxlsx::CellValueKind::Blank,
        "row_cells should include source and blank records in column order");
    check(sheet.row_cells(2).size() == 1 && sheet.row_cells(99).empty(),
        "row_cells should expose represented rows without synthesizing missing rows");
    check(column_one.size() == 3
            && column_one[0].value.text_value() == "placeholder-a1"
            && column_one[1].value.text_value() == "placeholder-a2"
            && column_one[2].value.text_value() == "column-new",
        "column_cells should include source and edit records in row order");
    check(sheet.column_cells(2).size() == 1 && sheet.column_cells(99).empty(),
        "column_cells should expose represented columns without synthesizing missing columns");
    sheet.set_cell(1, 1,
        fastxlsx::CellValue::text("changed-after-row-column-snapshot"));
    check(row_one[0].value.text_value() == "placeholder-a1"
            && column_one[0].value.text_value() == "placeholder-a1",
        "row and column snapshots should own their values");
    check(!editor.last_edit_error().has_value(),
        "row and column snapshots should not update diagnostics");

    const auto inspect = [](fastxlsx::WorksheetEditor& inspected) {
        check_row_column_projection(inspected, "row/column saved projection");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, std::nullopt, 1, "row/column save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "changed-after-row-column-snapshot",
        "row/column save should persist post-snapshot edit");
    check_contains(xml, "outside-row-column",
        "row/column save should retain outside D4");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column save should preserve source package");
    check_reopened_output(output, "row/column reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, std::nullopt, "row/column snapshots", inspect);
}

void check_source_projection(fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    const auto cells = sheet.sparse_cells();
    check(sheet.cell_count() == 3 && cells.size() == 3
            && cells[0].reference.row == 1 && cells[0].reference.column == 1
            && cells[0].value.kind() == fastxlsx::CellValueKind::Text
            && cells[0].value.text_value() == "placeholder-a1"
            && cells[1].reference.row == 1 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Number
            && cells[1].value.number_value() == 1.0
            && cells[2].reference.row == 2 && cells[2].reference.column == 1
            && cells[2].value.kind() == fastxlsx::CellValueKind::Text
            && cells[2].value.text_value() == "placeholder-a2",
        prefix + " should retain source sparse ordering and values");
    const auto row_one = sheet.row_cells(1);
    const auto column_one = sheet.column_cells(1);
    check(row_one.size() == 2
            && row_one[0].value.text_value() == "placeholder-a1"
            && row_one[1].value.number_value() == 1.0,
        prefix + " should retain row-one snapshots");
    check(column_one.size() == 2
            && column_one[0].value.text_value() == "placeholder-a1"
            && column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should retain column-one snapshots");
    check_bounds(sheet, 1, 1, 2, 2, scenario);
    check(!sheet.contains_cell("C3") && !sheet.try_cell("C3").has_value(),
        prefix + " should keep missing C3 absent");
}

void check_clean_no_pending_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& expected_error,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 0
            && editor.pending_change_count() == 0
            && !sheet.has_pending_changes(),
        prefix + " should remain clean with no handoff");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty()
            && editor.pending_replacement_cell_count() == 0
            && editor.pending_targeted_cell_replacement_count() == 0,
        prefix + " should expose no pending diagnostics");
    check(editor.last_edit_error() == expected_error,
        prefix + " should preserve expected last_edit_error");
}

void test_row_and_column_invalid_reads_preserve_diagnostics()
{
    const std::filesystem::path source = write_source(
        "fastxlsx-workbook-editor-public-row-column-cells-invalid-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-row-column-cells-invalid-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-cells-invalid-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::text("invalid-row-column-read-sentinel"));
    }), "invalid mutation should seed row/column read diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should record row/column read diagnostic");
    const EditorPublicStateSnapshot before_reads = snapshot(editor);
    const auto check_invalid_read = [&](auto&& action, std::string_view scenario) {
        check(throws_fastxlsx_error(action),
            std::string(scenario) + " should reject invalid coordinate");
        check_snapshot_preserved(editor, before_reads, scenario);
        check_clean_no_pending_state(editor, sheet, prior_error, scenario);
        check(sheet.cell_count() == 3
                && sheet.estimated_memory_usage() == baseline_memory,
            std::string(scenario) + " should preserve sparse state");
    };
    check_invalid_read([&] { (void)sheet.row_cells(0); }, "row_cells row zero");
    check_invalid_read([&] { (void)sheet.row_cells(1048577); },
        "row_cells row overflow");
    check_invalid_read([&] { (void)sheet.column_cells(0); },
        "column_cells column zero");
    check_invalid_read([&] { (void)sheet.column_cells(16385); },
        "column_cells column overflow");
    check(sheet.row_cells(10).empty() && sheet.column_cells(10).empty(),
        "valid missing row and column reads should return empty snapshots");
    check_snapshot_preserved(editor, before_reads,
        "valid missing row and column reads");
    check_source_projection(sheet, "row/column invalid reads live session");

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save,
        "row/column invalid reads first no-op save");
    check_clean_no_pending_state(editor, sheet, prior_error,
        "row/column invalid reads first saved session");
    check_source_projection(sheet, "row/column invalid reads first saved session");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "row/column invalid reads first output should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column invalid reads first save should preserve source package");
    check_reopened_output(output, "row/column invalid reads first reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check_source_projection(reopened, "row/column invalid reads reopened source");
        });

    const EditorPublicStateSnapshot before_second_save = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_second_save,
        "row/column invalid reads second no-op save");
    check_clean_no_pending_state(editor, sheet, prior_error,
        "row/column invalid reads second saved session");
    check_source_projection(sheet, "row/column invalid reads second saved session");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries
            && fastxlsx::test::read_zip_entries(output) == output_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column invalid reads second save should preserve all files");
    check_reopened_output(noop_output, "row/column invalid reads second reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check_source_projection(reopened, "row/column invalid reads second reopened source");
        });
}

void check_source_range_projections(
    fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    const auto check_three = [&](const auto& cells, std::string_view view) {
        check(cells.size() == 3
                && cells[0].reference.row == 1 && cells[0].reference.column == 1
                && cells[0].value.text_value() == "placeholder-a1"
                && cells[1].reference.row == 1 && cells[1].reference.column == 2
                && cells[1].value.number_value() == 1.0
                && cells[2].reference.row == 2 && cells[2].reference.column == 1
                && cells[2].value.text_value() == "placeholder-a2",
            prefix + " " + std::string(view) + " should retain source cells");
    };
    check_three(sheet.sparse_cells(), "full snapshot");
    check_three(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}),
        "CellRange snapshot");
    check_three(sheet.sparse_cells("A1:B2"), "A1 range snapshot");
    const std::array<fastxlsx::WorksheetCellReference, 4> requested {
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {3, 3}};
    check_three(sheet.sparse_cells(requested), "requested snapshot");
    check_three(sheet.sparse_cells({{1, 1}, {1, 2}, {2, 1}, {3, 3}}),
        "initializer snapshot");
    check(sheet.sparse_cells(fastxlsx::CellRange {3, 3, 4, 4}).empty()
            && sheet.sparse_cells("C3:D4").empty(),
        prefix + " should keep missing ranges empty");
}

void test_sparse_cells_invalid_ranges_preserve_diagnostics()
{
    const std::filesystem::path source = write_source(
        "fastxlsx-workbook-editor-public-sparse-range-error-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-sparse-range-error-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-sparse-range-error-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-range-sentinel"));
    }), "invalid mutation should seed sparse-range diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should record sparse-range diagnostic");
    if (prior_error.has_value()) {
        check_contains(*prior_error, "WorksheetEditor cell coordinate is invalid",
            "sparse-range prior diagnostic should identify invalid coordinate");
    }
    const EditorPublicStateSnapshot before_reads = snapshot(editor);
    const std::array<fastxlsx::CellRange, 4> invalid_ranges {
        fastxlsx::CellRange {2, 1, 1, 2},
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385}};
    for (const fastxlsx::CellRange invalid : invalid_ranges) {
        check(throws_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid); }),
            "invalid CellRange snapshot should throw");
        check_snapshot_preserved(editor, before_reads,
            "invalid CellRange snapshot");
    }
    const std::array<std::string_view, 6> invalid_references {
        "a1:B2", "A1:B2:C3", "B2:A1", "A:C", "$A$1:$B$2", "Data!A1:B2"};
    for (const std::string_view invalid : invalid_references) {
        check(throws_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid); }),
            "invalid A1 range snapshot should throw");
        check_snapshot_preserved(editor, before_reads,
            "invalid A1 range snapshot");
    }
    check_clean_no_pending_state(editor, sheet, prior_error,
        "invalid sparse-range reads");
    check(sheet.cell_count() == 3 && sheet.estimated_memory_usage() == baseline_memory,
        "invalid sparse-range reads should preserve sparse state");
    check_source_range_projections(sheet, "valid reads after invalid sparse ranges");
    check_snapshot_preserved(editor, before_reads,
        "valid reads after invalid sparse ranges");

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save,
        "invalid sparse ranges first no-op save");
    check_clean_no_pending_state(editor, sheet, prior_error,
        "invalid sparse ranges first saved session");
    check_source_range_projections(sheet, "invalid sparse ranges first saved session");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid sparse ranges first save should copy and preserve source");
    check_reopened_output(output, "invalid sparse ranges first reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check_source_projection(reopened, "invalid sparse ranges reopened source");
            check_source_range_projections(reopened, "invalid sparse ranges reopened ranges");
        });

    const EditorPublicStateSnapshot before_second_save = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_second_save,
        "invalid sparse ranges second no-op save");
    check_clean_no_pending_state(editor, sheet, prior_error,
        "invalid sparse ranges second saved session");
    check_source_range_projections(sheet, "invalid sparse ranges second saved session");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries
            && fastxlsx::test::read_zip_entries(output) == output_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid sparse ranges second save should preserve all files");
    check_reopened_output(noop_output, "invalid sparse ranges second reopen",
        [](fastxlsx::WorksheetEditor& reopened) {
            check_source_range_projections(reopened,
                "invalid sparse ranges second reopened ranges");
        });
}

} // namespace

int main()
{
    try {
        test_used_range_tracks_sparse_bounds();
        test_contains_cell_tracks_represented_state();
        test_row_and_column_cells_snapshot();
        test_row_and_column_invalid_reads_preserve_diagnostics();
        test_sparse_cells_invalid_ranges_preserve_diagnostics();
        std::cout << "WorkbookEditor public-state sparse inspection tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state sparse inspection test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
