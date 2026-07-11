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
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty()
            && editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should expose no replacement work");
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
        std::string(scenario) + " should retain the source sparse cell count");
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

void check_reopened_mutated_output(
    const std::filesystem::path& path, std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check_no_pending_work(reopened, sheet, scenario);
    check(!reopened.last_edit_error().has_value(),
        std::string(scenario) + " should reopen without diagnostics");
    check(sheet.cell_count() == 3,
        std::string(scenario) + " should retain the edited sparse count");
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == 1 && range->first_column == 1
            && range->last_row == 4 && range->last_column == 4,
        std::string(scenario) + " should retain edited bounds");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        std::string(scenario) + " should retain A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 1.0,
        std::string(scenario) + " should retain B1");
    check(!sheet.try_cell("A2").has_value(),
        std::string(scenario) + " should retain erased A2");
    check(sheet.get_cell("D4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("D4").text_value() == "a1-overload-new",
        std::string(scenario) + " should retain inserted D4");
}

void check_saved_sheet_snapshots(const fastxlsx::WorksheetEditor& sheet,
    const fastxlsx::WorkbookEditor& editor, std::size_t pending_count,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 3,
        prefix + " should keep the saved sparse count");
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == 3,
        prefix + " should expose all saved sparse records");
    check(cells.size() == 3
            && cells[0].reference.row == 1 && cells[0].reference.column == 1
            && cells[0].value.kind() == fastxlsx::CellValueKind::Text
            && cells[0].value.text_value() == "placeholder-a1"
            && cells[1].reference.row == 1 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Number
            && cells[1].value.number_value() == 1.0
            && cells[2].reference.row == 4 && cells[2].reference.column == 4
            && cells[2].value.kind() == fastxlsx::CellValueKind::Text
            && cells[2].value.text_value() == "a1-overload-new",
        prefix + " should expose A1, B1, and D4 in sparse order");
    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one = sheet.row_cells(1);
    check(row_one.size() == 2
            && row_one[0].reference.column == 1
            && row_one[0].value.text_value() == "placeholder-a1"
            && row_one[1].reference.column == 2
            && row_one[1].value.number_value() == 1.0,
        prefix + " should expose row one in column order");
    check(sheet.row_cells(2).empty(),
        prefix + " should keep erased A2 absent from row snapshots");
    const auto row_four = sheet.row_cells(4);
    check(row_four.size() == 1 && row_four[0].reference.column == 4
            && row_four[0].value.text_value() == "a1-overload-new",
        prefix + " should expose D4 in row snapshots");
    const auto column_one = sheet.column_cells(1);
    const auto column_two = sheet.column_cells(2);
    const auto column_four = sheet.column_cells(4);
    check(column_one.size() == 1 && column_one[0].reference.row == 1
            && column_one[0].value.text_value() == "placeholder-a1",
        prefix + " should expose A1 in column snapshots");
    check(column_two.size() == 1 && column_two[0].reference.row == 1
            && column_two[0].value.number_value() == 1.0,
        prefix + " should expose B1 in column snapshots");
    check(sheet.column_cells(3).empty(),
        prefix + " should keep the intermediate column absent");
    check(column_four.size() == 1 && column_four[0].reference.row == 4
            && column_four[0].value.text_value() == "a1-overload-new",
        prefix + " should expose D4 in column snapshots");
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == 1 && range->first_column == 1
            && range->last_row == 4 && range->last_column == 4,
        prefix + " should retain sparse bounds");
    check(!sheet.try_cell("A2").has_value() && !sheet.has_pending_changes(),
        prefix + " should keep A2 absent and the borrowed handle clean");
    check(editor.pending_change_count() == pending_count
            && editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && editor.pending_worksheet_edits().empty(),
        prefix + " should not create another materialized handoff");
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty()
            && editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !editor.last_edit_error().has_value(),
        prefix + " should keep replacement and error diagnostics clear");
}

void test_a1_overloads_read_mutate_and_save()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-a1-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-a1-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> maybe_a1 = sheet.try_cell("A1");
    check(maybe_a1.has_value()
            && maybe_a1->kind() == fastxlsx::CellValueKind::Text
            && maybe_a1->text_value() == "placeholder-a1",
        "A1 try_cell should read source-backed text");
    const fastxlsx::CellValue b1 = sheet.get_cell("B1");
    check(b1.kind() == fastxlsx::CellValueKind::Number && b1.number_value() == 1.0,
        "A1 get_cell should read source-backed numbers");

    sheet.set_cell("D4", fastxlsx::CellValue::text("a1-overload-new"));
    check(sheet.get_cell("D4").text_value() == "a1-overload-new",
        "A1 set_cell should update the sparse store");
    sheet.erase_cell("A2");
    check(!sheet.try_cell("A2").has_value(),
        "A1 erase_cell should remove the sparse record");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 save should preserve the source package");
    const std::string& worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "A1 save should refresh worksheet dimension");
    check_contains(worksheet_xml, R"(<c r="D4" t="inlineStr">)",
        "A1 save should persist D4");
    check_contains(worksheet_xml, "a1-overload-new",
        "A1 save should persist D4 text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "A1 save should persist erased A2");
    check_reopened_mutated_output(output, "A1 save");

    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 0,
        "A1 save should clean the sheet and unsaved watermark");
    const std::size_t pending_count = editor.pending_change_count();
    check(pending_count == 1,
        "A1 save should retain one materialized handoff");
    check_saved_sheet_snapshots(sheet, editor, pending_count, "A1 saved handle");

    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop, "A1 first no-op save");
    check_saved_sheet_snapshots(sheet, editor, pending_count, "A1 first no-op handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "A1 first no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 first no-op save should preserve the source package");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check_snapshot_preserved(editor, before_second_noop, "A1 second no-op save");
    check_saved_sheet_snapshots(sheet, editor, pending_count, "A1 second no-op handle");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "A1 second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "A1 second no-op save should preserve the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "A1 second no-op save should preserve the source package");
    check_reopened_mutated_output(second_noop_output, "A1 second no-op save");
}

void test_a1_overloads_reject_invalid_references()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-a1-invalid-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-invalid-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-a1-invalid-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t cell_count_before_reads = sheet.cell_count();
    const EditorPublicStateSnapshot before_invalid_reads = snapshot(editor);
    const std::array<std::string_view, 8> invalid_references {
        "", "a1", "1A", "A0", "A01", "XFE1", "A1048577", "A1:B2"};
    for (const std::string_view reference : invalid_references) {
        check(throws_fastxlsx_error([&] { (void)sheet.try_cell(reference); }),
            "A1 try_cell should reject invalid references");
        check(throws_fastxlsx_error([&] { (void)sheet.get_cell(reference); }),
            "A1 get_cell should reject invalid references");
    }
    check(sheet.cell_count() == cell_count_before_reads,
        "invalid A1 reads should preserve sparse cell count");
    check(!sheet.try_cell("XFD1048576").has_value(),
        "A1 try_cell should accept the last legal Excel reference");
    check_snapshot_preserved(editor, before_invalid_reads,
        "invalid A1 reads");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 reads should preserve existing cells");

    const std::size_t cell_count_before_mutation = sheet.cell_count();
    check(throws_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("should-not-write"));
    }), "A1 set_cell should reject lowercase references");
    check(sheet.cell_count() == cell_count_before_mutation
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 set_cell should preserve the sparse store");
    check(editor.last_edit_error().has_value(),
        "invalid A1 set_cell should update last_edit_error");
    check(throws_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "A1 erase_cell should reject range references");
    check(sheet.cell_count() == cell_count_before_mutation
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 erase_cell should preserve the sparse store");
    check(editor.last_edit_error().has_value(),
        "invalid A1 erase_cell should update last_edit_error");
    check_no_pending_work(editor, sheet, "invalid A1 mutations");

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save, "invalid A1 mutation save");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "invalid A1 mutation save should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid A1 mutation save should preserve source package");
    check_reopened_default_output(output, "invalid A1 mutation save");

    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop,
        "invalid A1 mutation no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries && noop_entries == source_entries,
        "invalid A1 mutation no-op output should match source and first output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid A1 mutation no-op save should preserve source package");
    check_reopened_default_output(noop_output, "invalid A1 mutation no-op save");
}

} // namespace

int main()
{
    try {
        test_a1_overloads_read_mutate_and_save();
        test_a1_overloads_reject_invalid_references();
        std::cout << "WorkbookEditor public-state A1 access tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state A1 access test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
