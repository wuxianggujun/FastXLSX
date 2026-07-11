#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <array>
#include <cstddef>
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

std::filesystem::path write_two_sheet_source(std::string_view filename)
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

void check_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty()
            && editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should expose no replacement diagnostics");
}

void check_no_pending_work(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 0
            && editor.pending_change_count() == 0
            && !sheet.has_pending_changes(),
        prefix + " should expose no pending or unsaved work");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should expose no pending materialized work");
    check_no_replacement_diagnostics(editor, scenario);
}

void check_dirty_materialized_summary(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet, std::string_view scenario)
{
    const std::string prefix(scenario);
    const std::size_t cell_count = sheet.cell_count();
    const std::size_t memory = sheet.estimated_memory_usage();
    check(editor.has_pending_changes() && editor.has_unsaved_changes()
            && editor.unsaved_change_count() > 0
            && editor.pending_change_count() == 0
            && sheet.has_pending_changes(),
        prefix + " should expose dirty materialized state before handoff");
    check(editor.pending_materialized_worksheet_names()
            == std::vector<std::string>{"Data"}
            && editor.pending_materialized_cell_count() == cell_count
            && editor.estimated_pending_materialized_memory_usage() == memory,
        prefix + " should expose materialized count and memory");
    check_no_replacement_diagnostics(editor, scenario);
    check(!editor.last_edit_error().has_value(),
        prefix + " should keep diagnostics clear");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one worksheet summary");
    check(summaries.size() == 1
            && summaries[0].source_name == "Data"
            && summaries[0].planned_name == "Data"
            && !summaries[0].renamed
            && !summaries[0].sheet_data_replaced
            && !summaries[0].targeted_cells_replaced
            && summaries[0].replacement_cell_count == 0
            && summaries[0].estimated_replacement_memory_usage == 0
            && summaries[0].materialized_dirty
            && summaries[0].materialized_cell_count == cell_count
            && summaries[0].estimated_materialized_memory_usage == memory,
        prefix + " should match the dirty materialized session");
}

void check_reopened_default_output(
    const std::filesystem::path& path, std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check_no_pending_work(reopened, sheet, scenario);
    check(!reopened.last_edit_error().has_value(),
        std::string(scenario) + " should reopen without diagnostics");
    check(sheet.cell_count() == 3,
        std::string(scenario) + " should retain source sparse count");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        std::string(scenario) + " should retain A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 1.0,
        std::string(scenario) + " should retain B1");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A2").text_value() == "placeholder-a2",
        std::string(scenario) + " should retain A2");
}

void check_reopened_range_output(
    const std::filesystem::path& path, std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check_no_pending_work(reopened, sheet, scenario);
    check(!reopened.last_edit_error().has_value(),
        std::string(scenario) + " should reopen without diagnostics");
    check(sheet.cell_count() == 3,
        std::string(scenario) + " should retain sparse mutation count");
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == 1 && range->first_column == 2
            && range->last_row == 4 && range->last_column == 4,
        std::string(scenario) + " should retain sparse mutation bounds");
    check(!sheet.try_cell("A1").has_value() && !sheet.try_cell("A2").has_value(),
        std::string(scenario) + " should retain erased A1 and A2");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        std::string(scenario) + " should retain blanked B1");
    check(!sheet.try_cell("B2").has_value(),
        std::string(scenario) + " should not synthesize B2");
    check(sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        std::string(scenario) + " should retain blanked C3");
    check(sheet.get_cell("D4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("D4").text_value() == "outside-a1-range",
        std::string(scenario) + " should retain D4 outside the range");
}

void check_source_snapshots(const fastxlsx::WorksheetEditor& sheet,
    const fastxlsx::WorkbookEditor& editor, std::size_t expected_count,
    std::size_t expected_memory, std::string_view scenario)
{
    const std::string prefix(scenario);
    const auto check_cells = [&](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells,
                                 std::string_view view) {
        check(cells.size() == 3,
            prefix + " " + std::string(view) + " should expose all source cells");
        check(cells.size() == 3
                && cells[0].reference.row == 1 && cells[0].reference.column == 1
                && cells[0].value.kind() == fastxlsx::CellValueKind::Text
                && cells[0].value.text_value() == "placeholder-a1"
                && cells[1].reference.row == 1 && cells[1].reference.column == 2
                && cells[1].value.kind() == fastxlsx::CellValueKind::Number
                && cells[1].value.number_value() == 1.0
                && cells[2].reference.row == 2 && cells[2].reference.column == 1
                && cells[2].value.kind() == fastxlsx::CellValueKind::Text
                && cells[2].value.text_value() == "placeholder-a2",
            prefix + " " + std::string(view) + " should retain source ordering and values");
    };
    check_cells(sheet.sparse_cells(), "full sparse snapshot");
    check_cells(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}),
        "CellRange snapshot");
    check_cells(sheet.sparse_cells("A1:B2"), "A1 range snapshot");
    const auto row_one = sheet.row_cells(1);
    check(row_one.size() == 2
            && row_one[0].reference.column == 1
            && row_one[0].value.text_value() == "placeholder-a1"
            && row_one[1].reference.column == 2
            && row_one[1].value.number_value() == 1.0,
        prefix + " should retain row-one snapshots");
    const auto column_one = sheet.column_cells(1);
    check(column_one.size() == 2
            && column_one[0].reference.row == 1
            && column_one[0].value.text_value() == "placeholder-a1"
            && column_one[1].reference.row == 2
            && column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should retain column-one snapshots");
    check(!sheet.has_pending_changes() && !editor.has_pending_changes()
            && editor.pending_change_count() == 0
            && sheet.cell_count() == expected_count
            && sheet.estimated_memory_usage() == expected_memory
            && !editor.last_edit_error().has_value(),
        prefix + " should retain clean sparse state");
}

void test_a1_range_mutations_sparse_semantics()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-a1-range-mutation-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-range-mutation-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-a1-range-mutation-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-a1-range-mutation-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell("C3", fastxlsx::CellValue::text("a1-range-clear-target"));
    sheet.set_cell("D4", fastxlsx::CellValue::text("outside-a1-range"));
    sheet.clear_cell_values("B1:C3");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank
            && sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range clear should blank represented cells");
    check(!sheet.try_cell("B2").has_value(),
        "A1 range clear should not synthesize missing cells");
    const auto cleared = sheet.sparse_cells("B1:C3");
    check(cleared.size() == 2
            && cleared[0].reference.row == 1 && cleared[0].reference.column == 2
            && cleared[0].value.kind() == fastxlsx::CellValueKind::Blank
            && cleared[1].reference.row == 3 && cleared[1].reference.column == 3
            && cleared[1].value.kind() == fastxlsx::CellValueKind::Blank,
        "A1 range clear should remain sparse and ordered");

    sheet.erase_cells("A1:A2");
    check(!sheet.try_cell("A1").has_value() && !sheet.try_cell("A2").has_value(),
        "A1 range erase should remove represented source cells");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank
            && sheet.get_cell("C3").kind() == fastxlsx::CellValueKind::Blank,
        "A1 range erase should preserve unrelated blank records");
    sheet.erase_cells("B2:C2");
    check(!editor.last_edit_error().has_value(),
        "missing-only A1 range erase should be a successful no-op");
    check_dirty_materialized_summary(editor, sheet, "A1 range dirty summary");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 range save should preserve source package");
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, R"(<c r="B1"/>)",
        "A1 range clear should persist blank B1");
    check_contains(xml, R"(<c r="C3"/>)",
        "A1 range clear should persist blank C3");
    check_contains(xml, "outside-a1-range",
        "A1 range mutation should preserve D4");
    check_not_contains(xml, "placeholder-a1",
        "A1 range erase should omit A1 text");
    check_not_contains(xml, "placeholder-a2",
        "A1 range erase should omit A2 text");
    check_not_contains(xml, "a1-range-clear-target",
        "A1 range clear should omit prior C3 payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "A1 range mutation should preserve untouched worksheet");
    check_reopened_range_output(output, "A1 range save");

    check(editor.pending_change_count() == 1 && !sheet.has_pending_changes()
            && !editor.has_unsaved_changes() && editor.unsaved_change_count() == 0,
        "A1 range save should retain one clean handoff");
    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop, "A1 range first no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "A1 range first no-op output should match first output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 range first no-op should preserve source package");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check_snapshot_preserved(editor, before_second_noop,
        "A1 range second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "A1 range second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "A1 range second no-op should preserve first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 range second no-op should preserve source package");
    check_reopened_range_output(second_noop_output, "A1 range second no-op save");
}

void test_a1_range_mutations_invalid_references()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-a1-range-invalid-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-range-invalid-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-a1-range-invalid-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t count_before = sheet.cell_count();
    const std::size_t memory_before = sheet.estimated_memory_usage();
    const std::array<std::string_view, 13> invalid_ranges {
        "", "a1:B2", "A1:b2", "A0:B2", "A01:B2", "XFE1:XFE2",
        "A1:A1048577", "A1:B2:C3", "B2:A1", "A:C", "1:3",
        "$A$1:$B$2", "Data!A1:B2"};
    for (const std::string_view range : invalid_ranges) {
        check(throws_fastxlsx_error([&] { sheet.clear_cell_values(range); }),
            "A1 range clear should reject invalid references");
        check(editor.last_edit_error().has_value(),
            "invalid A1 range clear should update last_edit_error");
        check(throws_fastxlsx_error([&] { sheet.erase_cells(range); }),
            "A1 range erase should reject invalid references");
        check(editor.last_edit_error().has_value(),
            "invalid A1 range erase should update last_edit_error");
    }
    check(!sheet.has_pending_changes() && !editor.has_pending_changes()
            && editor.pending_change_count() == 0
            && sheet.cell_count() == count_before
            && sheet.estimated_memory_usage() == memory_before
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 ranges should preserve clean sparse state");

    sheet.clear_cell_values("Z10:Z11");
    check(!editor.last_edit_error().has_value() && !sheet.has_pending_changes(),
        "valid missing-only clear should clear diagnostics and remain clean");
    check(throws_fastxlsx_error([&] { sheet.erase_cells("A0:A1"); }),
        "invalid erase should seed a diagnostic before recovery");
    check(editor.last_edit_error().has_value(),
        "invalid erase should record a diagnostic before recovery");
    sheet.erase_cells("Z10:Z11");
    check(!editor.last_edit_error().has_value() && !sheet.has_pending_changes()
            && sheet.cell_count() == count_before,
        "valid missing-only erase should clear diagnostics without synthesizing cells");
    check_source_snapshots(sheet, editor, count_before, memory_before,
        "invalid A1 ranges recovered session");

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save,
        "invalid A1 range first no-op save");
    check_source_snapshots(sheet, editor, count_before, memory_before,
        "invalid A1 range first saved session");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "invalid A1 range first no-op output should copy source");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid A1 range first no-op should preserve source package");
    check_reopened_default_output(output, "invalid A1 range first no-op save");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_second_noop,
        "invalid A1 range second no-op save");
    check_source_snapshots(sheet, editor, count_before, memory_before,
        "invalid A1 range second saved session");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "invalid A1 range second no-op output should match first output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "invalid A1 range second no-op should preserve first output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid A1 range second no-op should preserve source package");
    check_reopened_default_output(noop_output, "invalid A1 range second no-op save");
}

} // namespace

int main()
{
    try {
        test_a1_range_mutations_sparse_semantics();
        test_a1_range_mutations_invalid_references();
        std::cout << "WorkbookEditor public-state A1 range tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state A1 range test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
