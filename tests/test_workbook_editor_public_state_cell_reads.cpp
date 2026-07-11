#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

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
    check(after.source_names == before.source_names,
        prefix + " should preserve source worksheet names");
    check(after.planned_names == before.planned_names,
        prefix + " should preserve planned worksheet names");
    check(catalog_entries_equal(after.catalog, before.catalog),
        prefix + " should preserve worksheet catalog");
    check(after.has_pending_changes == before.has_pending_changes
            && after.has_unsaved_changes == before.has_unsaved_changes
            && after.unsaved_change_count == before.unsaved_change_count
            && after.pending_change_count == before.pending_change_count,
        prefix + " should preserve pending, unsaved, and count state");
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
        prefix + " should preserve pending worksheet summaries");
}

std::filesystem::path write_source()
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-public-get-cell-source.xlsx");
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

void check_reopened_output(const std::filesystem::path& path)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened.has_unsaved_changes()
            && reopened.unsaved_change_count() == 0
            && !sheet.has_pending_changes(),
        "reopened explicit blank output should start clean with no unsaved watermark");
    check(reopened.pending_change_count() == 0
            && reopened.pending_materialized_worksheet_names().empty()
            && reopened.pending_materialized_cell_count() == 0
            && reopened.estimated_pending_materialized_memory_usage() == 0
            && reopened.pending_worksheet_edits().empty(),
        "reopened explicit blank output should expose no pending materialized state");
    check(reopened.pending_replacement_cell_count() == 0
            && reopened.estimated_pending_replacement_memory_usage() == 0
            && reopened.pending_replacement_worksheet_names().empty()
            && reopened.pending_targeted_cell_replacement_count() == 0
            && reopened.pending_targeted_cell_replacement_worksheet_names().empty()
            && reopened.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !reopened.last_edit_error().has_value(),
        "reopened explicit blank output should expose no replacement or error diagnostics");
    check(sheet.cell_count() == 4,
        "reopened explicit blank output should keep sparse count");
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == 1 && range->first_column == 1
            && range->last_row == 4 && range->last_column == 4,
        "reopened explicit blank output should expose blank bounds");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A1").text_value() == "placeholder-a1",
        "reopened explicit blank output should preserve A1");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 1.0,
        "reopened explicit blank output should preserve B1");
    check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A2").text_value() == "placeholder-a2",
        "reopened explicit blank output should preserve A2");
    check(sheet.get_cell("D4").kind() == fastxlsx::CellValueKind::Blank,
        "reopened explicit blank output should read D4 as blank");
    check(!sheet.try_cell("E5").has_value(),
        "reopened explicit blank output should keep E5 missing");
}

void test_get_cell_missing_and_blank_semantics()
{
    const std::filesystem::path source = write_source();
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-public-get-cell-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-public-get-cell-noop-output.xlsx");
    const std::filesystem::path second_noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-public-get-cell-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t initial_cell_count = sheet.cell_count();
    check(throws_fastxlsx_error([&] { (void)sheet.get_cell(4, 4); }),
        "get_cell should reject a missing sparse cell");
    check(sheet.cell_count() == initial_cell_count
            && !sheet.has_pending_changes() && !editor.has_pending_changes()
            && !editor.last_edit_error(),
        "missing get_cell should preserve sparse, dirty, and edit diagnostic state");

    sheet.set_cell(4, 4, fastxlsx::CellValue::blank());
    check(sheet.get_cell(4, 4).kind() == fastxlsx::CellValueKind::Blank,
        "get_cell should distinguish explicit blank from missing");
    check(!sheet.try_cell(5, 5).has_value(),
        "try_cell should keep unrelated cells missing");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "explicit blank save should preserve source package");
    const std::string& worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "explicit blank save should expand dimension");
    check_contains(worksheet_xml, R"(<c r="D4"/>)",
        "explicit blank save should persist D4");
    check_reopened_output(output);

    check(!sheet.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 0
            && editor.pending_change_count() == 1,
        "first save should clean the sheet and watermark while retaining one handoff");
    check(editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0,
        "first save should clear dirty materialized diagnostics");
    check(editor.pending_replacement_cell_count() == 0
            && editor.estimated_pending_replacement_memory_usage() == 0
            && editor.pending_replacement_worksheet_names().empty()
            && editor.pending_targeted_cell_replacement_count() == 0
            && editor.pending_targeted_cell_replacement_worksheet_names().empty()
            && editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0
            && !editor.last_edit_error().has_value(),
        "first save should expose no replacement or error diagnostics");
    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "first no-op save should keep the borrowed sheet clean");
    check_snapshot_preserved(editor, before_noop, "first no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "first no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "first no-op save should preserve source package");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "second no-op save should keep the borrowed sheet clean");
    check_snapshot_preserved(editor, before_second_noop, "second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "second no-op save should preserve first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "second no-op save should preserve source package");
    check_reopened_output(second_noop_output);
}

} // namespace

int main()
{
    try {
        test_get_cell_missing_and_blank_semantics();
        std::cout << "WorkbookEditor public-state cell read tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state cell read test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
