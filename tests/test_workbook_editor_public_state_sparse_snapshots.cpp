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
        editor.source_worksheet_names(),
        editor.worksheet_names(),
        editor.worksheet_catalog(),
        editor.has_pending_changes(),
        editor.has_unsaved_changes(),
        editor.unsaved_change_count(),
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

void check_clean_saved_state(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::size_t expected_pending_count,
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
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !editor.last_edit_error().has_value(),
        prefix + " should expose no replacement or error diagnostics");
}

void check_reopened_output(const std::filesystem::path& path,
    std::string_view scenario, auto&& inspect)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened.has_unsaved_changes()
            && reopened.unsaved_change_count() == 0
            && reopened.pending_change_count() == 0
            && !sheet.has_pending_changes(),
        std::string(scenario) + " should reopen clean");
    check(reopened.pending_materialized_worksheet_names().empty()
            && reopened.pending_materialized_cell_count() == 0
            && reopened.estimated_pending_materialized_memory_usage() == 0
            && reopened.pending_worksheet_edits().empty()
            && reopened.pending_replacement_cell_count() == 0
            && reopened.pending_targeted_cell_replacement_count() == 0
            && !reopened.last_edit_error().has_value(),
        std::string(scenario) + " should reopen without pending diagnostics");
    inspect(sheet);
}

void check_two_noop_saves(fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& sheet,
    const std::filesystem::path& source,
    const auto& source_entries,
    const auto& output_entries,
    const std::filesystem::path& noop_output,
    const std::filesystem::path& second_noop_output,
    std::string_view scenario,
    auto&& inspect)
{
    const std::string prefix(scenario);
    const std::size_t pending_count = editor.pending_change_count();
    check(pending_count == 1,
        prefix + " should start no-op saves with one retained handoff");
    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop, prefix + " first no-op save");
    check_clean_saved_state(editor, sheet, pending_count, prefix + " first no-op save");
    inspect(sheet);
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        prefix + " first no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " first no-op save should preserve source package");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check_snapshot_preserved(
        editor, before_second_noop, prefix + " second no-op save");
    check_clean_saved_state(
        editor, sheet, pending_count, prefix + " second no-op save");
    inspect(sheet);
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        prefix + " second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        prefix + " second no-op save should preserve prior files");
    check_reopened_output(second_noop_output, prefix + " second no-op reopen", inspect);
}

void check_bounds(const fastxlsx::WorksheetEditor& sheet,
    std::uint32_t first_row, std::uint32_t first_column,
    std::uint32_t last_row, std::uint32_t last_column,
    std::string_view scenario)
{
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == first_row
            && range->first_column == first_column
            && range->last_row == last_row
            && range->last_column == last_column,
        std::string(scenario) + " should expose expected sparse bounds");
}

void check_full_snapshot_cells(
    const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
    std::string_view a1_text, std::string_view scenario)
{
    check(cells.size() == 4
            && cells[0].reference.row == 1 && cells[0].reference.column == 1
            && cells[0].value.kind() == fastxlsx::CellValueKind::Text
            && cells[0].value.text_value() == a1_text
            && cells[1].reference.row == 1 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Number
            && cells[1].value.number_value() == 1.0
            && cells[2].reference.row == 3 && cells[2].reference.column == 2
            && cells[2].value.kind() == fastxlsx::CellValueKind::Blank
            && cells[3].reference.row == 4 && cells[3].reference.column == 4
            && cells[3].value.kind() == fastxlsx::CellValueKind::Text
            && cells[3].value.text_value() == "snapshot-new",
        std::string(scenario) + " should expose row-major source, blank, and edit records");
}

void test_sparse_cells_snapshot()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-sparse-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-cells-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-cells-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-cells-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("snapshot-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    const auto cells = sheet.sparse_cells();
    check(cells.size() == sheet.cell_count(),
        "full sparse snapshot should include each represented record");
    check_full_snapshot_cells(cells, "placeholder-a1", "full sparse snapshot");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-snapshot"));
    check(cells[0].value.text_value() == "placeholder-a1",
        "full sparse snapshot should own its values");
    check(!editor.last_edit_error().has_value(),
        "full sparse snapshot should not update diagnostics");

    const auto inspect = [](fastxlsx::WorksheetEditor& inspected) {
        check(inspected.cell_count() == 4,
            "full sparse saved session should retain sparse count");
        check_bounds(inspected, 1, 1, 4, 4, "full sparse saved session");
        check_full_snapshot_cells(inspected.sparse_cells(),
            "changed-after-snapshot", "full sparse saved session");
        check(!inspected.try_cell("A2").has_value(),
            "full sparse saved session should keep erased A2 absent");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, 1, "full sparse save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full sparse save should preserve source package");
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, R"(<dimension ref="A1:D4"/>)",
        "full sparse save should retain dirty bounds");
    check_contains(xml, "changed-after-snapshot",
        "full sparse save should persist post-snapshot edit");
    check_not_contains(xml, "placeholder-a2",
        "full sparse save should not revive erased A2");
    check_reopened_output(output, "full sparse save reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, "full sparse snapshot", inspect);
}

void check_range_snapshot_cells(
    const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
    double b1_value, std::string_view c3_text, std::string_view scenario)
{
    check(cells.size() == 3
            && cells[0].reference.row == 1 && cells[0].reference.column == 2
            && cells[0].value.kind() == fastxlsx::CellValueKind::Number
            && cells[0].value.number_value() == b1_value
            && cells[1].reference.row == 3 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Blank
            && cells[2].reference.row == 3 && cells[2].reference.column == 3
            && cells[2].value.kind() == fastxlsx::CellValueKind::Text
            && cells[2].value.text_value() == c3_text,
        std::string(scenario) + " should expose row-major records inside the range");
}

void check_range_fixture_output(fastxlsx::WorksheetEditor& sheet,
    const std::vector<fastxlsx::WorksheetCellSnapshot>& range_cells,
    std::string_view outside_text, std::string_view scenario)
{
    check(sheet.cell_count() == 5,
        std::string(scenario) + " should retain sparse count");
    check_bounds(sheet, 1, 1, 4, 4, scenario);
    check_range_snapshot_cells(range_cells, 2.0,
        outside_text == "range-excluded" ? "range-new" : "a1-range-new", scenario);
    check(sheet.get_cell("A1").text_value() == "placeholder-a1"
            && sheet.get_cell("B1").number_value() == 2.0
            && !sheet.try_cell("A2").has_value()
            && sheet.get_cell("B3").kind() == fastxlsx::CellValueKind::Blank
            && sheet.get_cell("D4").text_value() == outside_text,
        std::string(scenario) + " should retain in-range and outside records");
}

void test_sparse_cells_range_snapshot()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-sparse-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("range-excluded"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("range-new"));
    sheet.erase_cell(2, 1);

    const fastxlsx::CellRange range {1, 2, 3, 3};
    const auto cells = sheet.sparse_cells(range);
    check_range_snapshot_cells(cells, 1.0, "range-new", "CellRange snapshot");
    check(sheet.sparse_cells(fastxlsx::CellRange {2, 2, 2, 3}).empty(),
        "CellRange snapshot should not synthesize missing cells");
    sheet.set_cell(1, 2, fastxlsx::CellValue::number(2.0));
    check(cells[0].value.number_value() == 1.0,
        "CellRange snapshot should own its values");
    const std::array<fastxlsx::CellRange, 4> invalid_ranges {
        fastxlsx::CellRange {3, 3, 1, 1},
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385}};
    const std::size_t count_before_invalid = sheet.cell_count();
    for (const fastxlsx::CellRange invalid : invalid_ranges) {
        check(throws_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid); }),
            "CellRange snapshot should reject invalid ranges");
    }
    check(sheet.cell_count() == count_before_invalid
            && !editor.last_edit_error().has_value(),
        "invalid CellRange snapshots should preserve sparse and diagnostic state");

    const auto inspect = [range](fastxlsx::WorksheetEditor& inspected) {
        check_range_fixture_output(inspected, inspected.sparse_cells(range),
            "range-excluded", "CellRange saved session");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, 1, "CellRange snapshot save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "range-excluded",
        "CellRange snapshot save should retain outside records");
    check_not_contains(xml, "placeholder-a2",
        "CellRange snapshot save should not revive erased A2");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "CellRange snapshot save should preserve source package");
    check_reopened_output(output, "CellRange snapshot reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, "CellRange snapshot", inspect);
}

void test_sparse_cells_a1_range_snapshot()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-sparse-a1-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-a1-range-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-a1-range-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sparse-a1-range-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("a1-range-excluded"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("a1-range-new"));
    sheet.erase_cell(2, 1);

    const auto cells = sheet.sparse_cells("B1:C3");
    check_range_snapshot_cells(cells, 1.0, "a1-range-new", "A1 range snapshot");
    const auto single = sheet.sparse_cells("B1");
    check(single.size() == 1 && single[0].reference.row == 1
            && single[0].reference.column == 2
            && single[0].value.number_value() == 1.0,
        "A1 range snapshot should accept a single-cell range");
    check(sheet.sparse_cells("B2:C2").empty(),
        "A1 range snapshot should not synthesize missing cells");
    sheet.set_cell(1, 2, fastxlsx::CellValue::number(2.0));
    check(cells[0].value.number_value() == 1.0,
        "A1 range snapshot should own its values");
    const std::array<std::string_view, 13> invalid_ranges {
        "", "a1:B2", "A1:b2", "A0:B2", "A01:B2", "XFE1:XFE2",
        "A1:A1048577", "A1:B2:C3", "B2:A1", "A:C", "1:3",
        "$A$1:$B$2", "Data!A1:B2"};
    const std::size_t count_before_invalid = sheet.cell_count();
    for (const std::string_view invalid : invalid_ranges) {
        check(throws_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid); }),
            "A1 range snapshot should reject invalid references");
    }
    check(sheet.cell_count() == count_before_invalid
            && !editor.last_edit_error().has_value(),
        "invalid A1 range snapshots should preserve sparse and diagnostic state");

    const auto inspect = [](fastxlsx::WorksheetEditor& inspected) {
        check_range_fixture_output(inspected, inspected.sparse_cells("B1:C3"),
            "a1-range-excluded", "A1 range saved session");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, 1, "A1 range snapshot save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "a1-range-excluded",
        "A1 range snapshot save should retain outside records");
    check_not_contains(xml, "placeholder-a2",
        "A1 range snapshot save should not revive erased A2");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 range snapshot save should preserve source package");
    check_reopened_output(output, "A1 range snapshot reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, "A1 range snapshot", inspect);
}

void check_batch_snapshot_cells(
    const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
    std::string_view a1_text, std::string_view scenario)
{
    check(cells.size() == 5
            && cells[0].reference.row == 4 && cells[0].reference.column == 4
            && cells[0].value.kind() == fastxlsx::CellValueKind::Text
            && cells[0].value.text_value() == "batch-new"
            && cells[1].reference.row == 1 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Number
            && cells[1].value.number_value() == 1.0
            && cells[2].reference.row == 3 && cells[2].reference.column == 2
            && cells[2].value.kind() == fastxlsx::CellValueKind::Blank
            && cells[3].reference.row == 1 && cells[3].reference.column == 1
            && cells[3].value.kind() == fastxlsx::CellValueKind::Text
            && cells[3].value.text_value() == a1_text
            && cells[4].reference.row == 1 && cells[4].reference.column == 1
            && cells[4].value.kind() == fastxlsx::CellValueKind::Text
            && cells[4].value.text_value() == a1_text,
        std::string(scenario) + " should preserve requested order, gaps, and duplicates");
}

void test_sparse_cells_coordinate_batch_snapshot()
{
    const std::filesystem::path source =
        write_source("fastxlsx-workbook-editor-public-sparse-batch-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-batch-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sparse-batch-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sparse-batch-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(4, 4, fastxlsx::CellValue::text("batch-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);
    const std::array<fastxlsx::WorksheetCellReference, 6> batch {
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {3, 2},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 1}};
    const auto cells = sheet.sparse_cells(batch);
    check_batch_snapshot_cells(cells, "placeholder-a1", "batch snapshot");
    const auto initializer_cells = sheet.sparse_cells({{4, 4}, {1, 1}});
    check(initializer_cells.size() == 2
            && initializer_cells[0].value.text_value() == "batch-new"
            && initializer_cells[1].value.text_value() == "placeholder-a1",
        "initializer-list snapshot should preserve requested order");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-batch-snapshot"));
    check(cells[3].value.text_value() == "placeholder-a1"
            && initializer_cells[1].value.text_value() == "placeholder-a1",
        "batch snapshots should own their values");
    const std::size_t count_before_invalid = sheet.cell_count();
    const std::size_t memory_before_invalid = sheet.estimated_memory_usage();
    const std::array<fastxlsx::WorksheetCellReference, 4> invalid_batch {
        fastxlsx::WorksheetCellReference {0, 1},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 16385},
        fastxlsx::WorksheetCellReference {1048577, 1}};
    check(throws_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_batch); }),
        "batch snapshot should reject invalid coordinates");
    check(sheet.cell_count() == count_before_invalid
            && sheet.estimated_memory_usage() == memory_before_invalid
            && !editor.last_edit_error().has_value(),
        "invalid batch snapshot should preserve sparse and diagnostic state");

    const auto inspect = [batch](fastxlsx::WorksheetEditor& inspected) {
        check(inspected.cell_count() == 4,
            "batch saved session should retain sparse count");
        check_bounds(inspected, 1, 1, 4, 4, "batch saved session");
        check_batch_snapshot_cells(inspected.sparse_cells(batch),
            "changed-after-batch-snapshot", "batch saved session");
        check(!inspected.try_cell("A2").has_value(),
            "batch saved session should keep erased A2 absent");
    };
    editor.save_as(output);
    check_clean_saved_state(editor, sheet, 1, "batch snapshot save");
    inspect(sheet);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "changed-after-batch-snapshot",
        "batch snapshot save should persist post-snapshot edit");
    check_contains(xml, "batch-new",
        "batch snapshot save should persist inserted cell");
    check_not_contains(xml, "placeholder-a2",
        "batch snapshot save should not revive erased A2");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "batch snapshot save should preserve source package");
    check_reopened_output(output, "batch snapshot reopen", inspect);
    check_two_noop_saves(editor, sheet, source, source_entries, output_entries,
        noop_output, second_noop_output, "batch snapshot", inspect);
}

} // namespace

int main()
{
    try {
        test_sparse_cells_snapshot();
        test_sparse_cells_range_snapshot();
        test_sparse_cells_a1_range_snapshot();
        test_sparse_cells_coordinate_batch_snapshot();
        std::cout << "WorkbookEditor public-state sparse snapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state sparse snapshot test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
