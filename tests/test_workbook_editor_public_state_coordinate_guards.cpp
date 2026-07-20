#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <cstddef>
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

void check_clean_diagnostics(const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    const std::optional<std::string>& expected_error,
    std::string_view scenario)
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
    check(editor.last_edit_error() == expected_error,
        prefix + " should expose the expected edit diagnostic");
}

void check_source_cells(const fastxlsx::WorksheetEditor& sheet,
    std::size_t expected_memory, std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == 3 && sheet.estimated_memory_usage() == expected_memory,
        prefix + " should preserve sparse count and memory");
    const auto cells = sheet.sparse_cells();
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
        prefix + " should preserve source sparse ordering and values");
    const auto range_cells = sheet.sparse_cells("A1:B2");
    check(range_cells.size() == cells.size(),
        prefix + " should preserve A1 range snapshots");
    const auto row_one = sheet.row_cells(1);
    check(row_one.size() == 2
            && row_one[0].reference.column == 1
            && row_one[0].value.text_value() == "placeholder-a1"
            && row_one[1].reference.column == 2
            && row_one[1].value.number_value() == 1.0,
        prefix + " should preserve row-one snapshots");
    const auto column_one = sheet.column_cells(1);
    check(column_one.size() == 2
            && column_one[0].reference.row == 1
            && column_one[0].value.text_value() == "placeholder-a1"
            && column_one[1].reference.row == 2
            && column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should preserve column-one snapshots");
    check(!sheet.try_cell("C3").has_value(),
        prefix + " should keep rejected or missing C3 absent");
}

void check_reopened_default_output(
    const std::filesystem::path& path, std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check_clean_diagnostics(reopened, sheet, std::nullopt, scenario);
    const std::size_t memory = sheet.estimated_memory_usage();
    check_source_cells(sheet, memory, scenario);
}

void check_recovered_cells(const fastxlsx::WorksheetEditor& sheet,
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix(scenario);
    const auto cells = sheet.sparse_cells();
    check(cells.size() == 3
            && cells[0].reference.row == 1 && cells[0].reference.column == 1
            && cells[0].value.kind() == fastxlsx::CellValueKind::Text
            && cells[0].value.text_value() == "row-column-recovered"
            && cells[1].reference.row == 1 && cells[1].reference.column == 2
            && cells[1].value.kind() == fastxlsx::CellValueKind::Number
            && cells[1].value.number_value() == 1.0
            && cells[2].reference.row == 2 && cells[2].reference.column == 1
            && cells[2].value.kind() == fastxlsx::CellValueKind::Text
            && cells[2].value.text_value() == "placeholder-a2",
        prefix + " should preserve recovered sparse ordering and values");
    check(sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2}).size() == 3
            && sheet.sparse_cells("A1:B2").size() == 3,
        prefix + " should preserve recovered range snapshots");
    const auto row_one = sheet.row_cells(1);
    const auto column_one = sheet.column_cells(1);
    check(row_one.size() == 2
            && row_one[0].value.text_value() == "row-column-recovered"
            && row_one[1].value.number_value() == 1.0,
        prefix + " should preserve recovered row snapshots");
    check(column_one.size() == 2
            && column_one[0].value.text_value() == "row-column-recovered"
            && column_one[1].value.text_value() == "placeholder-a2",
        prefix + " should preserve recovered column snapshots");
    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value() && range->first_row == 1 && range->first_column == 1
            && range->last_row == 2 && range->last_column == 2,
        prefix + " should preserve recovered bounds");
    check(!sheet.try_cell(1, 3).has_value(),
        prefix + " should keep rejected C1 absent");
    check(!sheet.has_pending_changes() && editor.pending_change_count() == 1
            && editor.pending_materialized_worksheet_names().empty()
            && editor.pending_materialized_cell_count() == 0
            && editor.estimated_pending_materialized_memory_usage() == 0
            && !editor.last_edit_error().has_value(),
        prefix + " should retain one clean materialized handoff");
}

void check_reopened_recovered_output(
    const std::filesystem::path& path, std::string_view scenario)
{
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(path);
    fastxlsx::WorksheetEditor sheet = reopened.worksheet("Data");
    check_clean_diagnostics(reopened, sheet, std::nullopt, scenario);
    check(sheet.cell_count() == 3,
        std::string(scenario) + " should preserve recovered sparse count");
    check(sheet.get_cell(1, 1).kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell(1, 1).text_value() == "row-column-recovered",
        std::string(scenario) + " should preserve recovered A1");
    check(sheet.get_cell(1, 2).kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell(1, 2).number_value() == 1.0,
        std::string(scenario) + " should preserve B1");
    check(sheet.get_cell(2, 1).kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell(2, 1).text_value() == "placeholder-a2",
        std::string(scenario) + " should preserve A2");
    check(!sheet.try_cell(1, 3).has_value(),
        std::string(scenario) + " should keep rejected C1 absent");
}

void test_row_column_overloads_reject_invalid_coordinates()
{
    const std::filesystem::path source = write_source(
        "fastxlsx-workbook-editor-public-row-column-invalid-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-row-column-invalid-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-invalid-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-invalid-second-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t count_before_reads = sheet.cell_count();
    const EditorPublicStateSnapshot before_invalid_reads = snapshot(editor);
    check(throws_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "try_cell should reject row zero");
    check(throws_fastxlsx_error([&] { (void)sheet.get_cell(1, 0); }),
        "get_cell should reject column zero");
    check(throws_fastxlsx_error([&] { (void)sheet.try_cell(1048577, 1); }),
        "try_cell should reject row overflow");
    check(throws_fastxlsx_error([&] { (void)sheet.get_cell(1, 16385); }),
        "get_cell should reject column overflow");
    check(sheet.cell_count() == count_before_reads
            && !sheet.try_cell(1048576, 16384).has_value(),
        "invalid reads should preserve count and accept the last legal coordinate");
    check_snapshot_preserved(editor, before_invalid_reads,
        "invalid row/column reads");

    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-row-zero"));
    }), "set_cell should reject row zero");
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(1, 16385,
            fastxlsx::CellValue::text("invalid-column-overflow"));
    }), "set_cell should reject column overflow");
    check(throws_fastxlsx_error([&] { sheet.erase_cell(1048577, 1); }),
        "erase_cell should reject row overflow");
    check(throws_fastxlsx_error([&] { sheet.erase_cell(1, 0); }),
        "erase_cell should reject column zero");
    const std::optional<std::string> invalid_error = editor.last_edit_error();
    check(invalid_error.has_value(),
        "invalid mutations should update last_edit_error");
    check_clean_diagnostics(editor, sheet, invalid_error,
        "invalid row/column mutations");
    check(sheet.cell_count() == count_before_reads
            && sheet.get_cell(1, 1).text_value() == "placeholder-a1",
        "invalid mutations should preserve source cells");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("row-column-recovered"));
    check(!editor.last_edit_error().has_value(),
        "valid mutation should clear prior diagnostics");
    editor.save_as(output);
    check_recovered_cells(sheet, editor, "row/column recovery save");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column recovery save should preserve source package");
    const std::string& xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(xml, "row-column-recovered",
        "row/column recovery should persist valid payload");
    check_not_contains(xml, "invalid-row-zero",
        "row/column recovery should omit rejected row payload");
    check_not_contains(xml, "invalid-column-overflow",
        "row/column recovery should omit rejected column payload");
    check_reopened_recovered_output(output, "row/column recovery save");

    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop,
        "row/column recovery first no-op save");
    check_recovered_cells(sheet, editor, "row/column recovery first no-op");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "row/column first no-op output should match first output");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(second_noop_output);
    check_snapshot_preserved(editor, before_second_noop,
        "row/column recovery second no-op save");
    check_recovered_cells(sheet, editor, "row/column recovery second no-op");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "row/column second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column second no-op should preserve prior files");
    check_reopened_recovered_output(
        second_noop_output, "row/column recovery second no-op save");
}

struct RejectedMutationCase {
    std::string id;
    std::string scenario;
    std::function<void(fastxlsx::WorksheetEditor&)> action;
    std::string diagnostic;
    std::string rejected_payload;
};

void check_rejected_mutation_noop(const RejectedMutationCase& test_case)
{
    const std::filesystem::path source = write_source(
        "fastxlsx-coordinate-guard-" + test_case.id + "-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-coordinate-guard-" + test_case.id + "-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-coordinate-guard-" + test_case.id + "-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    bool failed = false;
    try {
        test_case.action(sheet);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), test_case.diagnostic,
            test_case.scenario + " should expose expected diagnostic");
    }
    check(failed, test_case.scenario + " should reject the mutation");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        test_case.scenario + " should retain public diagnostic");
    if (prior_error.has_value()) {
        check_contains(*prior_error, test_case.diagnostic,
            test_case.scenario + " should retain expected diagnostic");
    }
    check_clean_diagnostics(editor, sheet, prior_error, test_case.scenario);
    check_source_cells(sheet, baseline_memory, test_case.scenario);

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save, test_case.scenario + " save");
    check_clean_diagnostics(editor, sheet, prior_error, test_case.scenario + " save");
    check_source_cells(sheet, baseline_memory, test_case.scenario + " save");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        test_case.scenario + " save should copy source entries");
    if (!test_case.rejected_payload.empty()) {
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            test_case.rejected_payload,
            test_case.scenario + " save should omit rejected payload");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        test_case.scenario + " save should preserve source package");
    check_reopened_default_output(output, test_case.scenario + " save");

    const EditorPublicStateSnapshot before_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_noop,
        test_case.scenario + " no-op save");
    check_clean_diagnostics(
        editor, sheet, prior_error, test_case.scenario + " no-op save");
    check_source_cells(sheet, baseline_memory, test_case.scenario + " no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries && noop_entries == output_entries,
        test_case.scenario + " no-op output should match source and first output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        test_case.scenario + " no-op should preserve source package");
    check_reopened_default_output(noop_output, test_case.scenario + " no-op save");
}

void test_invalid_coordinate_mutations_noop_save()
{
    const std::vector<RejectedMutationCase> cases {
        {"set-row-zero", "set_cell invalid row coordinate",
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell(0, 1, fastxlsx::CellValue::text("set-cell-invalid-row"));
            }, "WorksheetEditor cell coordinate is invalid", "set-cell-invalid-row"},
        {"set-column-overflow", "set_cell invalid column coordinate",
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell(1, 16385,
                    fastxlsx::CellValue::text("set-cell-invalid-column"));
            }, "WorksheetEditor cell coordinate is invalid", "set-cell-invalid-column"},
        {"set-a1-invalid", "set_cell A1 invalid reference",
            [](fastxlsx::WorksheetEditor& sheet) {
                sheet.set_cell("a1", fastxlsx::CellValue::text("set-cell-a1-invalid"));
            }, "WorksheetEditor cell reference is invalid", "set-cell-a1-invalid"},
        {"erase-row-overflow", "erase_cell invalid row coordinate",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.erase_cell(1048577, 1); },
            "WorksheetEditor cell coordinate is invalid", ""},
        {"erase-column-zero", "erase_cell invalid column coordinate",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.erase_cell(1, 0); },
            "WorksheetEditor cell coordinate is invalid", ""},
        {"erase-a1-range", "erase_cell A1 range reference",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.erase_cell("A1:B2"); },
            "WorksheetEditor cell reference is invalid", ""},
        {"clear-row-zero", "clear_cell_value invalid row coordinate",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.clear_cell_value(0, 1); },
            "WorksheetEditor cell coordinate is invalid", ""},
        {"clear-column-overflow", "clear_cell_value invalid column coordinate",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.clear_cell_value(1, 16385); },
            "WorksheetEditor cell coordinate is invalid", ""},
        {"clear-a1-invalid", "clear_cell_value A1 invalid reference",
            [](fastxlsx::WorksheetEditor& sheet) { sheet.clear_cell_value("a1"); },
            "WorksheetEditor cell reference is invalid", ""},
    };
    for (const RejectedMutationCase& test_case : cases) {
        check_rejected_mutation_noop(test_case);
    }
}

void test_invalid_cell_reads_preserve_prior_diagnostic()
{
    const std::filesystem::path source = write_source(
        "fastxlsx-workbook-editor-public-invalid-read-error-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-invalid-read-error-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-invalid-read-error-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_memory = sheet.estimated_memory_usage();
    check_source_cells(sheet, baseline_memory, "invalid read baseline");
    check(throws_fastxlsx_error([&] {
        sheet.set_cell(0, 1, fastxlsx::CellValue::text("invalid-read-sentinel"));
    }), "invalid mutation should seed last_edit_error");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "invalid mutation should record a prior diagnostic");
    if (prior_error.has_value()) {
        check_contains(*prior_error, "WorksheetEditor cell coordinate is invalid",
            "prior diagnostic should identify invalid coordinates");
    }
    const EditorPublicStateSnapshot before_invalid_reads = snapshot(editor);
    const auto check_read_failure = [&](auto&& action, std::string_view scenario) {
        check(throws_fastxlsx_error(action),
            std::string(scenario) + " should reject the read");
        check_snapshot_preserved(editor, before_invalid_reads, scenario);
        check_clean_diagnostics(editor, sheet, prior_error, scenario);
        check_source_cells(sheet, baseline_memory, scenario);
    };
    check_read_failure([&] { (void)sheet.try_cell(0, 1); }, "row-zero try_cell");
    check_read_failure([&] { (void)sheet.get_cell(1, 0); }, "column-zero get_cell");
    check_read_failure([&] { (void)sheet.try_cell(1048577, 1); },
        "row-overflow try_cell");
    check_read_failure([&] { (void)sheet.get_cell(1, 16385); },
        "column-overflow get_cell");
    check_read_failure([&] { (void)sheet.try_cell("a1"); },
        "lowercase A1 try_cell");
    check_read_failure([&] { (void)sheet.get_cell("A1:B2"); },
        "range A1 get_cell");
    check_read_failure([&] { (void)sheet.try_cell("A01"); },
        "leading-zero A1 try_cell");
    check_read_failure([&] { (void)sheet.get_cell("XFE1"); },
        "column-overflow A1 get_cell");
    check_read_failure([&] { (void)sheet.get_cell(4, 4); },
        "missing-cell get_cell");

    check(!sheet.try_cell("XFD1048576").has_value(),
        "try_cell should accept the last legal missing cell");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "valid get_cell should preserve source data after failures");
    check_snapshot_preserved(editor, before_invalid_reads,
        "valid reads after invalid failures");
    check_source_cells(sheet, baseline_memory,
        "valid reads after invalid failures");

    const EditorPublicStateSnapshot before_save = snapshot(editor);
    editor.save_as(output);
    check_snapshot_preserved(editor, before_save, "invalid reads first no-op save");
    check_clean_diagnostics(editor, sheet, prior_error,
        "invalid reads first saved session");
    check_source_cells(sheet, baseline_memory, "invalid reads first saved session");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "invalid reads first output should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid reads first save should preserve source package");
    check_reopened_default_output(output, "invalid reads first no-op save");

    const EditorPublicStateSnapshot before_second_noop = snapshot(editor);
    editor.save_as(noop_output);
    check_snapshot_preserved(editor, before_second_noop,
        "invalid reads second no-op save");
    check_clean_diagnostics(editor, sheet, prior_error,
        "invalid reads second saved session");
    check_source_cells(sheet, baseline_memory, "invalid reads second saved session");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "invalid reads second output should match first output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries
            && fastxlsx::test::read_zip_entries(source) == source_entries,
        "invalid reads second save should preserve prior files");
    check_reopened_default_output(noop_output, "invalid reads second no-op save");
}

void check_cell_range_equals(
    const std::optional<fastxlsx::CellRange>& range,
    std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column,
    std::string_view message)
{
    check(range.has_value() && range->first_row == first_row &&
            range->first_column == first_column && range->last_row == last_row &&
            range->last_column == last_column,
        message);
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

bool workbook_editor_catalog_entries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].source_name != rhs[index].source_name
            || lhs[index].planned_name != rhs[index].planned_name
            || lhs[index].renamed != rhs[index].renamed) {
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
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), before.catalog),
        prefix + " should preserve worksheet catalog");
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
            || left.freeze_panes_changed != right.freeze_panes_changed
            || left.frozen_row_count != right.frozen_row_count
            || left.frozen_column_count != right.frozen_column_count
            || left.materialized_dirty != right.materialized_dirty
            || left.materialized_cell_count != right.materialized_cell_count
            || left.estimated_materialized_memory_usage
                != right.estimated_materialized_memory_usage) {
            return false;
        }
    }
    return true;
}

struct WorkbookEditorPublicSaveStateSnapshot {
    bool has_pending_changes{};
    std::size_t pending_change_count{};
    std::vector<std::string> pending_materialized_worksheet_names;
    std::size_t pending_materialized_cell_count{};
    std::size_t estimated_pending_materialized_memory_usage{};
    std::size_t pending_replacement_cell_count{};
    std::size_t estimated_pending_replacement_memory_usage{};
    std::vector<std::string> pending_replacement_worksheet_names;
    std::size_t pending_targeted_cell_replacement_count{};
    std::vector<std::string> pending_targeted_cell_replacement_worksheet_names;
    std::size_t estimated_pending_targeted_cell_replacement_xml_bytes{};
    std::optional<std::string> last_edit_error;
    std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> pending_worksheet_edits;
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
    check(editor.has_pending_changes() == before.has_pending_changes,
        std::string(scenario) + " should preserve pending-change state");
    check(editor.pending_change_count() == before.pending_change_count,
        std::string(scenario) + " should preserve public pending change count");
    check(editor.pending_materialized_worksheet_names()
            == before.pending_materialized_worksheet_names,
        std::string(scenario) + " should preserve pending materialized worksheet names");
    check(editor.pending_materialized_cell_count()
            == before.pending_materialized_cell_count,
        std::string(scenario) + " should preserve pending materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == before.estimated_pending_materialized_memory_usage,
        std::string(scenario) + " should preserve materialized memory estimate");
    check(editor.pending_replacement_cell_count()
            == before.pending_replacement_cell_count,
        std::string(scenario) + " should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == before.estimated_pending_replacement_memory_usage,
        std::string(scenario) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names()
            == before.pending_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending replacement worksheet names");
    check(editor.pending_targeted_cell_replacement_count()
            == before.pending_targeted_cell_replacement_count,
        std::string(scenario) + " should preserve pending targeted cell replacement count");
    check(editor.pending_targeted_cell_replacement_worksheet_names()
            == before.pending_targeted_cell_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes()
            == before.estimated_pending_targeted_cell_replacement_xml_bytes,
        std::string(scenario) + " should preserve targeted cell XML byte estimate");
    check(editor.last_edit_error() == before.last_edit_error,
        std::string(scenario) + " should not replace or clear last_edit_error");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), before.pending_worksheet_edits),
        std::string(scenario) + " should preserve pending worksheet edit summaries");
}

void check_workbook_editor_public_no_pending_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(!editor.has_pending_changes(), prefix + " should keep the editor clean");
    check(editor.pending_change_count() == 0,
        prefix + " should keep pending edit count empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep pending summaries empty");
}

void check_workbook_editor_no_replacement_diagnostics(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(editor.pending_replacement_cell_count() == 0,
        prefix + " should not expose replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        prefix + " should not expose replacement memory");
    check(editor.pending_replacement_worksheet_names().empty(),
        prefix + " should not expose replacement sheet names");
    check(editor.pending_targeted_cell_replacement_count() == 0,
        prefix + " should not expose targeted cell patches");
    check(editor.pending_targeted_cell_replacement_worksheet_names().empty(),
        prefix + " should not expose targeted cell worksheet names");
    check(editor.estimated_pending_targeted_cell_replacement_xml_bytes() == 0,
        prefix + " should not expose targeted cell XML bytes");
    check(!editor.has_pending_targeted_cell_replacement("Data"),
        prefix + " should not report Data targeted cell patches");
}

void check_public_state_single_named_dirty_materialized_summary(
    const fastxlsx::WorkbookEditor& editor,
    const fastxlsx::WorksheetEditor& sheet,
    std::string_view worksheet_name,
    std::size_t expected_pending_change_count,
    std::string_view scenario,
    const std::optional<std::string>& expected_last_edit_error = std::nullopt)
{
    const std::string prefix = std::string(scenario);
    const std::string expected_name = std::string(worksheet_name);
    const std::size_t expected_cell_count = sheet.cell_count();
    const std::size_t expected_memory_usage = sheet.estimated_memory_usage();

    check(editor.has_pending_changes(),
        prefix + " should expose pending public state");
    check(editor.pending_change_count() == expected_pending_change_count,
        prefix + " should not count dirty materialized sessions as staged handoffs");
    check(editor.last_edit_error() == expected_last_edit_error,
        prefix + " should expose the expected last_edit_error state");
    check_workbook_editor_no_replacement_diagnostics(
        editor, prefix + " should not expose replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{expected_name},
        prefix + " should expose the expected dirty materialized worksheet");
    check(editor.pending_materialized_cell_count() == expected_cell_count,
        prefix + " should expose the dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() == expected_memory_usage,
        prefix + " should expose the dirty materialized memory estimate");

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
        editor.pending_worksheet_edits();
    check(summaries.size() == 1,
        prefix + " should expose one dirty materialized summary");
    if (summaries.size() == 1) {
        const auto& summary = summaries[0];
        check(summary.source_name == expected_name &&
                summary.planned_name == expected_name &&
                !summary.renamed,
            prefix + " summary should identify the worksheet without rename state");
        check(!summary.sheet_data_replaced &&
                !summary.targeted_cells_replaced &&
                summary.replacement_cell_count == 0 &&
                summary.estimated_replacement_memory_usage == 0,
            prefix + " summary should not expose replacement state");
        check(summary.materialized_dirty &&
                summary.materialized_cell_count == expected_cell_count &&
                summary.estimated_materialized_memory_usage == expected_memory_usage,
            prefix + " summary should match the dirty materialized state");
    }
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

void check_reopened_clean_sheet_output(
    const std::filesystem::path& output,
    std::string_view sheet_name,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet(sheet_name);
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
}

void check_reopened_default_data_sheet_output(
    const std::filesystem::path& output,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check_reopened_clean_sheet_output(output, "Data", scenario,
        [prefix](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                prefix + " reopened output should keep default source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                prefix + " reopened output should keep default source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "placeholder-a1",
                prefix + " reopened output should read source-backed A1");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_b1.number_value() == 1.0,
                prefix + " reopened output should read source-backed B1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                prefix + " reopened output should read source-backed A2");
        });
}

std::filesystem::path write_two_sheet_source_with_shift_memory_formula(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("anchor-a1")});
        data.append_row({fastxlsx::CellView::formula("A9+A9+A9+A9+A9")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me")});
    }
    writer.close();

    return path;
}

void check_reopened_shift_output(
    const std::filesystem::path& output,
    std::string_view scenario,
    const std::function<void(fastxlsx::WorksheetEditor&)>& inspect)
{
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data");
    const std::string prefix(scenario);

    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened output should not expose stale diagnostics");
    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened output should materialize as clean public state");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened output should not expose dirty diagnostics");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened output should not expose dirty worksheet names");

    inspect(reopened_sheet);

    check(!reopened_editor.has_pending_changes() &&
            !reopened_sheet.has_pending_changes(),
        prefix + " reopened readback should keep public state clean");
    check(reopened_editor.pending_change_count() == 0 &&
            reopened_editor.pending_materialized_cell_count() == 0 &&
            reopened_editor.estimated_pending_materialized_memory_usage() == 0 &&
            reopened_editor.pending_replacement_cell_count() == 0 &&
            reopened_editor.estimated_pending_replacement_memory_usage() == 0 &&
            reopened_editor.pending_worksheet_edits().empty(),
        prefix + " reopened readback should keep dirty diagnostics empty");
    check(reopened_editor.pending_materialized_worksheet_names().empty() &&
            reopened_editor.pending_replacement_worksheet_names().empty(),
        prefix + " reopened readback should keep dirty worksheet names empty");
    check(!reopened_editor.last_edit_error().has_value(),
        prefix + " reopened readback should keep last_edit_error empty");
}

void test_public_worksheet_editor_shift_valid_after_invalid_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-invalid-recovery-source.xlsx");

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-row-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-row-recovery-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-row-recovery-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-row-recovery-post-noop-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
            "invalid-to-valid shift should reject invalid row insertion first");
        check(threw_fastxlsx_error([&] { sheet.delete_rows(1048576, 2); }),
            "invalid-to-valid shift should reject invalid row deletion range");
        check(editor.last_edit_error().has_value(),
            "invalid-to-valid row shift should retain the invalid shift diagnostic");
        check(!sheet.has_pending_changes() && editor.pending_materialized_cell_count() == 0,
            "invalid-to-valid row shift failures should leave the borrowed handle clean");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "invalid-to-valid row shift failures should not expose dirty memory");
        check(sheet.cell_count() == 3,
            "invalid-to-valid row shift failures should preserve sparse count");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "invalid-to-valid row shift failures should preserve source-backed cells");

        sheet.insert_rows(2, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "valid insert_rows after invalid shifts pre-save recovery");
        check(!editor.last_edit_error().has_value(),
            "valid insert_rows after invalid shifts should clear the prior diagnostic");
        check(sheet.has_pending_changes(),
            "valid insert_rows after invalid shifts should dirty the same borrowed handle");
        check(sheet.cell_count() == 3 && editor.pending_materialized_cell_count() == 3,
            "valid insert_rows after invalid shifts should keep aggregate sparse count stable");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "valid insert_rows after invalid shifts should expose shifted dirty memory");
        check(sheet.get_cell("A3").text_value() == "placeholder-a2",
            "valid insert_rows after invalid shifts should move source-backed rows");
        check(!sheet.try_cell("A2").has_value(),
            "valid insert_rows after invalid shifts should remove the old shifted coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const auto inspect_reopened_row_shift_recovery =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "invalid-to-valid row recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                    "invalid-to-valid row recovery reopened output should expose shifted bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "invalid-to-valid row recovery reopened output should keep source A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "invalid-to-valid row recovery reopened output should keep source B1");
                const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
                check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a3.text_value() == "placeholder-a2",
                    "invalid-to-valid row recovery reopened output should read shifted A2");
                check(!reopened_sheet.try_cell("A2").has_value(),
                    "invalid-to-valid row recovery reopened output should keep old A2 absent");
            };
        check_reopened_shift_output(output, "invalid-to-valid row shift recovery",
            inspect_reopened_row_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid row shift noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "invalid-to-valid row shift noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "invalid-to-valid row shift noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "invalid-to-valid row shift noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "invalid-to-valid row shift noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "invalid-to-valid row shift noop save should not expose dirty summaries");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "invalid-to-valid row shift noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "invalid-to-valid row shift noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "invalid-to-valid row shift noop save should keep output entries stable");
        check_reopened_shift_output(noop_output, "invalid-to-valid row shift noop save",
            inspect_reopened_row_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid row shift second noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "invalid-to-valid row shift second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "invalid-to-valid row shift second noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "invalid-to-valid row shift second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "invalid-to-valid row shift second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "invalid-to-valid row shift second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "invalid-to-valid row shift second noop save");
        const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "invalid-to-valid row shift second noop output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "invalid-to-valid row shift second noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "invalid-to-valid row shift second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "invalid-to-valid row shift second noop save should leave the first no-op output unchanged");
        check_reopened_shift_output(second_noop_output,
            "invalid-to-valid row shift second noop save",
            inspect_reopened_row_shift_recovery);

        sheet.set_cell("C3", fastxlsx::CellValue::text("post-noop-invalid-row-recovery"));
        check(sheet.has_pending_changes(),
            "invalid-to-valid row shift post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 4,
            "invalid-to-valid row shift post-noop edit should add one sparse cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
            "invalid-to-valid row shift post-noop edit should expand bounds to C3");
        const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
        check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
                post_noop_cell.text_value() == "post-noop-invalid-row-recovery",
            "invalid-to-valid row shift post-noop edit should be readable before save");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "invalid-to-valid row shift post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid row shift post-noop save should clean the materialized session");
        check(editor.pending_change_count() == 2,
            "invalid-to-valid row shift post-noop save should record the second handoff");
        check(editor.has_pending_changes(),
            "invalid-to-valid row shift post-noop save should retain staged materialized handoffs");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "invalid-to-valid row shift post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "invalid-to-valid row shift post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "invalid-to-valid row shift post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "invalid-to-valid row shift post-noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "invalid-to-valid row shift post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "invalid-to-valid row shift post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "invalid-to-valid row shift post-noop save should leave the repeat no-op output unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="C3")",
            "invalid-to-valid row shift post-noop save should write the post-noop C3 cell");
        check_reopened_shift_output(post_noop_output, "invalid-to-valid row shift post-noop save",
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "invalid-to-valid row shift post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                    "invalid-to-valid row shift post-noop save reopened output should expose post-noop bounds");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "invalid-to-valid row shift post-noop save reopened output should keep source B1");
                const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
                check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a3.text_value() == "placeholder-a2",
                    "invalid-to-valid row shift post-noop save reopened output should keep shifted A2");
                const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
                check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_c3.text_value() == "post-noop-invalid-row-recovery",
                    "invalid-to-valid row shift post-noop save reopened output should keep post-noop edit");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                    reopened_sheet.row_cells(3);
                check(row_three.size() == 2 &&
                        row_three[0].reference.row == 3 &&
                        row_three[0].reference.column == 1 &&
                        row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[0].value.text_value() == "placeholder-a2" &&
                        row_three[1].reference.row == 3 &&
                        row_three[1].reference.column == 3 &&
                        row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[1].value.text_value() == "post-noop-invalid-row-recovery",
                    "invalid-to-valid row shift post-noop row_cells should expose shifted row order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    reopened_sheet.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "placeholder-a1" &&
                        column_one[1].reference.row == 3 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    "invalid-to-valid row shift post-noop column_cells should expose shifted source row");
                check(!reopened_sheet.try_cell("A2").has_value(),
                    "invalid-to-valid row shift post-noop save reopened output should keep old A2 absent");
            });
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-column-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-column-recovery-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-column-recovery-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-column-recovery-post-noop-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
            "invalid-to-valid shift should reject invalid column insertion first");
        check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
            "invalid-to-valid shift should reject invalid column deletion range");
        check(editor.last_edit_error().has_value(),
            "invalid-to-valid column shift should retain the invalid shift diagnostic");
        check(!sheet.has_pending_changes() && editor.pending_materialized_cell_count() == 0,
            "invalid-to-valid column shift failures should leave the borrowed handle clean");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "invalid-to-valid column shift failures should not expose dirty memory");
        check(sheet.cell_count() == 3,
            "invalid-to-valid column shift failures should preserve sparse count");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "invalid-to-valid column shift failures should preserve source-backed cells");

        sheet.insert_columns(2, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "valid insert_columns after invalid shifts pre-save recovery");
        check(!editor.last_edit_error().has_value(),
            "valid insert_columns after invalid shifts should clear the prior diagnostic");
        check(sheet.has_pending_changes(),
            "valid insert_columns after invalid shifts should dirty the same borrowed handle");
        check(sheet.cell_count() == 3 && editor.pending_materialized_cell_count() == 3,
            "valid insert_columns after invalid shifts should keep aggregate sparse count stable");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "valid insert_columns after invalid shifts should expose shifted dirty memory");
        const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0,
            "valid insert_columns after invalid shifts should move source-backed columns");
        check(!sheet.try_cell("B1").has_value(),
            "valid insert_columns after invalid shifts should remove the old shifted coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const auto inspect_reopened_column_shift_recovery =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "invalid-to-valid column recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                    "invalid-to-valid column recovery reopened output should expose shifted bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "invalid-to-valid column recovery reopened output should keep source A1");
                const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
                check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_c1.number_value() == 1.0,
                    "invalid-to-valid column recovery reopened output should read shifted B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "invalid-to-valid column recovery reopened output should keep source A2");
                check(!reopened_sheet.try_cell("B1").has_value(),
                    "invalid-to-valid column recovery reopened output should keep old B1 absent");
            };
        check_reopened_shift_output(output, "invalid-to-valid column shift recovery",
            inspect_reopened_column_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid column shift noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "invalid-to-valid column shift noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "invalid-to-valid column shift noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "invalid-to-valid column shift noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "invalid-to-valid column shift noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "invalid-to-valid column shift noop save should not expose dirty summaries");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "invalid-to-valid column shift noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "invalid-to-valid column shift noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "invalid-to-valid column shift noop save should keep output entries stable");
        check_reopened_shift_output(noop_output, "invalid-to-valid column shift noop save",
            inspect_reopened_column_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid column shift second noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "invalid-to-valid column shift second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "invalid-to-valid column shift second noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "invalid-to-valid column shift second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "invalid-to-valid column shift second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "invalid-to-valid column shift second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "invalid-to-valid column shift second noop save");
        const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "invalid-to-valid column shift second noop output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "invalid-to-valid column shift second noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "invalid-to-valid column shift second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "invalid-to-valid column shift second noop save should leave the first no-op output unchanged");
        check_reopened_shift_output(second_noop_output,
            "invalid-to-valid column shift second noop save",
            inspect_reopened_column_shift_recovery);

        sheet.set_cell("D2", fastxlsx::CellValue::text("post-noop-invalid-column-recovery"));
        check(sheet.has_pending_changes(),
            "invalid-to-valid column shift post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 4,
            "invalid-to-valid column shift post-noop edit should add one sparse cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
            "invalid-to-valid column shift post-noop edit should expand bounds to D2");
        const fastxlsx::CellValue post_noop_cell = sheet.get_cell("D2");
        check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
                post_noop_cell.text_value() == "post-noop-invalid-column-recovery",
            "invalid-to-valid column shift post-noop edit should be readable before save");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "invalid-to-valid column shift post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "invalid-to-valid column shift post-noop save should clean the materialized session");
        check(editor.pending_change_count() == 2,
            "invalid-to-valid column shift post-noop save should record the second handoff");
        check(editor.has_pending_changes(),
            "invalid-to-valid column shift post-noop save should retain staged materialized handoffs");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "invalid-to-valid column shift post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "invalid-to-valid column shift post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "invalid-to-valid column shift post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "invalid-to-valid column shift post-noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "invalid-to-valid column shift post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "invalid-to-valid column shift post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "invalid-to-valid column shift post-noop save should leave the repeat no-op output unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="D2")",
            "invalid-to-valid column shift post-noop save should write the post-noop D2 cell");
        check_reopened_shift_output(post_noop_output, "invalid-to-valid column shift post-noop save",
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "invalid-to-valid column shift post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                    "invalid-to-valid column shift post-noop save reopened output should expose post-noop bounds");
                const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
                check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_c1.number_value() == 1.0,
                    "invalid-to-valid column shift post-noop save reopened output should keep shifted B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "invalid-to-valid column shift post-noop save reopened output should keep source A2");
                const fastxlsx::CellValue reopened_d2 = reopened_sheet.get_cell("D2");
                check(reopened_d2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_d2.text_value() == "post-noop-invalid-column-recovery",
                    "invalid-to-valid column shift post-noop save reopened output should keep post-noop edit");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    reopened_sheet.row_cells(2);
                check(row_two.size() == 2 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2" &&
                        row_two[1].reference.row == 2 &&
                        row_two[1].reference.column == 4 &&
                        row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[1].value.text_value() == "post-noop-invalid-column-recovery",
                    "invalid-to-valid column shift post-noop row_cells should expose row-two order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                    reopened_sheet.column_cells(3);
                check(column_three.size() == 1 &&
                        column_three[0].reference.row == 1 &&
                        column_three[0].reference.column == 3 &&
                        column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_three[0].value.number_value() == 1.0,
                    "invalid-to-valid column shift post-noop column_cells should expose shifted number");
                check(!reopened_sheet.try_cell("B1").has_value(),
                    "invalid-to-valid column shift post-noop save reopened output should keep old B1 absent");
            });
    }
}

void test_public_worksheet_editor_dirty_shift_valid_after_invalid_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-recovery-source.xlsx");

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-row-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-row-recovery-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-row-recovery-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-row-recovery-post-noop-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        sheet.set_cell(4, 2, fastxlsx::CellValue::text("dirty-row-tail"));
        const std::size_t dirty_count = sheet.cell_count();
        const std::size_t dirty_memory = sheet.estimated_memory_usage();
        check(dirty_count == 4 && sheet.has_pending_changes(),
            "dirty invalid-to-valid row shift should start from a dirty sparse session");
        check(editor.pending_materialized_cell_count() == dirty_count,
            "dirty invalid-to-valid row shift should report the dirty materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "dirty invalid-to-valid row shift should report the dirty materialized memory");

        check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
            "dirty invalid-to-valid row shift should reject invalid row insertion");
        check(threw_fastxlsx_error([&] { sheet.delete_rows(1048576, 2); }),
            "dirty invalid-to-valid row shift should reject invalid row deletion range");
        check(editor.last_edit_error().has_value(),
            "dirty invalid-to-valid row shift should retain the invalid shift diagnostic");
        check(sheet.has_pending_changes() && editor.pending_materialized_cell_count() == dirty_count,
            "dirty invalid-to-valid row shift failures should preserve dirty diagnostics");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "dirty invalid-to-valid row shift failures should preserve dirty memory");
        check(sheet.cell_count() == dirty_count,
            "dirty invalid-to-valid row shift failures should preserve sparse count");
        check(sheet.get_cell("B4").text_value() == "dirty-row-tail",
            "dirty invalid-to-valid row shift failures should preserve dirty cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "dirty invalid-to-valid row shift failures should preserve source-backed cells");

        sheet.insert_rows(2, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "valid insert_rows after dirty invalid shifts pre-save recovery");
        check(!editor.last_edit_error().has_value(),
            "valid insert_rows after dirty invalid shifts should clear the prior diagnostic");
        check(sheet.has_pending_changes(),
            "valid insert_rows after dirty invalid shifts should keep the borrowed handle dirty");
        check(sheet.cell_count() == dirty_count &&
                editor.pending_materialized_cell_count() == dirty_count,
            "valid insert_rows after dirty invalid shifts should keep dirty sparse count stable");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "valid insert_rows after dirty invalid shifts should expose shifted dirty memory");
        check(sheet.get_cell("A3").text_value() == "placeholder-a2",
            "valid insert_rows after dirty invalid shifts should move source-backed rows");
        check(sheet.get_cell("B5").text_value() == "dirty-row-tail",
            "valid insert_rows after dirty invalid shifts should move dirty rows");
        check(!sheet.try_cell("A2").has_value() && !sheet.try_cell("B4").has_value(),
            "valid insert_rows after dirty invalid shifts should remove old shifted coordinates");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const auto inspect_reopened_dirty_row_shift_recovery =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "dirty invalid-to-valid row recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 2,
                    "dirty invalid-to-valid row recovery reopened output should expose shifted bounds");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "dirty invalid-to-valid row recovery reopened output should keep source B1");
                const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
                check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a3.text_value() == "placeholder-a2",
                    "dirty invalid-to-valid row recovery reopened output should read shifted source A2");
                const fastxlsx::CellValue reopened_b5 = reopened_sheet.get_cell("B5");
                check(reopened_b5.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_b5.text_value() == "dirty-row-tail",
                    "dirty invalid-to-valid row recovery reopened output should read shifted dirty cell");
                check(!reopened_sheet.try_cell("A2").has_value() &&
                        !reopened_sheet.try_cell("B4").has_value(),
                    "dirty invalid-to-valid row recovery reopened output should keep old coordinates absent");
            };
        check_reopened_shift_output(output, "dirty invalid-to-valid row shift recovery",
            inspect_reopened_dirty_row_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid row shift noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "dirty invalid-to-valid row shift noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "dirty invalid-to-valid row shift noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "dirty invalid-to-valid row shift noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "dirty invalid-to-valid row shift noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid row shift noop save should not expose dirty summaries");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "dirty invalid-to-valid row shift noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "dirty invalid-to-valid row shift noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "dirty invalid-to-valid row shift noop save should keep output entries stable");
        check_reopened_shift_output(noop_output, "dirty invalid-to-valid row shift noop save",
            inspect_reopened_dirty_row_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid row shift second noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "dirty invalid-to-valid row shift second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid row shift second noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "dirty invalid-to-valid row shift second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "dirty invalid-to-valid row shift second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "dirty invalid-to-valid row shift second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "dirty invalid-to-valid row shift second noop save");
        const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "dirty invalid-to-valid row shift second noop output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "dirty invalid-to-valid row shift second noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "dirty invalid-to-valid row shift second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "dirty invalid-to-valid row shift second noop save should leave the first no-op output unchanged");
        check_reopened_shift_output(second_noop_output,
            "dirty invalid-to-valid row shift second noop save",
            inspect_reopened_dirty_row_shift_recovery);

        sheet.set_cell("C3", fastxlsx::CellValue::text("post-noop-dirty-invalid-row-recovery"));
        check(sheet.has_pending_changes(),
            "dirty invalid-to-valid row shift post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 5,
            "dirty invalid-to-valid row shift post-noop edit should add one sparse cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 5, 3,
            "dirty invalid-to-valid row shift post-noop edit should expand bounds to C5");
        const fastxlsx::CellValue post_noop_cell = sheet.get_cell("C3");
        check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
                post_noop_cell.text_value() == "post-noop-dirty-invalid-row-recovery",
            "dirty invalid-to-valid row shift post-noop edit should be readable before save");
        check(sheet.get_cell("B5").text_value() == "dirty-row-tail",
            "dirty invalid-to-valid row shift post-noop edit should preserve shifted dirty tail");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "dirty invalid-to-valid row shift post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid row shift post-noop save should clean the materialized session");
        check(editor.pending_change_count() == 2,
            "dirty invalid-to-valid row shift post-noop save should record the second handoff");
        check(editor.has_pending_changes(),
            "dirty invalid-to-valid row shift post-noop save should retain staged materialized handoffs");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid row shift post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "dirty invalid-to-valid row shift post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "dirty invalid-to-valid row shift post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "dirty invalid-to-valid row shift post-noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "dirty invalid-to-valid row shift post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "dirty invalid-to-valid row shift post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "dirty invalid-to-valid row shift post-noop save should leave the repeat no-op output unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="C3")",
            "dirty invalid-to-valid row shift post-noop save should write the post-noop C3 cell");
        check_reopened_shift_output(post_noop_output, "dirty invalid-to-valid row shift post-noop save",
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 5,
                    "dirty invalid-to-valid row shift post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 3,
                    "dirty invalid-to-valid row shift post-noop save reopened output should expose post-noop bounds");
                const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
                check(reopened_a3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a3.text_value() == "placeholder-a2",
                    "dirty invalid-to-valid row shift post-noop save reopened output should keep shifted source A2");
                const fastxlsx::CellValue reopened_b5 = reopened_sheet.get_cell("B5");
                check(reopened_b5.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_b5.text_value() == "dirty-row-tail",
                    "dirty invalid-to-valid row shift post-noop save reopened output should keep shifted dirty tail");
                const fastxlsx::CellValue reopened_c3 = reopened_sheet.get_cell("C3");
                check(reopened_c3.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_c3.text_value() == "post-noop-dirty-invalid-row-recovery",
                    "dirty invalid-to-valid row shift post-noop save reopened output should keep post-noop edit");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                    reopened_sheet.row_cells(3);
                check(row_three.size() == 2 &&
                        row_three[0].reference.row == 3 &&
                        row_three[0].reference.column == 1 &&
                        row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[0].value.text_value() == "placeholder-a2" &&
                        row_three[1].reference.row == 3 &&
                        row_three[1].reference.column == 3 &&
                        row_three[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_three[1].value.text_value() == "post-noop-dirty-invalid-row-recovery",
                    "dirty invalid-to-valid row shift post-noop row_cells should expose shifted row order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    reopened_sheet.column_cells(2);
                check(column_two.size() == 2 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0 &&
                        column_two[1].reference.row == 5 &&
                        column_two[1].reference.column == 2 &&
                        column_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_two[1].value.text_value() == "dirty-row-tail",
                    "dirty invalid-to-valid row shift post-noop column_cells should expose source number and dirty tail");
                check(!reopened_sheet.try_cell("A2").has_value() &&
                        !reopened_sheet.try_cell("B4").has_value(),
                    "dirty invalid-to-valid row shift post-noop save reopened output should keep old coordinates absent");
            });
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-column-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-column-recovery-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-column-recovery-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-dirty-invalid-column-recovery-post-noop-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        sheet.set_cell(2, 4, fastxlsx::CellValue::text("dirty-column-tail"));
        const std::size_t dirty_count = sheet.cell_count();
        const std::size_t dirty_memory = sheet.estimated_memory_usage();
        check(dirty_count == 4 && sheet.has_pending_changes(),
            "dirty invalid-to-valid column shift should start from a dirty sparse session");
        check(editor.pending_materialized_cell_count() == dirty_count,
            "dirty invalid-to-valid column shift should report the dirty materialized count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "dirty invalid-to-valid column shift should report the dirty materialized memory");

        check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
            "dirty invalid-to-valid column shift should reject invalid column insertion");
        check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
            "dirty invalid-to-valid column shift should reject invalid column deletion range");
        check(editor.last_edit_error().has_value(),
            "dirty invalid-to-valid column shift should retain the invalid shift diagnostic");
        check(sheet.has_pending_changes() && editor.pending_materialized_cell_count() == dirty_count,
            "dirty invalid-to-valid column shift failures should preserve dirty diagnostics");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "dirty invalid-to-valid column shift failures should preserve dirty memory");
        check(sheet.cell_count() == dirty_count,
            "dirty invalid-to-valid column shift failures should preserve sparse count");
        check(sheet.get_cell("D2").text_value() == "dirty-column-tail",
            "dirty invalid-to-valid column shift failures should preserve dirty cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "dirty invalid-to-valid column shift failures should preserve source-backed cells");

        sheet.insert_columns(2, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "valid insert_columns after dirty invalid shifts pre-save recovery");
        check(!editor.last_edit_error().has_value(),
            "valid insert_columns after dirty invalid shifts should clear the prior diagnostic");
        check(sheet.has_pending_changes(),
            "valid insert_columns after dirty invalid shifts should keep the borrowed handle dirty");
        check(sheet.cell_count() == dirty_count &&
                editor.pending_materialized_cell_count() == dirty_count,
            "valid insert_columns after dirty invalid shifts should keep dirty sparse count stable");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "valid insert_columns after dirty invalid shifts should expose shifted dirty memory");
        const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0,
            "valid insert_columns after dirty invalid shifts should move source-backed columns");
        check(sheet.get_cell("E2").text_value() == "dirty-column-tail",
            "valid insert_columns after dirty invalid shifts should move dirty columns");
        check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("D2").has_value(),
            "valid insert_columns after dirty invalid shifts should remove old shifted coordinates");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const auto inspect_reopened_dirty_column_shift_recovery =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "dirty invalid-to-valid column recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
                    "dirty invalid-to-valid column recovery reopened output should expose shifted bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "dirty invalid-to-valid column recovery reopened output should keep source A1");
                const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
                check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_c1.number_value() == 1.0,
                    "dirty invalid-to-valid column recovery reopened output should read shifted B1");
                const fastxlsx::CellValue reopened_e2 = reopened_sheet.get_cell("E2");
                check(reopened_e2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_e2.text_value() == "dirty-column-tail",
                    "dirty invalid-to-valid column recovery reopened output should read shifted dirty cell");
                check(!reopened_sheet.try_cell("B1").has_value() &&
                        !reopened_sheet.try_cell("D2").has_value(),
                    "dirty invalid-to-valid column recovery reopened output should keep old coordinates absent");
            };
        check_reopened_shift_output(output, "dirty invalid-to-valid column shift recovery",
            inspect_reopened_dirty_column_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid column shift noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "dirty invalid-to-valid column shift noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "dirty invalid-to-valid column shift noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "dirty invalid-to-valid column shift noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "dirty invalid-to-valid column shift noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid column shift noop save should not expose dirty summaries");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "dirty invalid-to-valid column shift noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "dirty invalid-to-valid column shift noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "dirty invalid-to-valid column shift noop save should keep output entries stable");
        check_reopened_shift_output(noop_output,
            "dirty invalid-to-valid column shift noop save",
            inspect_reopened_dirty_column_shift_recovery);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid column shift second noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            "dirty invalid-to-valid column shift second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid column shift second noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "dirty invalid-to-valid column shift second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "dirty invalid-to-valid column shift second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "dirty invalid-to-valid column shift second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "dirty invalid-to-valid column shift second noop save");
        const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "dirty invalid-to-valid column shift second noop output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "dirty invalid-to-valid column shift second noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "dirty invalid-to-valid column shift second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "dirty invalid-to-valid column shift second noop save should leave the first no-op output unchanged");
        check_reopened_shift_output(second_noop_output,
            "dirty invalid-to-valid column shift second noop save",
            inspect_reopened_dirty_column_shift_recovery);

        sheet.set_cell("F2", fastxlsx::CellValue::text("post-noop-dirty-invalid-column-recovery"));
        check(sheet.has_pending_changes(),
            "dirty invalid-to-valid column shift post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 5,
            "dirty invalid-to-valid column shift post-noop edit should add one sparse cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 6,
            "dirty invalid-to-valid column shift post-noop edit should expand bounds to F2");
        const fastxlsx::CellValue post_noop_cell = sheet.get_cell("F2");
        check(post_noop_cell.kind() == fastxlsx::CellValueKind::Text &&
                post_noop_cell.text_value() == "post-noop-dirty-invalid-column-recovery",
            "dirty invalid-to-valid column shift post-noop edit should be readable before save");
        check(sheet.get_cell("E2").text_value() == "dirty-column-tail",
            "dirty invalid-to-valid column shift post-noop edit should preserve shifted dirty tail");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "dirty invalid-to-valid column shift post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "dirty invalid-to-valid column shift post-noop save should clean the materialized session");
        check(editor.pending_change_count() == 2,
            "dirty invalid-to-valid column shift post-noop save should record the second handoff");
        check(editor.has_pending_changes(),
            "dirty invalid-to-valid column shift post-noop save should retain staged materialized handoffs");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "dirty invalid-to-valid column shift post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "dirty invalid-to-valid column shift post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "dirty invalid-to-valid column shift post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "dirty invalid-to-valid column shift post-noop save should leave the source unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "dirty invalid-to-valid column shift post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "dirty invalid-to-valid column shift post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "dirty invalid-to-valid column shift post-noop save should leave the repeat no-op output unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="F2")",
            "dirty invalid-to-valid column shift post-noop save should write the post-noop F2 cell");
        check_reopened_shift_output(post_noop_output, "dirty invalid-to-valid column shift post-noop save",
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 5,
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 6,
                    "dirty invalid-to-valid column shift post-noop save reopened output should expose post-noop bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep source A1");
                const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
                check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_c1.number_value() == 1.0,
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep shifted source B1");
                const fastxlsx::CellValue reopened_e2 = reopened_sheet.get_cell("E2");
                check(reopened_e2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_e2.text_value() == "dirty-column-tail",
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep shifted dirty tail");
                const fastxlsx::CellValue reopened_f2 = reopened_sheet.get_cell("F2");
                check(reopened_f2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_f2.text_value() == "post-noop-dirty-invalid-column-recovery",
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep post-noop edit");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    reopened_sheet.row_cells(2);
                check(row_two.size() == 3 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2" &&
                        row_two[1].reference.row == 2 &&
                        row_two[1].reference.column == 5 &&
                        row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[1].value.text_value() == "dirty-column-tail" &&
                        row_two[2].reference.row == 2 &&
                        row_two[2].reference.column == 6 &&
                        row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[2].value.text_value() == "post-noop-dirty-invalid-column-recovery",
                    "dirty invalid-to-valid column shift post-noop row_cells should expose row-two order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                    reopened_sheet.column_cells(3);
                check(column_three.size() == 1 &&
                        column_three[0].reference.row == 1 &&
                        column_three[0].reference.column == 3 &&
                        column_three[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_three[0].value.number_value() == 1.0,
                    "dirty invalid-to-valid column shift post-noop column_cells should expose shifted number");
                check(!reopened_sheet.try_cell("B1").has_value() &&
                        !reopened_sheet.try_cell("D2").has_value(),
                    "dirty invalid-to-valid column shift post-noop save reopened output should keep old coordinates absent");
            });
    }
}

void test_public_worksheet_editor_shift_formula_out_of_bounds_references()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-formula-ref-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto check_source_package_unchanged =
        [&source, &source_entries](const char* message) {
            check(fastxlsx::test::read_zip_entries(source) == source_entries, message);
        };
    const auto check_row_ref_sparse_cells =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells, const char* message) {
            check(cells.size() == 2 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "placeholder-a2" &&
                    cells[1].reference.row == 3 &&
                    cells[1].reference.column == 3 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[1].value.text_value() == "#REF!+A:A+#REF!+B3",
                message);
        };
    const auto check_row_ref_post_noop_sparse_cells =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells, const char* message) {
            check(cells.size() == 3 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    cells[0].value.text_value() == "placeholder-a2" &&
                    cells[1].reference.row == 3 &&
                    cells[1].reference.column == 3 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[1].value.text_value() == "#REF!+A:A+#REF!+B3" &&
                    cells[2].reference.row == 3 &&
                    cells[2].reference.column == 4 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[2].value.text_value() == "C3+A1",
                message);
        };
    const auto check_column_ref_sparse_cells =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells, const char* message) {
            check(cells.size() == 2 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[0].value.number_value() == 1.0 &&
                    cells[1].reference.row == 1 &&
                    cells[1].reference.column == 3 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[1].value.text_value() == "#REF!+#REF!+1:1+C2",
                message);
        };
    const auto check_column_ref_post_noop_sparse_cells =
        [](const std::vector<fastxlsx::WorksheetCellSnapshot>& cells, const char* message) {
            check(cells.size() == 3 &&
                    cells[0].reference.row == 1 &&
                    cells[0].reference.column == 1 &&
                    cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    cells[0].value.number_value() == 1.0 &&
                    cells[1].reference.row == 1 &&
                    cells[1].reference.column == 3 &&
                    cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[1].value.text_value() == "#REF!+#REF!+1:1+C2" &&
                    cells[2].reference.row == 1 &&
                    cells[2].reference.column == 4 &&
                    cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    cells[2].value.text_value() == "C1+A1",
                message);
        };

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-noop-second-output.xlsx");
        const std::filesystem::path post_noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-post-noop-output.xlsx");
        const std::filesystem::path post_noop_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-formula-row-ref-post-noop-noop-output.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 3, fastxlsx::CellValue::formula("A1+A:A+1:1+B4"));
        sheet.delete_rows(1, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "delete_rows #REF formula pre-save shift");

        check(editor.pending_materialized_cell_count() == sheet.cell_count(),
            "delete_rows #REF formula should report shifted sparse count before save");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "delete_rows #REF formula should report shifted dirty memory before save");
        const fastxlsx::CellValue shifted_formula = sheet.get_cell("C3");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+A:A+#REF!+B3",
            "delete_rows should translate row-out-of-bounds formula references to #REF!");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_three =
            sheet.row_cells(3);
        check(shifted_row_three.size() == 1 &&
                shifted_row_three[0].reference.row == 3 &&
                shifted_row_three[0].reference.column == 3 &&
                shifted_row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_row_three[0].value.text_value() == "#REF!+A:A+#REF!+B3",
            "delete_rows #REF formula live row_cells should expose the shifted formula row");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_formula_column =
            sheet.column_cells(3);
        check(shifted_formula_column.size() == 1 &&
                shifted_formula_column[0].reference.row == 3 &&
                shifted_formula_column[0].reference.column == 3 &&
                shifted_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula_column[0].value.text_value() == "#REF!+A:A+#REF!+B3",
            "delete_rows #REF formula live column_cells should expose the shifted formula");
        check_row_ref_sparse_cells(sheet.sparse_cells(),
            "delete_rows #REF formula live sparse_cells should expose shifted source and formula order");
        check_row_ref_sparse_cells(sheet.sparse_cells("A1:C3"),
            "delete_rows #REF formula live range sparse_cells should expose shifted source and formula order");
        const std::array<fastxlsx::WorksheetCellReference, 3> row_ref_requested_cells {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {4, 3},
            fastxlsx::WorksheetCellReference {3, 3},
        };
        check_row_ref_sparse_cells(sheet.sparse_cells(row_ref_requested_cells),
            "delete_rows #REF formula live requested sparse_cells should skip the old formula coordinate");
        check(sheet.contains_cell("A1") && sheet.contains_cell("C3") &&
                !sheet.contains_cell("C4"),
            "delete_rows #REF formula live contains_cell should match shifted represented state");
        check(!sheet.try_cell("C4").has_value(),
            "delete_rows formula #REF translation should remove the old formula coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_source_package_unchanged(
            "delete_rows #REF formula save_as should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="C3"><f>#REF!+A:A+#REF!+B3</f></c>)",
            "delete_rows save_as should persist row-out-of-bounds formula references as #REF!");
        const auto inspect_reopened_row_ref_formula =
            [&check_row_ref_sparse_cells, &row_ref_requested_cells](
                fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 2,
                    "delete_rows #REF formula reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                    "delete_rows #REF formula reopened output should expose shifted bounds");
                const std::optional<fastxlsx::CellValue> reopened_c3 =
                    reopened_sheet.try_cell("C3");
                check(reopened_c3.has_value() &&
                        reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_c3->text_value() == "#REF!+A:A+#REF!+B3",
                    "delete_rows #REF formula reopened output should read translated formula");
                const std::optional<fastxlsx::CellValue> reopened_a1 =
                    reopened_sheet.try_cell("A1");
                check(reopened_a1.has_value() &&
                        reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1->text_value() == "placeholder-a2",
                    "delete_rows #REF formula reopened output should read shifted source rows");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                    reopened_sheet.row_cells(3);
                check(reopened_row_three.size() == 1 &&
                        reopened_row_three[0].reference.row == 3 &&
                        reopened_row_three[0].reference.column == 3 &&
                        reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_three[0].value.text_value() == "#REF!+A:A+#REF!+B3",
                    "delete_rows #REF formula reopened row_cells should expose the shifted formula row");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_formula_column =
                    reopened_sheet.column_cells(3);
                check(reopened_formula_column.size() == 1 &&
                        reopened_formula_column[0].reference.row == 3 &&
                        reopened_formula_column[0].reference.column == 3 &&
                        reopened_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_formula_column[0].value.text_value() == "#REF!+A:A+#REF!+B3",
                    "delete_rows #REF formula reopened column_cells should expose the shifted formula");
                check_row_ref_sparse_cells(reopened_sheet.sparse_cells(),
                    "delete_rows #REF formula reopened sparse_cells should expose shifted source and formula order");
                check_row_ref_sparse_cells(reopened_sheet.sparse_cells("A1:C3"),
                    "delete_rows #REF formula reopened range sparse_cells should expose shifted source and formula order");
                check_row_ref_sparse_cells(reopened_sheet.sparse_cells(row_ref_requested_cells),
                    "delete_rows #REF formula reopened requested sparse_cells should skip the old formula coordinate");
                check(reopened_sheet.contains_cell("A1") && reopened_sheet.contains_cell("C3") &&
                        !reopened_sheet.contains_cell("C4"),
                    "delete_rows #REF formula reopened contains_cell should match shifted represented state");
                check(!reopened_sheet.try_cell("C4").has_value(),
                    "delete_rows #REF formula reopened output should keep old coordinate absent");
            };
        check_reopened_shift_output(output, "delete_rows #REF formula",
            inspect_reopened_row_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "delete_rows #REF formula noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "delete_rows #REF formula noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "delete_rows #REF formula noop save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "delete_rows #REF formula noop save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "delete_rows #REF formula noop save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "delete_rows #REF formula noop save should keep materialized summaries empty");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "delete_rows #REF formula noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "delete_rows #REF formula noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "delete_rows #REF formula noop save should keep output entries stable");
        check_source_package_unchanged(
            "delete_rows #REF formula noop save should leave the source package unchanged");
        check_reopened_shift_output(noop_output, "delete_rows #REF formula noop save",
            inspect_reopened_row_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_rows #REF formula second noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "delete_rows #REF formula second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_rows #REF formula second noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_rows #REF formula second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_rows #REF formula second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "delete_rows #REF formula second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "delete_rows #REF formula second noop save");
        const auto second_noop_entries =
            fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "delete_rows #REF formula second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "delete_rows #REF formula second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "delete_rows #REF formula second noop save should leave the prior no-op output unchanged");
        check_source_package_unchanged(
            "delete_rows #REF formula second noop save should leave the source package unchanged");
        check_reopened_shift_output(second_noop_output,
            "delete_rows #REF formula second noop save",
            inspect_reopened_row_ref_formula);

        sheet.set_cell("D3", fastxlsx::CellValue::formula("C3+A1"));
        check(sheet.has_pending_changes(),
            "delete_rows #REF formula post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 3,
            "delete_rows #REF formula post-noop edit should add one sparse formula cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 3, 4,
            "delete_rows #REF formula post-noop edit should expand bounds to D3");
        const fastxlsx::CellValue retained_formula = sheet.get_cell("C3");
        check(retained_formula.kind() == fastxlsx::CellValueKind::Formula &&
                retained_formula.text_value() == "#REF!+A:A+#REF!+B3",
            "delete_rows #REF formula post-noop edit should preserve translated #REF formula");
        const fastxlsx::CellValue post_noop_formula = sheet.get_cell("D3");
        check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_formula.text_value() == "C3+A1",
            "delete_rows #REF formula post-noop edit should expose the new formula before save");
        const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_three =
            sheet.row_cells(3);
        check(post_noop_row_three.size() == 2 &&
                post_noop_row_three[0].reference.row == 3 &&
                post_noop_row_three[0].reference.column == 3 &&
                post_noop_row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_row_three[0].value.text_value() == "#REF!+A:A+#REF!+B3" &&
                post_noop_row_three[1].reference.row == 3 &&
                post_noop_row_three[1].reference.column == 4 &&
                post_noop_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_row_three[1].value.text_value() == "C3+A1",
            "delete_rows #REF formula post-noop live row_cells should expose formulas in sparse order");
        const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_formula_column =
            sheet.column_cells(4);
        check(post_noop_formula_column.size() == 1 &&
                post_noop_formula_column[0].reference.row == 3 &&
                post_noop_formula_column[0].reference.column == 4 &&
                post_noop_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_formula_column[0].value.text_value() == "C3+A1",
            "delete_rows #REF formula post-noop live column_cells should expose the later formula");
        check_row_ref_post_noop_sparse_cells(sheet.sparse_cells(),
            "delete_rows #REF formula post-noop live sparse_cells should expose formulas in sparse order");
        check_row_ref_post_noop_sparse_cells(sheet.sparse_cells("A1:D3"),
            "delete_rows #REF formula post-noop live range sparse_cells should expose formulas in sparse order");
        const std::array<fastxlsx::WorksheetCellReference, 4> row_ref_post_noop_requested_cells {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {4, 3},
            fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::WorksheetCellReference {3, 4},
        };
        check_row_ref_post_noop_sparse_cells(sheet.sparse_cells(row_ref_post_noop_requested_cells),
            "delete_rows #REF formula post-noop live requested sparse_cells should skip the old formula coordinate");
        check(sheet.contains_cell("A1") && sheet.contains_cell("C3") &&
                sheet.contains_cell("D3") && !sheet.contains_cell("C4"),
            "delete_rows #REF formula post-noop live contains_cell should match edited represented state");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "delete_rows #REF formula post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_rows #REF formula post-noop save should clean the materialized handle");
        check(editor.pending_change_count() == 2,
            "delete_rows #REF formula post-noop save should record the second handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_rows #REF formula post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_rows #REF formula post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_rows #REF formula post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "delete_rows #REF formula post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "delete_rows #REF formula post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "delete_rows #REF formula post-noop save should leave the repeat no-op output unchanged");
        check_source_package_unchanged(
            "delete_rows #REF formula post-noop save should leave the source package unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="C3"><f>#REF!+A:A+#REF!+B3</f></c>)",
            "delete_rows #REF formula post-noop save should keep the translated #REF formula XML");
        check_contains(post_noop_xml, R"(<c r="D3"><f>C3+A1</f></c>)",
            "delete_rows #REF formula post-noop save should write the post-noop formula");
        const auto inspect_reopened_row_ref_post_noop_formula =
            [&check_row_ref_post_noop_sparse_cells, &row_ref_post_noop_requested_cells](
                fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "delete_rows #REF formula post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 4,
                    "delete_rows #REF formula post-noop save reopened output should expose post-noop bounds");
                const std::optional<fastxlsx::CellValue> reopened_c3 =
                    reopened_sheet.try_cell("C3");
                check(reopened_c3.has_value() &&
                        reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_c3->text_value() == "#REF!+A:A+#REF!+B3",
                    "delete_rows #REF formula post-noop save reopened output should keep translated #REF formula");
                const std::optional<fastxlsx::CellValue> reopened_d3 =
                    reopened_sheet.try_cell("D3");
                check(reopened_d3.has_value() &&
                        reopened_d3->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_d3->text_value() == "C3+A1",
                    "delete_rows #REF formula post-noop save reopened output should keep post-noop formula");
                const std::optional<fastxlsx::CellValue> reopened_a1 =
                    reopened_sheet.try_cell("A1");
                check(reopened_a1.has_value() &&
                        reopened_a1->kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1->text_value() == "placeholder-a2",
                    "delete_rows #REF formula post-noop save reopened output should keep shifted source rows");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
                    reopened_sheet.row_cells(3);
                check(reopened_row_three.size() == 2 &&
                        reopened_row_three[0].reference.row == 3 &&
                        reopened_row_three[0].reference.column == 3 &&
                        reopened_row_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_three[0].value.text_value() == "#REF!+A:A+#REF!+B3" &&
                        reopened_row_three[1].reference.row == 3 &&
                        reopened_row_three[1].reference.column == 4 &&
                        reopened_row_three[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_three[1].value.text_value() == "C3+A1",
                    "delete_rows #REF formula post-noop row_cells should expose formulas in sparse order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_formula_column =
                    reopened_sheet.column_cells(4);
                check(reopened_formula_column.size() == 1 &&
                        reopened_formula_column[0].reference.row == 3 &&
                        reopened_formula_column[0].reference.column == 4 &&
                        reopened_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_formula_column[0].value.text_value() == "C3+A1",
                    "delete_rows #REF formula post-noop column_cells should expose the later formula");
                check_row_ref_post_noop_sparse_cells(reopened_sheet.sparse_cells(),
                    "delete_rows #REF formula post-noop sparse_cells should expose formulas in sparse order");
                check_row_ref_post_noop_sparse_cells(reopened_sheet.sparse_cells("A1:D3"),
                    "delete_rows #REF formula post-noop range sparse_cells should expose formulas in sparse order");
                check_row_ref_post_noop_sparse_cells(
                    reopened_sheet.sparse_cells(row_ref_post_noop_requested_cells),
                    "delete_rows #REF formula post-noop requested sparse_cells should skip the old formula coordinate");
                check(reopened_sheet.contains_cell("A1") && reopened_sheet.contains_cell("C3") &&
                        reopened_sheet.contains_cell("D3") && !reopened_sheet.contains_cell("C4"),
                    "delete_rows #REF formula post-noop contains_cell should match edited represented state");
                check(!reopened_sheet.try_cell("C4").has_value(),
                    "delete_rows #REF formula post-noop save reopened output should keep old coordinate absent");
            };
        check_reopened_shift_output(post_noop_output, "delete_rows #REF formula post-noop save",
            inspect_reopened_row_ref_post_noop_formula);
        check_reopened_shift_output(second_noop_output,
            "delete_rows #REF formula second noop output after post-noop save",
            inspect_reopened_row_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(post_noop_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_rows #REF formula post-noop noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 2,
            "delete_rows #REF formula post-noop noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_rows #REF formula post-noop noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_rows #REF formula post-noop noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_rows #REF formula post-noop noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_post_noop_noop,
            "delete_rows #REF formula post-noop noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_post_noop_noop,
            "delete_rows #REF formula post-noop noop save");
        const auto post_noop_noop_entries =
            fastxlsx::test::read_zip_entries(post_noop_noop_output);
        check(post_noop_noop_entries == post_noop_entries,
            "delete_rows #REF formula post-noop noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
            "delete_rows #REF formula post-noop noop save should leave prior post-noop output unchanged");
        check_source_package_unchanged(
            "delete_rows #REF formula post-noop noop save should leave the source package unchanged");
        check_reopened_shift_output(post_noop_noop_output,
            "delete_rows #REF formula post-noop noop save",
            inspect_reopened_row_ref_post_noop_formula);
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-noop-second-output.xlsx");
        const std::filesystem::path post_noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-post-noop-output.xlsx");
        const std::filesystem::path post_noop_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-formula-column-ref-post-noop-noop-output.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1, 4, fastxlsx::CellValue::formula("A1+A:A+1:1+D2"));
        sheet.delete_columns(1, 1);
        const std::size_t shifted_memory = sheet.estimated_memory_usage();
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "delete_columns #REF formula pre-save shift");

        check(editor.pending_materialized_cell_count() == sheet.cell_count(),
            "delete_columns #REF formula should report shifted sparse count before save");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            "delete_columns #REF formula should report shifted dirty memory before save");
        const fastxlsx::CellValue shifted_formula = sheet.get_cell("C1");
        check(shifted_formula.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula.text_value() == "#REF!+#REF!+1:1+C2",
            "delete_columns should translate column-out-of-bounds formula references to #REF!");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one =
            sheet.row_cells(1);
        check(shifted_row_one.size() == 2 &&
                shifted_row_one[0].reference.row == 1 &&
                shifted_row_one[0].reference.column == 1 &&
                shifted_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                shifted_row_one[0].value.number_value() == 1.0 &&
                shifted_row_one[1].reference.row == 1 &&
                shifted_row_one[1].reference.column == 3 &&
                shifted_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_row_one[1].value.text_value() == "#REF!+#REF!+1:1+C2",
            "delete_columns #REF formula live row_cells should expose shifted source and formula order");
        const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_formula_column =
            sheet.column_cells(3);
        check(shifted_formula_column.size() == 1 &&
                shifted_formula_column[0].reference.row == 1 &&
                shifted_formula_column[0].reference.column == 3 &&
                shifted_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula_column[0].value.text_value() == "#REF!+#REF!+1:1+C2",
            "delete_columns #REF formula live column_cells should expose the shifted formula");
        check_column_ref_sparse_cells(sheet.sparse_cells(),
            "delete_columns #REF formula live sparse_cells should expose shifted source and formula order");
        check_column_ref_sparse_cells(sheet.sparse_cells("A1:C1"),
            "delete_columns #REF formula live range sparse_cells should expose shifted source and formula order");
        const std::array<fastxlsx::WorksheetCellReference, 3> column_ref_requested_cells {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1, 4},
            fastxlsx::WorksheetCellReference {1, 3},
        };
        check_column_ref_sparse_cells(sheet.sparse_cells(column_ref_requested_cells),
            "delete_columns #REF formula live requested sparse_cells should skip the old formula coordinate");
        check(sheet.contains_cell("A1") && sheet.contains_cell("C1") &&
                !sheet.contains_cell("B1") && !sheet.contains_cell("D1"),
            "delete_columns #REF formula live contains_cell should match shifted represented state");
        check(!sheet.try_cell("D1").has_value(),
            "delete_columns formula #REF translation should remove the old formula coordinate");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_source_package_unchanged(
            "delete_columns #REF formula save_as should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="C1"><f>#REF!+#REF!+1:1+C2</f></c>)",
            "delete_columns save_as should persist column-out-of-bounds formula references as #REF!");
        const auto inspect_reopened_column_ref_formula =
            [&check_column_ref_sparse_cells, &column_ref_requested_cells](
                fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 2,
                    "delete_columns #REF formula reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 3,
                    "delete_columns #REF formula reopened output should expose shifted bounds");
                const std::optional<fastxlsx::CellValue> reopened_c1 =
                    reopened_sheet.try_cell("C1");
                check(reopened_c1.has_value() &&
                        reopened_c1->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_c1->text_value() == "#REF!+#REF!+1:1+C2",
                    "delete_columns #REF formula reopened output should read translated formula");
                const std::optional<fastxlsx::CellValue> reopened_a1 =
                    reopened_sheet.try_cell("A1");
                check(reopened_a1.has_value() &&
                        reopened_a1->kind() == fastxlsx::CellValueKind::Number &&
                        reopened_a1->number_value() == 1.0,
                    "delete_columns #REF formula reopened output should read shifted source columns");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                    reopened_sheet.row_cells(1);
                check(reopened_row_one.size() == 2 &&
                        reopened_row_one[0].reference.row == 1 &&
                        reopened_row_one[0].reference.column == 1 &&
                        reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_row_one[0].value.number_value() == 1.0 &&
                        reopened_row_one[1].reference.row == 1 &&
                        reopened_row_one[1].reference.column == 3 &&
                        reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_one[1].value.text_value() == "#REF!+#REF!+1:1+C2",
                    "delete_columns #REF formula reopened row_cells should expose shifted source and formula order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_formula_column =
                    reopened_sheet.column_cells(3);
                check(reopened_formula_column.size() == 1 &&
                        reopened_formula_column[0].reference.row == 1 &&
                        reopened_formula_column[0].reference.column == 3 &&
                        reopened_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_formula_column[0].value.text_value() == "#REF!+#REF!+1:1+C2",
                    "delete_columns #REF formula reopened column_cells should expose the shifted formula");
                check_column_ref_sparse_cells(reopened_sheet.sparse_cells(),
                    "delete_columns #REF formula reopened sparse_cells should expose shifted source and formula order");
                check_column_ref_sparse_cells(reopened_sheet.sparse_cells("A1:C1"),
                    "delete_columns #REF formula reopened range sparse_cells should expose shifted source and formula order");
                check_column_ref_sparse_cells(reopened_sheet.sparse_cells(column_ref_requested_cells),
                    "delete_columns #REF formula reopened requested sparse_cells should skip the old formula coordinate");
                check(reopened_sheet.contains_cell("A1") && reopened_sheet.contains_cell("C1") &&
                        !reopened_sheet.contains_cell("B1") && !reopened_sheet.contains_cell("D1"),
                    "delete_columns #REF formula reopened contains_cell should match shifted represented state");
                check(!reopened_sheet.try_cell("D1").has_value(),
                    "delete_columns #REF formula reopened output should keep old coordinate absent");
            };
        check_reopened_shift_output(output, "delete_columns #REF formula",
            inspect_reopened_column_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "delete_columns #REF formula noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "delete_columns #REF formula noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "delete_columns #REF formula noop save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "delete_columns #REF formula noop save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "delete_columns #REF formula noop save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "delete_columns #REF formula noop save should keep materialized summaries empty");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "delete_columns #REF formula noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "delete_columns #REF formula noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "delete_columns #REF formula noop save should keep output entries stable");
        check_source_package_unchanged(
            "delete_columns #REF formula noop save should leave the source package unchanged");
        check_reopened_shift_output(noop_output, "delete_columns #REF formula noop save",
            inspect_reopened_column_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_columns #REF formula second noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "delete_columns #REF formula second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_columns #REF formula second noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_columns #REF formula second noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_columns #REF formula second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "delete_columns #REF formula second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "delete_columns #REF formula second noop save");
        const auto second_noop_entries =
            fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            "delete_columns #REF formula second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "delete_columns #REF formula second noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "delete_columns #REF formula second noop save should leave the prior no-op output unchanged");
        check_source_package_unchanged(
            "delete_columns #REF formula second noop save should leave the source package unchanged");
        check_reopened_shift_output(second_noop_output,
            "delete_columns #REF formula second noop save",
            inspect_reopened_column_ref_formula);

        sheet.set_cell("D1", fastxlsx::CellValue::formula("C1+A1"));
        check(sheet.has_pending_changes(),
            "delete_columns #REF formula post-noop edit should dirty the saved session");
        check(sheet.cell_count() == 3,
            "delete_columns #REF formula post-noop edit should add one sparse formula cell");
        check_cell_range_equals(sheet.used_range(), 1, 1, 1, 4,
            "delete_columns #REF formula post-noop edit should expand bounds to D1");
        const fastxlsx::CellValue retained_formula = sheet.get_cell("C1");
        check(retained_formula.kind() == fastxlsx::CellValueKind::Formula &&
                retained_formula.text_value() == "#REF!+#REF!+1:1+C2",
            "delete_columns #REF formula post-noop edit should preserve translated #REF formula");
        const fastxlsx::CellValue post_noop_formula = sheet.get_cell("D1");
        check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_formula.text_value() == "C1+A1",
            "delete_columns #REF formula post-noop edit should expose the new formula before save");
        const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_one =
            sheet.row_cells(1);
        check(post_noop_row_one.size() == 3 &&
                post_noop_row_one[0].reference.row == 1 &&
                post_noop_row_one[0].reference.column == 1 &&
                post_noop_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                post_noop_row_one[0].value.number_value() == 1.0 &&
                post_noop_row_one[1].reference.row == 1 &&
                post_noop_row_one[1].reference.column == 3 &&
                post_noop_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_row_one[1].value.text_value() == "#REF!+#REF!+1:1+C2" &&
                post_noop_row_one[2].reference.row == 1 &&
                post_noop_row_one[2].reference.column == 4 &&
                post_noop_row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_row_one[2].value.text_value() == "C1+A1",
            "delete_columns #REF formula post-noop live row_cells should expose formulas in sparse order");
        const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_formula_column =
            sheet.column_cells(4);
        check(post_noop_formula_column.size() == 1 &&
                post_noop_formula_column[0].reference.row == 1 &&
                post_noop_formula_column[0].reference.column == 4 &&
                post_noop_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                post_noop_formula_column[0].value.text_value() == "C1+A1",
            "delete_columns #REF formula post-noop live column_cells should expose the later formula");
        check_column_ref_post_noop_sparse_cells(sheet.sparse_cells(),
            "delete_columns #REF formula post-noop live sparse_cells should expose formulas in sparse order");
        check_column_ref_post_noop_sparse_cells(sheet.sparse_cells("A1:D1"),
            "delete_columns #REF formula post-noop live range sparse_cells should expose formulas in sparse order");
        const std::array<fastxlsx::WorksheetCellReference, 4> column_ref_post_noop_requested_cells {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::WorksheetCellReference {1, 3},
            fastxlsx::WorksheetCellReference {1, 4},
        };
        check_column_ref_post_noop_sparse_cells(sheet.sparse_cells(column_ref_post_noop_requested_cells),
            "delete_columns #REF formula post-noop live requested sparse_cells should skip the deleted column");
        check(sheet.contains_cell("A1") && sheet.contains_cell("C1") &&
                sheet.contains_cell("D1") && !sheet.contains_cell("B1"),
            "delete_columns #REF formula post-noop live contains_cell should match edited represented state");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, "delete_columns #REF formula post-noop edit");

        editor.save_as(post_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_columns #REF formula post-noop save should clean the materialized handle");
        check(editor.pending_change_count() == 2,
            "delete_columns #REF formula post-noop save should record the second handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_columns #REF formula post-noop save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_columns #REF formula post-noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_columns #REF formula post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "delete_columns #REF formula post-noop save should leave the first output unchanged");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "delete_columns #REF formula post-noop save should leave the prior no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
            "delete_columns #REF formula post-noop save should leave the repeat no-op output unchanged");
        check_source_package_unchanged(
            "delete_columns #REF formula post-noop save should leave the source package unchanged");

        const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
        const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_xml, R"(<c r="C1"><f>#REF!+#REF!+1:1+C2</f></c>)",
            "delete_columns #REF formula post-noop save should keep the translated #REF formula XML");
        check_contains(post_noop_xml, R"(<c r="D1"><f>C1+A1</f></c>)",
            "delete_columns #REF formula post-noop save should write the post-noop formula");
        const auto inspect_reopened_column_ref_post_noop_formula =
            [&check_column_ref_post_noop_sparse_cells, &column_ref_post_noop_requested_cells](
                fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "delete_columns #REF formula post-noop save reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 4,
                    "delete_columns #REF formula post-noop save reopened output should expose post-noop bounds");
                const std::optional<fastxlsx::CellValue> reopened_c1 =
                    reopened_sheet.try_cell("C1");
                check(reopened_c1.has_value() &&
                        reopened_c1->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_c1->text_value() == "#REF!+#REF!+1:1+C2",
                    "delete_columns #REF formula post-noop save reopened output should keep translated #REF formula");
                const std::optional<fastxlsx::CellValue> reopened_d1 =
                    reopened_sheet.try_cell("D1");
                check(reopened_d1.has_value() &&
                        reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_d1->text_value() == "C1+A1",
                    "delete_columns #REF formula post-noop save reopened output should keep post-noop formula");
                const std::optional<fastxlsx::CellValue> reopened_a1 =
                    reopened_sheet.try_cell("A1");
                check(reopened_a1.has_value() &&
                        reopened_a1->kind() == fastxlsx::CellValueKind::Number &&
                        reopened_a1->number_value() == 1.0,
                    "delete_columns #REF formula post-noop save reopened output should keep shifted source columns");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                    reopened_sheet.row_cells(1);
                check(reopened_row_one.size() == 3 &&
                        reopened_row_one[0].reference.row == 1 &&
                        reopened_row_one[0].reference.column == 1 &&
                        reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_row_one[0].value.number_value() == 1.0 &&
                        reopened_row_one[1].reference.row == 1 &&
                        reopened_row_one[1].reference.column == 3 &&
                        reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_one[1].value.text_value() == "#REF!+#REF!+1:1+C2" &&
                        reopened_row_one[2].reference.row == 1 &&
                        reopened_row_one[2].reference.column == 4 &&
                        reopened_row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_row_one[2].value.text_value() == "C1+A1",
                    "delete_columns #REF formula post-noop row_cells should expose formulas in sparse order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_formula_column =
                    reopened_sheet.column_cells(4);
                check(reopened_formula_column.size() == 1 &&
                        reopened_formula_column[0].reference.row == 1 &&
                        reopened_formula_column[0].reference.column == 4 &&
                        reopened_formula_column[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_formula_column[0].value.text_value() == "C1+A1",
                    "delete_columns #REF formula post-noop column_cells should expose the later formula");
                check_column_ref_post_noop_sparse_cells(reopened_sheet.sparse_cells(),
                    "delete_columns #REF formula post-noop sparse_cells should expose formulas in sparse order");
                check_column_ref_post_noop_sparse_cells(reopened_sheet.sparse_cells("A1:D1"),
                    "delete_columns #REF formula post-noop range sparse_cells should expose formulas in sparse order");
                check_column_ref_post_noop_sparse_cells(
                    reopened_sheet.sparse_cells(column_ref_post_noop_requested_cells),
                    "delete_columns #REF formula post-noop requested sparse_cells should skip the deleted column");
                check(reopened_sheet.contains_cell("A1") && reopened_sheet.contains_cell("C1") &&
                        reopened_sheet.contains_cell("D1") && !reopened_sheet.contains_cell("B1"),
                    "delete_columns #REF formula post-noop contains_cell should match edited represented state");
                check(!reopened_sheet.try_cell("B1").has_value(),
                    "delete_columns #REF formula post-noop save reopened output should keep empty intermediate column absent");
            };
        check_reopened_shift_output(post_noop_output, "delete_columns #REF formula post-noop save",
            inspect_reopened_column_ref_post_noop_formula);
        check_reopened_shift_output(second_noop_output,
            "delete_columns #REF formula second noop output after post-noop save",
            inspect_reopened_column_ref_formula);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(post_noop_noop_output);
        check(!sheet.has_pending_changes(),
            "delete_columns #REF formula post-noop noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 2,
            "delete_columns #REF formula post-noop noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            "delete_columns #REF formula post-noop noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "delete_columns #REF formula post-noop noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "delete_columns #REF formula post-noop noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_post_noop_noop,
            "delete_columns #REF formula post-noop noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_post_noop_noop,
            "delete_columns #REF formula post-noop noop save");
        const auto post_noop_noop_entries =
            fastxlsx::test::read_zip_entries(post_noop_noop_output);
        check(post_noop_noop_entries == post_noop_entries,
            "delete_columns #REF formula post-noop noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
            "delete_columns #REF formula post-noop noop save should leave prior post-noop output unchanged");
        check_source_package_unchanged(
            "delete_columns #REF formula post-noop noop save should leave the source package unchanged");
        check_reopened_shift_output(post_noop_noop_output,
            "delete_columns #REF formula post-noop noop save",
            inspect_reopened_column_ref_post_noop_formula);
    }
}

void test_public_worksheet_editor_row_column_shift_noop_and_invalid_preserve_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-guards-source.xlsx");

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-zero-count-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-zero-count-noop-save-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before shift zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before shift zero-count no-op");
        const WorkbookEditorPublicCatalogSnapshot catalog_before_zero_count_noops =
            workbook_editor_public_catalog_snapshot(editor);

        sheet.insert_rows(2, 0);
        sheet.delete_rows(2, 0);
        sheet.insert_columns(2, 0);
        sheet.delete_columns(2, 0);
        check(!editor.last_edit_error().has_value(),
            "zero-count row/column shifts should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "zero-count row/column shifts should not dirty a clean materialized worksheet");
        check_workbook_editor_public_no_pending_state(
            editor, "zero-count row/column shifts");
        check(sheet.cell_count() == 3,
            "zero-count row/column shifts should preserve sparse cell count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "zero-count row/column shifts should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "zero-count row/column shifts should not contribute pending materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "zero-count row/column shifts should not contribute pending materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "zero-count row/column shifts");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "zero-count row/column shifts should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "zero-count row/column shifts should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "zero-count row/column shifts should preserve source A2");
        check_workbook_editor_public_catalog_preserved(editor, catalog_before_zero_count_noops,
            "zero-count row/column shifts");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_save,
            "zero-count row/column shift save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_save,
            "zero-count row/column shift save");
        check_workbook_editor_public_no_pending_state(
            editor, "zero-count row/column shift save");
        check(!sheet.has_pending_changes(),
            "zero-count row/column shift save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "zero-count row/column shift save should not queue replacement diagnostics");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "zero-count row/column shift save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "zero-count row/column shift save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "zero-count row/column shift save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_noop,
            "zero-count row/column shift noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_noop,
            "zero-count row/column shift noop save");
        check_workbook_editor_public_no_pending_state(
            editor, "zero-count row/column shift noop save");
        check(!sheet.has_pending_changes(),
            "zero-count row/column shift noop save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "zero-count row/column shift noop save should not queue replacement diagnostics");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "zero-count row/column shift noop save should still copy source entries");
        check(noop_entries == output_entries,
            "zero-count row/column shift noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "zero-count row/column shift noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "zero-count row/column shift noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-nonzero-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-nonzero-noop-save-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before shift nonzero no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before shift nonzero no-op");
        const WorkbookEditorPublicCatalogSnapshot catalog_before_nonzero_noops =
            workbook_editor_public_catalog_snapshot(editor);

        sheet.insert_rows(10, 1);
        sheet.insert_columns(10, 1);
        sheet.delete_rows(10, 1);
        sheet.delete_columns(10, 1);
        check(!editor.last_edit_error().has_value(),
            "nonzero row/column shift no-ops should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "nonzero row/column shift no-ops should not dirty a clean materialized worksheet");
        check_workbook_editor_public_no_pending_state(
            editor, "nonzero row/column shift no-ops");
        check(sheet.cell_count() == 3,
            "nonzero row/column shift no-ops should preserve sparse cell count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "nonzero row/column shift no-ops should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "nonzero row/column shift no-ops should not contribute pending materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "nonzero row/column shift no-ops should not contribute pending materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "nonzero row/column shift no-ops");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "nonzero row/column shift no-ops should preserve source-backed cells");
        check_workbook_editor_public_catalog_preserved(editor, catalog_before_nonzero_noops,
            "nonzero row/column shift no-ops");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "save_as after nonzero row/column shift no-ops should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "save_as after nonzero row/column shift no-ops should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "shift nonzero no-op");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "nonzero row/column shift no-op save should keep materialized handle clean");
        check(editor.pending_change_count() == 0,
            "nonzero row/column shift no-op save should keep edit count empty");
        check(editor.pending_materialized_worksheet_names().empty(),
            "nonzero row/column shift no-op save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "nonzero row/column shift no-op save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "nonzero row/column shift no-op save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "nonzero row/column shift no-op save should keep materialized summaries empty");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "nonzero row/column shift no-op save");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "nonzero row/column shift no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "nonzero row/column shift no-op save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "nonzero row/column shift no-op save should still copy source entries");
        check(noop_entries == output_entries,
            "nonzero row/column shift no-op save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "nonzero row/column shift no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(noop_output, "shift nonzero no-op save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-max-boundary-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-max-boundary-noop-save-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before max-boundary shift no-ops");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before max-boundary shift no-ops");
        const WorkbookEditorPublicCatalogSnapshot catalog_before_max_boundary_noops =
            workbook_editor_public_catalog_snapshot(editor);

        sheet.insert_rows(1048576, 1);
        sheet.insert_columns(16384, 1);
        sheet.delete_rows(1048576, 1);
        sheet.delete_columns(16384, 1);
        check(!editor.last_edit_error().has_value(),
            "max-boundary row/column shift no-ops should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "max-boundary row/column shift no-ops should not dirty a clean materialized worksheet");
        check_workbook_editor_public_no_pending_state(
            editor, "max-boundary row/column shift no-ops");
        check(sheet.cell_count() == 3,
            "max-boundary row/column shift no-ops should preserve sparse cell count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "max-boundary row/column shift no-ops should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "max-boundary row/column shift no-ops should not contribute pending materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "max-boundary row/column shift no-ops should not contribute pending materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "max-boundary row/column shift no-ops");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                sheet.get_cell("B1").number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "placeholder-a2",
            "max-boundary row/column shift no-ops should preserve source-backed cells");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_max_boundary_noops,
            "max-boundary row/column shift no-ops");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "max-boundary row/column shift no-op save should keep materialized handle clean");
        check_workbook_editor_public_no_pending_state(
            editor, "max-boundary row/column shift no-op save");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "max-boundary row/column shift no-op save");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save,
            "max-boundary row/column shift no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save,
            "max-boundary row/column shift no-op save");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "save_as after max-boundary row/column shift no-ops should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "save_as after max-boundary row/column shift no-ops should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "shift max-boundary no-op");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "max-boundary row/column shift no-op save repeat should keep materialized handle clean");
        check_workbook_editor_public_no_pending_state(
            editor, "max-boundary row/column shift no-op save repeat");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "max-boundary row/column shift no-op save repeat");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "max-boundary row/column shift no-op save repeat");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "max-boundary row/column shift no-op save repeat");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "max-boundary row/column shift no-op save repeat should still copy source entries");
        check(noop_entries == output_entries,
            "max-boundary row/column shift no-op save repeat should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "max-boundary row/column shift no-op save repeat should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "shift max-boundary no-op save repeat");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-invalid-noop-save-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
        const WorkbookEditorPublicCatalogSnapshot catalog_before_validation_failures =
            workbook_editor_public_catalog_snapshot(editor);

        bool invalid_row_failed = false;
        try {
            sheet.insert_rows(0, 1);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_row_failed = true;
        }
        check(invalid_row_failed, "insert_rows should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed insert_rows invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_rows invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_rows invalid-row failure should preserve sparse cell count");

        bool invalid_delete_rows_count_failed = false;
        try {
            sheet.delete_rows(1048576, 2);
        } catch (const fastxlsx::FastXlsxError& error) {
            invalid_delete_rows_count_failed = true;
            check_contains(error.what(), "1048576",
                "delete_rows invalid count should expose the Excel row limit");
        }
        check(invalid_delete_rows_count_failed,
            "delete_rows should reject count ranges past the Excel row limit");
        check(!sheet.has_pending_changes(),
            "delete_rows invalid-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_rows invalid-count failure should preserve sparse cell count");

        bool invalid_column_failed = false;
        try {
            sheet.insert_columns(0, 1);
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_column_failed = true;
        }
        check(invalid_column_failed,
            "insert_columns should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed insert_columns invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_columns invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_columns invalid-column failure should preserve sparse cell count");

        bool invalid_count_failed = false;
        try {
            sheet.delete_columns(16384, 2);
        } catch (const fastxlsx::FastXlsxError& error) {
            invalid_count_failed = true;
            check_contains(error.what(), "16384",
                "delete_columns invalid count should expose the Excel column limit");
        }
        check(invalid_count_failed,
            "delete_columns should reject count ranges past the Excel column limit");
        check(!sheet.has_pending_changes(),
            "delete_columns invalid-count failure should not dirty the materialized worksheet");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "delete_columns invalid-count failure should preserve source cells");

        check(threw_fastxlsx_error([&] {
            sheet.insert_rows(0, 0);
        }), "insert_rows should validate invalid row numbers before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed insert_rows invalid zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_rows invalid zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_rows invalid zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_rows(0, 0);
        }), "delete_rows should validate invalid row numbers before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed delete_rows invalid zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_rows invalid zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_rows invalid zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.insert_columns(0, 0);
        }), "insert_columns should validate invalid column numbers before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed insert_columns invalid zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_columns invalid zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_columns invalid zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_columns(0, 0);
        }), "delete_columns should validate invalid column numbers before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed delete_columns invalid zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_columns invalid zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_columns invalid zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.insert_rows(1048577, 0);
        }), "insert_rows should validate row upper bounds before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed insert_rows upper-bound zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_rows upper-bound zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_rows upper-bound zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_rows(1048577, 0);
        }), "delete_rows should validate row upper bounds before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed delete_rows upper-bound zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_rows upper-bound zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_rows upper-bound zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.insert_columns(16385, 0);
        }), "insert_columns should validate column upper bounds before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed insert_columns upper-bound zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_columns upper-bound zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_columns upper-bound zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_columns(16385, 0);
        }), "delete_columns should validate column upper bounds before zero-count no-op");
        check(editor.last_edit_error().has_value(),
            "failed delete_columns upper-bound zero-count mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_columns upper-bound zero-count failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_columns upper-bound zero-count failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.insert_rows(1048577, 1);
        }), "insert_rows should reject row upper bounds for nonzero shifts");
        check(editor.last_edit_error().has_value(),
            "failed insert_rows upper-bound nonzero mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_rows upper-bound nonzero failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_rows upper-bound nonzero failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_rows(1048577, 1);
        }), "delete_rows should reject row upper bounds for nonzero shifts");
        check(editor.last_edit_error().has_value(),
            "failed delete_rows upper-bound nonzero mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_rows upper-bound nonzero failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_rows upper-bound nonzero failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.insert_columns(16385, 1);
        }), "insert_columns should reject column upper bounds for nonzero shifts");
        check(editor.last_edit_error().has_value(),
            "failed insert_columns upper-bound nonzero mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "insert_columns upper-bound nonzero failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "insert_columns upper-bound nonzero failure should preserve sparse cell count");

        check(threw_fastxlsx_error([&] {
            sheet.delete_columns(16385, 1);
        }), "delete_columns should reject column upper bounds for nonzero shifts");
        check(editor.last_edit_error().has_value(),
            "failed delete_columns upper-bound nonzero mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "delete_columns upper-bound nonzero failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "delete_columns upper-bound nonzero failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "invalid shift start/count failures should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "invalid shift start/count failures should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "invalid shift start/count failures should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "shift validation failures");
        check(editor.pending_materialized_worksheet_names().empty(),
            "shift validation failures should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "shift validation failures should not contribute pending materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "shift validation failures should not contribute pending materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "shift validation failures");
        check_workbook_editor_public_catalog_preserved(editor, catalog_before_validation_failures,
            "shift validation failures");
        const std::optional<std::string> shift_validation_error = editor.last_edit_error();
        check(shift_validation_error.has_value(),
            "shift validation failures should retain the last validation diagnostic");

        editor.save_as(output);
        check(editor.last_edit_error() == shift_validation_error,
            "save_as after shift validation failures should preserve the validation diagnostic");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "save_as after shift validation failures should copy source entries");
        check_reopened_default_data_sheet_output(output, "shift validation failure");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "save_as after shift validation failures should leave the source package unchanged");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_noops =
            workbook_editor_public_catalog_snapshot(editor);
        sheet.insert_rows(2, 0);
        sheet.delete_rows(2, 0);
        sheet.insert_columns(2, 0);
        sheet.delete_columns(2, 0);
        check(!editor.last_edit_error().has_value(),
            "valid zero-count shifts after validation failures should clear diagnostics");
        check(!sheet.has_pending_changes(),
            "valid zero-count shifts after validation failures should keep the materialized handle clean");
        check_workbook_editor_public_no_pending_state(
            editor, "valid zero-count shifts after validation failures");
        check(sheet.cell_count() == 3,
            "valid zero-count shifts after validation failures should preserve sparse cell count");
        check(editor.pending_materialized_worksheet_names().empty(),
            "valid zero-count shifts after validation failures should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "valid zero-count shifts after validation failures should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "valid zero-count shifts after validation failures should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "valid zero-count shifts after validation failures");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_recovery_noops,
            "valid zero-count shifts after validation failures");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "shift validation recovery noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 0,
            "shift validation recovery noop save should keep edit count empty");
        check(editor.pending_materialized_worksheet_names().empty(),
            "shift validation recovery noop save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "shift validation recovery noop save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "shift validation recovery noop save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "shift validation recovery noop save should keep materialized summaries empty");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "shift validation recovery noop save");
        check(!editor.last_edit_error().has_value(),
            "shift validation recovery noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "shift validation recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "shift validation recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == source_entries,
            "shift validation recovery noop save should still copy source entries");
        check(noop_entries == output_entries,
            "shift validation recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "shift validation recovery noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(noop_output,
            "shift validation recovery noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-row-overflow-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-row-overflow-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1048576, 1, fastxlsx::CellValue::text("row-edge"));
        const std::size_t dirty_count = sheet.cell_count();
        const std::size_t dirty_memory = sheet.estimated_memory_usage();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_row_overflow =
            workbook_editor_public_catalog_snapshot(editor);
        bool row_overflow_failed = false;
        try {
            sheet.insert_rows(1, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            row_overflow_failed = true;
            check_contains(error.what(), "1048576",
                "insert_rows overflow should expose the Excel row limit");
        }
        check(row_overflow_failed,
            "insert_rows should reject shifts that move a represented cell past the row limit");
        check(sheet.cell_count() == dirty_count,
            "insert_rows overflow failure should preserve sparse cell count");
        check(editor.pending_materialized_cell_count() == dirty_count,
            "insert_rows overflow failure should preserve pending materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "insert_rows overflow failure should preserve pending materialized memory");
        check(sheet.get_cell("A1048576").text_value() == "row-edge",
            "insert_rows overflow failure should preserve the edge cell");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "insert_rows overflow failure should not shift earlier cells");
        check(sheet.has_pending_changes(),
            "insert_rows overflow failure should preserve prior dirty state");
        check_workbook_editor_public_catalog_preserved(editor, catalog_before_row_overflow,
            "insert_rows overflow failure");
        const std::optional<std::string> row_overflow_error = editor.last_edit_error();
        check(row_overflow_error.has_value(),
            "insert_rows overflow failure should retain the shift overflow diagnostic");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "insert_rows overflow failure", row_overflow_error);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_rows overflow failure should leave the source package unchanged");

        editor.save_as(output);
        check(editor.last_edit_error() == row_overflow_error,
            "insert_rows overflow save_as should preserve the shift overflow diagnostic");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_rows overflow save_as should leave the source package unchanged");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B1048576"/>)",
            "insert_rows overflow save_as should preserve the unshifted dirty dimension");
        check_contains(worksheet_xml, "row-edge",
            "insert_rows overflow save_as should persist the pre-existing edge cell");
        check_contains(worksheet_xml, "placeholder-a1",
            "insert_rows overflow save_as should keep source cells unshifted");
        check_not_contains(worksheet_xml, R"(r="A1048577")",
            "insert_rows overflow save_as should not write an out-of-bounds row");
        const auto inspect_reopened_row_overflow =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "insert_rows overflow recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1048576, 2,
                    "insert_rows overflow recovery reopened output should keep edge bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "insert_rows overflow recovery reopened output should keep source A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "insert_rows overflow recovery reopened output should keep source B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "insert_rows overflow recovery reopened output should keep source A2");
                const fastxlsx::CellValue reopened_edge =
                    reopened_sheet.get_cell("A1048576");
                check(reopened_edge.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_edge.text_value() == "row-edge",
                    "insert_rows overflow recovery reopened output should keep edge A1048576");
            };
        check_reopened_clean_sheet_output(output, "Data", "insert_rows overflow recovery",
            inspect_reopened_row_overflow);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "insert_rows overflow noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "insert_rows overflow noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "insert_rows overflow noop save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "insert_rows overflow noop save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "insert_rows overflow noop save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "insert_rows overflow noop save should keep materialized summaries empty");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_rows overflow noop save should leave the source package unchanged");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "insert_rows overflow noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "insert_rows overflow noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "insert_rows overflow noop save should keep output entries stable");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "insert_rows overflow noop save", inspect_reopened_row_overflow);
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-column-overflow-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-shift-column-overflow-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1, 16384, fastxlsx::CellValue::text("column-edge"));
        const std::size_t dirty_count = sheet.cell_count();
        const std::size_t dirty_memory = sheet.estimated_memory_usage();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_column_overflow =
            workbook_editor_public_catalog_snapshot(editor);
        bool column_overflow_failed = false;
        try {
            sheet.insert_columns(1, 1);
        } catch (const fastxlsx::FastXlsxError& error) {
            column_overflow_failed = true;
            check_contains(error.what(), "16384",
                "insert_columns overflow should expose the Excel column limit");
        }
        check(column_overflow_failed,
            "insert_columns should reject shifts that move a represented cell past the column limit");
        check(sheet.cell_count() == dirty_count,
            "insert_columns overflow failure should preserve sparse cell count");
        check(editor.pending_materialized_cell_count() == dirty_count,
            "insert_columns overflow failure should preserve pending materialized cell count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory,
            "insert_columns overflow failure should preserve pending materialized memory");
        check(sheet.get_cell("XFD1").text_value() == "column-edge",
            "insert_columns overflow failure should preserve the edge cell");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "insert_columns overflow failure should not shift earlier cells");
        check(sheet.has_pending_changes(),
            "insert_columns overflow failure should preserve prior dirty state");
        check_workbook_editor_public_catalog_preserved(editor, catalog_before_column_overflow,
            "insert_columns overflow failure");
        const std::optional<std::string> column_overflow_error = editor.last_edit_error();
        check(column_overflow_error.has_value(),
            "insert_columns overflow failure should retain the shift overflow diagnostic");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "insert_columns overflow failure", column_overflow_error);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_columns overflow failure should leave the source package unchanged");

        editor.save_as(output);
        check(editor.last_edit_error() == column_overflow_error,
            "insert_columns overflow save_as should preserve the shift overflow diagnostic");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_columns overflow save_as should leave the source package unchanged");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:XFD2"/>)",
            "insert_columns overflow save_as should preserve the unshifted dirty dimension");
        check_contains(worksheet_xml, "column-edge",
            "insert_columns overflow save_as should persist the pre-existing edge cell");
        check_contains(worksheet_xml, "placeholder-a1",
            "insert_columns overflow save_as should keep source cells unshifted");
        check_not_contains(worksheet_xml, R"(r="XFE1")",
            "insert_columns overflow save_as should not write an out-of-bounds column");
        const auto inspect_reopened_column_overflow =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "insert_columns overflow recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 16384,
                    "insert_columns overflow recovery reopened output should keep edge bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "placeholder-a1",
                    "insert_columns overflow recovery reopened output should keep source A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "insert_columns overflow recovery reopened output should keep source B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "insert_columns overflow recovery reopened output should keep source A2");
                const fastxlsx::CellValue reopened_edge = reopened_sheet.get_cell("XFD1");
                check(reopened_edge.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_edge.text_value() == "column-edge",
                    "insert_columns overflow recovery reopened output should keep edge XFD1");
            };
        check_reopened_clean_sheet_output(output, "Data", "insert_columns overflow recovery",
            inspect_reopened_column_overflow);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "insert_columns overflow noop save should keep materialized handle clean");
        check(editor.pending_change_count() == 1,
            "insert_columns overflow noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "insert_columns overflow noop save should keep dirty materialized names empty");
        check(editor.pending_materialized_cell_count() == 0,
            "insert_columns overflow noop save should keep aggregate dirty cell count empty");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "insert_columns overflow noop save should keep dirty memory estimate empty");
        check(editor.pending_worksheet_edits().empty(),
            "insert_columns overflow noop save should keep materialized summaries empty");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "insert_columns overflow noop save should leave the source package unchanged");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "insert_columns overflow noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "insert_columns overflow noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "insert_columns overflow noop save should keep output entries stable");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "insert_columns overflow noop save", inspect_reopened_column_overflow);
    }
}

void test_public_worksheet_editor_shift_memory_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_shift_memory_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-memory-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-memory-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-memory-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-memory-second-noop-output.xlsx");
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
    const auto check_shift_memory_guard_snapshots =
        [](fastxlsx::WorksheetEditor& observed_sheet,
           std::string_view scenario) {
            const std::string prefix(scenario);

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                observed_sheet.sparse_cells();
            check(cells.size() == 2,
                prefix + " sparse_cells should expose only source-backed cells");
            if (cells.size() == 2) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[0].value.text_value() == "anchor-a1",
                    prefix + " sparse_cells should keep source-backed A1 first");
                check(cells[1].reference.row == 2 &&
                        cells[1].reference.column == 1 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        cells[1].value.text_value() == "A9+A9+A9+A9+A9",
                    prefix + " sparse_cells should keep original formula A2 second");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> range_cells =
                observed_sheet.sparse_cells("A1:A3");
            check(range_cells.size() == 2,
                prefix + " range sparse_cells should skip the rejected shifted coordinate");
            if (range_cells.size() == 2) {
                check(range_cells[0].reference.row == 1 &&
                        range_cells[0].reference.column == 1 &&
                        range_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        range_cells[0].value.text_value() == "anchor-a1",
                    prefix + " range sparse_cells should keep source-backed A1 first");
                check(range_cells[1].reference.row == 2 &&
                        range_cells[1].reference.column == 1 &&
                        range_cells[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                        range_cells[1].value.text_value() == "A9+A9+A9+A9+A9",
                    prefix + " range sparse_cells should keep original formula A2 second");
            }

            const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
                fastxlsx::WorksheetCellReference {2, 1},
                fastxlsx::WorksheetCellReference {3, 1},
                fastxlsx::WorksheetCellReference {1, 1},
                fastxlsx::WorksheetCellReference {2, 1},
            };
            const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
                observed_sheet.sparse_cells(requested_refs);
            check(requested_cells.size() == 3,
                prefix + " requested sparse_cells should skip rejected A3 and keep duplicate A2");
            if (requested_cells.size() == 3) {
                check(requested_cells[0].reference.row == 2 &&
                        requested_cells[0].reference.column == 1 &&
                        requested_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[0].value.text_value() == "A9+A9+A9+A9+A9",
                    prefix + " requested sparse_cells should keep original A2 first");
                check(requested_cells[1].reference.row == 1 &&
                        requested_cells[1].reference.column == 1 &&
                        requested_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        requested_cells[1].value.text_value() == "anchor-a1",
                    prefix + " requested sparse_cells should keep A1 after skipped A3");
                check(requested_cells[2].reference.row == 2 &&
                        requested_cells[2].reference.column == 1 &&
                        requested_cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        requested_cells[2].value.text_value() == "A9+A9+A9+A9+A9",
                    prefix + " requested sparse_cells should preserve duplicate A2");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                observed_sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[0].value.text_value() == "anchor-a1",
                prefix + " row_cells should keep source-backed A1");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                observed_sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_two[0].value.text_value() == "A9+A9+A9+A9+A9",
                prefix + " row_cells should keep original formula A2");
            check(observed_sheet.row_cells(3).empty(),
                prefix + " row_cells should keep rejected shifted row empty");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                observed_sheet.column_cells(1);
            check(column_one.size() == 2 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "anchor-a1" &&
                    column_one[1].reference.row == 2 &&
                    column_one[1].reference.column == 1 &&
                    column_one[1].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_one[1].value.text_value() == "A9+A9+A9+A9+A9",
                prefix + " column_cells should keep source-backed A1 and A2");
            check(observed_sheet.column_cells(2).empty(),
                prefix + " column_cells should keep gap column empty");
        };

    bool failed = false;
    try {
        sheet.insert_rows(2, 1);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
            "insert_rows formula translation should expose memory budget diagnostics");
    }
    check(failed,
        "insert_rows should reject formula translation that exceeds memory budget");
    check(editor.last_edit_error().has_value(),
        "failed insert_rows memory-budget mutation should update last_edit_error");
    check_contains(*editor.last_edit_error(), "CellStore memory_budget_bytes guardrail exceeded",
        "last_edit_error should retain the insert_rows memory-budget diagnostic");
    check(!sheet.has_pending_changes(),
        "failed insert_rows memory-budget mutation should not dirty the materialized session");
    check(!editor.has_pending_changes(),
        "failed insert_rows memory-budget mutation should not dirty the editor");
    check_workbook_editor_public_no_pending_state(
        editor, "failed insert_rows memory-budget mutation");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed insert_rows memory-budget mutation should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed insert_rows memory-budget mutation should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed insert_rows memory-budget mutation should not expose dirty materialized memory");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "failed insert_rows memory-budget mutation");
    check(sheet.cell_count() == baseline_count,
        "failed insert_rows memory-budget mutation should preserve sparse cell count");
    check(sheet.estimated_memory_usage() == baseline_memory,
        "failed insert_rows memory-budget mutation should preserve sparse memory estimate");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "failed insert_rows memory-budget mutation should leave the source package unchanged");
    const fastxlsx::CellValue original_formula = sheet.get_cell("A2");
    check(original_formula.kind() == fastxlsx::CellValueKind::Formula &&
            original_formula.text_value() == "A9+A9+A9+A9+A9",
        "failed insert_rows memory-budget mutation should preserve the original formula");
    check(!sheet.try_cell("A3").has_value(),
        "failed insert_rows memory-budget mutation should not leave a shifted formula readable");
    check_shift_memory_guard_snapshots(
        sheet, "failed insert_rows memory-budget mutation");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "save_as after failed insert_rows memory-budget mutation should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "save_as after failed insert_rows memory-budget mutation should leave the source package unchanged");
    const auto inspect_reopened_shift_memory_guard_failure =
        [&check_shift_memory_guard_snapshots](
            fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 2,
                "shift memory guard failure reopened output should keep source sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 1,
                "shift memory guard failure reopened output should keep source used range");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "anchor-a1",
                "shift memory guard failure reopened output should read source-backed A1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a2.text_value() == "A9+A9+A9+A9+A9",
                "shift memory guard failure reopened output should read original formula");
            check(!reopened_sheet.try_cell("A3").has_value(),
                "shift memory guard failure reopened output should keep rejected shift absent");
            check_shift_memory_guard_snapshots(
                reopened_sheet, "shift memory guard failure reopened output");
        };
    check_reopened_clean_sheet_output(output, "Data", "shift memory guard failure",
        inspect_reopened_shift_memory_guard_failure);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "shift memory guard failure noop save should keep sheet and editor clean");
    check(editor.pending_change_count() == 0,
        "shift memory guard failure noop save should not add a handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "shift memory guard failure noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "shift memory guard failure noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "shift memory guard failure noop save should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "shift memory guard failure noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift memory guard failure noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "shift memory guard failure noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "shift memory guard failure noop save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "shift memory guard failure noop save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift memory guard failure noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data",
        "shift memory guard failure noop save",
        inspect_reopened_shift_memory_guard_failure);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
        "shift memory guard failure second noop save should keep sheet and editor clean");
    check(editor.pending_change_count() == 0,
        "shift memory guard failure second noop save should not add a handoff");
    check(editor.pending_materialized_worksheet_names().empty(),
        "shift memory guard failure second noop save should not expose dirty worksheet names");
    check(editor.pending_materialized_cell_count() == 0,
        "shift memory guard failure second noop save should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "shift memory guard failure second noop save should not expose dirty materialized memory");
    check(editor.pending_worksheet_edits().empty(),
        "shift memory guard failure second noop save should not expose dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "shift memory guard failure second noop save");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "shift memory guard failure second noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "shift memory guard failure second noop save");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "shift memory guard failure second noop output should match the first noop output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "shift memory guard failure second noop save should leave the first noop output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift memory guard failure second noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(second_noop_output, "Data",
        "shift memory guard failure second noop save",
        inspect_reopened_shift_memory_guard_failure);
}

void test_public_worksheet_editor_exact_guardrail_budgets_allow_sparse_shifts()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-noop-output.xlsx");
    const std::filesystem::path reacquired_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-reacquired-noop-output.xlsx");
    const std::filesystem::path reacquired_second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-reacquired-second-noop-output.xlsx");
    const std::filesystem::path reacquired_post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-exact-budgets-reacquired-post-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
    const std::size_t exact_max_cells = sizing_sheet.cell_count();
    const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = exact_max_cells;
    options.memory_budget_bytes = exact_memory_budget;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
    check(sheet.cell_count() == exact_max_cells &&
            sheet.estimated_memory_usage() == exact_memory_budget,
        "exact guardrail shift setup should match source sparse budget");

    sheet.insert_rows(2, 1);
    sheet.insert_columns(2, 1);
    check(!editor.last_edit_error().has_value(),
        "exact guardrail sparse shifts should not expose diagnostics");
    check(sheet.has_pending_changes() && editor.has_pending_changes(),
        "exact guardrail sparse shifts should dirty the materialized session");
    check(sheet.cell_count() == exact_max_cells,
        "exact guardrail sparse shifts should preserve represented cell count");
    check(sheet.estimated_memory_usage() == exact_memory_budget,
        "exact guardrail sparse shifts should preserve the sparse memory estimate");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
        "exact guardrail sparse shifts should update sparse bounds");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            sheet.get_cell("C1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "placeholder-a2",
        "exact guardrail sparse shifts should expose shifted source cells");
    check(!sheet.try_cell("B1").has_value() &&
            !sheet.try_cell("A2").has_value() &&
            !sheet.try_cell("B3").has_value(),
        "exact guardrail sparse shifts should keep inserted coordinates absent");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "exact guardrail sparse shifts");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "exact guardrail sparse shift save should clean the materialized session");
    check(editor.pending_change_count() == 1,
        "exact guardrail sparse shift save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift save should clear dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "exact guardrail sparse shift save");
    check(!editor.last_edit_error().has_value(),
        "exact guardrail sparse shift save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "exact guardrail sparse shift save should leave the source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const auto make_exact_guardrail_sparse_shift_inspector =
        [](std::string message_prefix) {
            return [message_prefix](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    message_prefix + " should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                    message_prefix + " should keep shifted bounds");
                check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                        reopened_sheet.get_cell("C1").number_value() == 1.0 &&
                        reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                    message_prefix + " should read shifted source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    reopened_sheet.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "placeholder-a1" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 3 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    message_prefix + " row_cells should expose shifted row order");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    reopened_sheet.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "placeholder-a1" &&
                        column_one[1].reference.row == 3 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    message_prefix + " column_cells should expose shifted column order");
                check(!reopened_sheet.try_cell("B1").has_value() &&
                        !reopened_sheet.try_cell("A2").has_value() &&
                        !reopened_sheet.try_cell("B3").has_value(),
                    message_prefix + " should keep inserted coordinates absent");
            };
        };
    check_reopened_shift_output(output, "exact guardrail sparse shift save",
        make_exact_guardrail_sparse_shift_inspector(
            "exact guardrail sparse shift reopened output"));

    fastxlsx::WorkbookEditor budget_reacquired_editor =
        fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor budget_reacquired_sheet =
        budget_reacquired_editor.worksheet("Data", options);
    check(!budget_reacquired_sheet.has_pending_changes() &&
            !budget_reacquired_editor.has_pending_changes(),
        "exact guardrail sparse shift same-budget reacquire should materialize clean state");
    check(!budget_reacquired_editor.last_edit_error().has_value(),
        "exact guardrail sparse shift same-budget reacquire should not expose diagnostics");
    check(budget_reacquired_editor.pending_change_count() == 0 &&
            budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
            budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
            budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
            budget_reacquired_editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift same-budget reacquire should not expose dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        budget_reacquired_editor,
        "exact guardrail sparse shift same-budget reacquire");
    check(budget_reacquired_sheet.cell_count() == exact_max_cells,
        "exact guardrail sparse shift same-budget reacquire should keep sparse count");
    check(budget_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "exact guardrail sparse shift same-budget reacquire should stay within exact memory budget");
    check(budget_reacquired_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
            budget_reacquired_sheet.get_cell("C1").number_value() == 1.0 &&
            budget_reacquired_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "exact guardrail sparse shift same-budget reacquire should read shifted source cells");
    budget_reacquired_editor.save_as(reacquired_noop_output);
    check(!budget_reacquired_sheet.has_pending_changes() &&
            !budget_reacquired_editor.has_pending_changes(),
        "exact guardrail sparse shift same-budget reacquired no-op save should stay clean");
    check(budget_reacquired_editor.pending_change_count() == 0 &&
            budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
            budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
            budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
            budget_reacquired_editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift same-budget reacquired no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        budget_reacquired_editor,
        "exact guardrail sparse shift same-budget reacquired no-op save");
    const auto reacquired_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_noop_output);
    check(reacquired_noop_entries == output_entries,
        "exact guardrail sparse shift same-budget reacquired no-op output should match the saved output");
    check_reopened_shift_output(reacquired_noop_output,
        "exact guardrail sparse shift same-budget reacquired no-op save",
        make_exact_guardrail_sparse_shift_inspector(
            "exact guardrail sparse shift same-budget reacquired no-op reopened output"));

    const WorkbookEditorPublicCatalogSnapshot reacquired_catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(budget_reacquired_editor);
    const WorkbookEditorPublicSaveStateSnapshot reacquired_save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(budget_reacquired_editor);
    budget_reacquired_editor.save_as(reacquired_second_noop_output);
    check(!budget_reacquired_sheet.has_pending_changes() &&
            !budget_reacquired_editor.has_pending_changes(),
        "exact guardrail sparse shift same-budget reacquired second no-op save should stay clean");
    check(budget_reacquired_editor.pending_change_count() == 0 &&
            budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
            budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
            budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
            budget_reacquired_editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift same-budget reacquired second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        budget_reacquired_editor,
        "exact guardrail sparse shift same-budget reacquired second no-op save");
    check_workbook_editor_public_save_state_preserved(
        budget_reacquired_editor,
        reacquired_save_state_before_second_noop,
        "exact guardrail sparse shift same-budget reacquired second no-op save");
    check_workbook_editor_public_catalog_preserved(
        budget_reacquired_editor,
        reacquired_catalog_before_second_noop,
        "exact guardrail sparse shift same-budget reacquired second no-op save");
    const auto reacquired_second_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
    check(reacquired_second_noop_entries == reacquired_noop_entries,
        "exact guardrail sparse shift same-budget reacquired second no-op output should match the first reacquired no-op output");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) ==
            reacquired_noop_entries,
        "exact guardrail sparse shift same-budget reacquired second no-op save should leave the first reacquired no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "exact guardrail sparse shift same-budget reacquired second no-op save should leave the saved input package unchanged");
    check_reopened_shift_output(reacquired_second_noop_output,
        "exact guardrail sparse shift same-budget reacquired second no-op save",
        make_exact_guardrail_sparse_shift_inspector(
            "exact guardrail sparse shift same-budget reacquired second no-op reopened output"));

    budget_reacquired_sheet.set_cell("A3", fastxlsx::CellValue::text("post-budget-a3"));
    check(budget_reacquired_sheet.has_pending_changes() &&
            budget_reacquired_editor.has_pending_changes(),
        "exact guardrail sparse shift same-budget post-noop edit should dirty the reacquired session");
    check(budget_reacquired_sheet.cell_count() == exact_max_cells,
        "exact guardrail sparse shift same-budget post-noop edit should keep sparse count within budget");
    check(budget_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
        "exact guardrail sparse shift same-budget post-noop edit should stay within exact memory budget");
    check(budget_reacquired_sheet.get_cell("A3").kind() ==
                fastxlsx::CellValueKind::Text &&
            budget_reacquired_sheet.get_cell("A3").text_value() == "post-budget-a3" &&
            budget_reacquired_sheet.get_cell("C1").kind() ==
                fastxlsx::CellValueKind::Number &&
            budget_reacquired_sheet.get_cell("C1").number_value() == 1.0,
        "exact guardrail sparse shift same-budget post-noop edit should preserve shifted cells");
    check_public_state_single_data_dirty_materialized_summary(
        budget_reacquired_editor,
        budget_reacquired_sheet,
        0,
        "exact guardrail sparse shift same-budget post-noop edit");

    budget_reacquired_editor.save_as(reacquired_post_noop_output);
    check(!budget_reacquired_sheet.has_pending_changes(),
        "exact guardrail sparse shift same-budget post-noop save should clean the reacquired session");
    check(budget_reacquired_editor.pending_change_count() == 1,
        "exact guardrail sparse shift same-budget post-noop save should record one materialized handoff");
    check(budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
            budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
            budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
            budget_reacquired_editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift same-budget post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        budget_reacquired_editor,
        "exact guardrail sparse shift same-budget post-noop save");
    check(!budget_reacquired_editor.last_edit_error().has_value(),
        "exact guardrail sparse shift same-budget post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "exact guardrail sparse shift same-budget post-noop save should leave the saved input package unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_noop_output) ==
            reacquired_noop_entries,
        "exact guardrail sparse shift same-budget post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) ==
            reacquired_second_noop_entries,
        "exact guardrail sparse shift same-budget post-noop save should leave the second no-op output unchanged");
    const auto reacquired_post_noop_entries =
        fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
    const std::string reacquired_post_noop_xml =
        reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reacquired_post_noop_xml, R"(<dimension ref="A1:C3"/>)",
        "exact guardrail sparse shift same-budget post-noop output should keep shifted bounds");
    check_contains(reacquired_post_noop_xml, "post-budget-a3",
        "exact guardrail sparse shift same-budget post-noop output should write the later budget-valid edit");
    check_not_contains(reacquired_post_noop_xml, "placeholder-a2",
        "exact guardrail sparse shift same-budget post-noop output should replace the old shifted text");
    check_reopened_shift_output(reacquired_post_noop_output,
        "exact guardrail sparse shift same-budget post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "exact guardrail sparse shift same-budget post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "exact guardrail sparse shift same-budget post-noop reopened output should keep shifted bounds");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("C1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A3").text_value() == "post-budget-a3",
                "exact guardrail sparse shift same-budget post-noop reopened output should read the later edit");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 1 &&
                    row_three[0].reference.row == 3 &&
                    row_three[0].reference.column == 1 &&
                    row_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_three[0].value.text_value() == "post-budget-a3",
                "exact guardrail sparse shift same-budget post-noop row_cells should expose the later edit");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B3").has_value(),
                "exact guardrail sparse shift same-budget post-noop reopened output should keep inserted coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "exact guardrail sparse shift noop save should keep the materialized session clean");
    check(editor.pending_change_count() == 1,
        "exact guardrail sparse shift noop save should not add another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "exact guardrail sparse shift noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "exact guardrail sparse shift noop save");
    check(!editor.last_edit_error().has_value(),
        "exact guardrail sparse shift noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "exact guardrail sparse shift noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "exact guardrail sparse shift noop save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "exact guardrail sparse shift noop output should match the first save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "exact guardrail sparse shift noop save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "exact guardrail sparse shift noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "exact guardrail sparse shift noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
                "exact guardrail sparse shift noop reopened output should keep shifted bounds");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    reopened_sheet.get_cell("C1").number_value() == 1.0 &&
                    reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "exact guardrail sparse shift noop reopened output should read shifted source cells");
            check(!reopened_sheet.try_cell("B1").has_value() &&
                    !reopened_sheet.try_cell("A2").has_value() &&
                    !reopened_sheet.try_cell("B3").has_value(),
                "exact guardrail sparse shift noop reopened output should keep inserted coordinates absent");
        });
}

void test_public_worksheet_editor_delete_shifts_release_guardrail_budget_for_insertions()
{
    const auto run_case = [](bool delete_columns, bool use_memory_budget) {
        const std::string operation =
            delete_columns ? "delete_columns" : "delete_rows";
        const std::string budget =
            use_memory_budget ? "memory-budget" : "max-cells";
        const std::string scenario =
            operation + " " + budget + " budget release";
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-second-noop-output.xlsx");
        const std::filesystem::path reacquired_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-reacquired-noop-output.xlsx");
        const std::filesystem::path reacquired_second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-reacquired-second-noop-output.xlsx");
        const std::filesystem::path reacquired_post_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-" + operation + "-" +
            budget + "-release-reacquired-post-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor sizing_editor =
            fastxlsx::WorkbookEditor::open(source);
        const fastxlsx::WorksheetEditor sizing_sheet =
            sizing_editor.worksheet("Data");
        const std::size_t exact_max_cells = sizing_sheet.cell_count();
        const std::size_t exact_memory_budget =
            sizing_sheet.estimated_memory_usage();

        fastxlsx::WorksheetEditorOptions options;
        if (use_memory_budget) {
            options.memory_budget_bytes = exact_memory_budget;
        } else {
            options.max_cells = exact_max_cells;
        }

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_count = sheet.cell_count();
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool insert_failed = false;
        try {
            sheet.set_cell("D4", fastxlsx::CellValue::text("rejected"));
        } catch (const fastxlsx::FastXlsxError& error) {
            insert_failed = true;
            check_contains(error.what(),
                use_memory_budget
                    ? "CellStore memory_budget_bytes guardrail exceeded"
                    : "CellStore max_cells guardrail exceeded",
                scenario + " should reject insertion before delete shift");
        }
        check(insert_failed,
            scenario + " should reject a new sparse cell before delete shift");
        check(editor.last_edit_error().has_value(),
            scenario + " rejected insertion should update last_edit_error");
        check(!sheet.has_pending_changes() && !editor.has_pending_changes(),
            scenario + " rejected insertion should keep public state clean");
        check(sheet.cell_count() == baseline_count &&
                sheet.estimated_memory_usage() == baseline_memory,
            scenario + " rejected insertion should preserve sparse budget state");
        check(!sheet.try_cell("D4").has_value(),
            scenario + " rejected insertion should not leave the failed target readable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            scenario + " rejected insertion should leave the source package unchanged");

        if (delete_columns) {
            sheet.delete_columns(1, 1);
        } else {
            sheet.delete_rows(2, 1);
        }
        const std::size_t released_count = delete_columns ? 1U : 2U;
        check(!editor.last_edit_error().has_value(),
            scenario + " delete shift should clear the prior guardrail diagnostic");
        check(sheet.has_pending_changes() && editor.has_pending_changes(),
            scenario + " delete shift should dirty the materialized session");
        check(sheet.cell_count() == released_count,
            scenario + " delete shift should release represented sparse records");
        check(sheet.estimated_memory_usage() < baseline_memory,
            scenario + " delete shift should reduce the sparse memory estimate");

        sheet.set_cell("D4", fastxlsx::CellValue::text("ok"));
        const std::size_t final_count = released_count + 1U;
        check(sheet.cell_count() == final_count,
            scenario + " recovered insertion should use released sparse budget");
        check(sheet.estimated_memory_usage() <= exact_memory_budget,
            scenario + " recovered insertion should stay within exact memory budget");
        check(sheet.get_cell("D4").text_value() == "ok",
            scenario + " recovered insertion should be readable");
        check(!sheet.try_cell("A2").has_value() &&
                !sheet.try_cell("E4").has_value(),
            scenario + " recovered insertion should not revive deleted cells or create unrelated cells");
        if (delete_columns) {
            check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Number &&
                    sheet.get_cell("A1").number_value() == 1.0 &&
                    !sheet.try_cell("B1").has_value(),
                scenario + " should shift B1 number to A1");
        } else {
            check(sheet.get_cell("A1").text_value() == "placeholder-a1" &&
                    sheet.get_cell("B1").number_value() == 1.0,
                scenario + " should keep row-one source cells");
        }
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, scenario);

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            scenario + " save should clean the materialized session");
        check(editor.pending_change_count() == 1,
            scenario + " save should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            scenario + " save should clear dirty materialized diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            editor, scenario + " save");
        check(!editor.last_edit_error().has_value(),
            scenario + " save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            scenario + " save should leave the source package unchanged");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
            "rejected",
            scenario + " save should not leak the rejected insertion payload");
        const auto inspect_recovered_output =
            [delete_columns, final_count, scenario](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == final_count,
                    scenario + " reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                    scenario + " reopened output should expose recovered bounds");
                check(reopened_sheet.get_cell("D4").kind() ==
                            fastxlsx::CellValueKind::Text &&
                        reopened_sheet.get_cell("D4").text_value() == "ok",
                    scenario + " reopened output should read recovered insertion");
                check(!reopened_sheet.try_cell("A2").has_value() &&
                        !reopened_sheet.try_cell("E4").has_value(),
                    scenario + " reopened output should omit deleted cells and unrelated cells");
                if (delete_columns) {
                    check(reopened_sheet.get_cell("A1").kind() ==
                                fastxlsx::CellValueKind::Number &&
                            reopened_sheet.get_cell("A1").number_value() == 1.0 &&
                            !reopened_sheet.try_cell("B1").has_value(),
                        scenario + " reopened output should keep shifted number at A1");
                } else {
                    check(reopened_sheet.get_cell("A1").text_value() ==
                                "placeholder-a1" &&
                            reopened_sheet.get_cell("B1").number_value() == 1.0,
                        scenario + " reopened output should keep row-one source cells");
                }
            };
        check_reopened_clean_sheet_output(output, "Data", scenario,
            inspect_recovered_output);

        fastxlsx::WorkbookEditor budget_reacquired_editor =
            fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor budget_reacquired_sheet =
            budget_reacquired_editor.worksheet("Data", options);
        check(!budget_reacquired_sheet.has_pending_changes() &&
                !budget_reacquired_editor.has_pending_changes(),
            scenario + " same-budget reacquire should materialize clean state");
        check(!budget_reacquired_editor.last_edit_error().has_value(),
            scenario + " same-budget reacquire should not expose diagnostics");
        check(budget_reacquired_editor.pending_change_count() == 0 &&
                budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
                budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
                budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                budget_reacquired_editor.pending_worksheet_edits().empty(),
            scenario + " same-budget reacquire should not expose dirty diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            budget_reacquired_editor, scenario + " same-budget reacquire");
        check(budget_reacquired_sheet.cell_count() == final_count,
            scenario + " same-budget reacquire should keep sparse count");
        check(budget_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            scenario + " same-budget reacquire should stay within the original exact budget");
        check(budget_reacquired_sheet.get_cell("D4").kind() ==
                    fastxlsx::CellValueKind::Text &&
                budget_reacquired_sheet.get_cell("D4").text_value() == "ok",
            scenario + " same-budget reacquire should read recovered insertion");
        budget_reacquired_editor.save_as(reacquired_noop_output);
        check(!budget_reacquired_sheet.has_pending_changes() &&
                !budget_reacquired_editor.has_pending_changes(),
            scenario + " same-budget reacquired no-op save should stay clean");
        check(budget_reacquired_editor.pending_change_count() == 0 &&
                budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
                budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
                budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                budget_reacquired_editor.pending_worksheet_edits().empty(),
            scenario + " same-budget reacquired no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            budget_reacquired_editor, scenario + " same-budget reacquired no-op save");
        const auto reacquired_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_noop_output);
        check(reacquired_noop_entries == output_entries,
            scenario + " same-budget reacquired no-op output should match the saved output");
        check_not_contains(reacquired_noop_entries.at("xl/worksheets/sheet1.xml"),
            "rejected",
            scenario + " same-budget reacquired no-op output should not leak the rejected insertion payload");
        check_reopened_clean_sheet_output(reacquired_noop_output, "Data",
            scenario + " same-budget reacquired no-op",
            inspect_recovered_output);

        const WorkbookEditorPublicCatalogSnapshot
            reacquired_catalog_before_second_noop =
                workbook_editor_public_catalog_snapshot(budget_reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot
            reacquired_save_state_before_second_noop =
                workbook_editor_public_save_state_snapshot(budget_reacquired_editor);
        budget_reacquired_editor.save_as(reacquired_second_noop_output);
        check(!budget_reacquired_sheet.has_pending_changes() &&
                !budget_reacquired_editor.has_pending_changes(),
            scenario + " same-budget reacquired second no-op save should stay clean");
        check(budget_reacquired_editor.pending_change_count() == 0 &&
                budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
                budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
                budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                budget_reacquired_editor.pending_worksheet_edits().empty(),
            scenario + " same-budget reacquired second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            budget_reacquired_editor,
            scenario + " same-budget reacquired second no-op save");
        check_workbook_editor_public_save_state_preserved(
            budget_reacquired_editor,
            reacquired_save_state_before_second_noop,
            scenario + " same-budget reacquired second no-op save");
        check_workbook_editor_public_catalog_preserved(
            budget_reacquired_editor,
            reacquired_catalog_before_second_noop,
            scenario + " same-budget reacquired second no-op save");
        const auto reacquired_second_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_second_noop_output);
        check(reacquired_second_noop_entries == reacquired_noop_entries,
            scenario + " same-budget reacquired second no-op output should match the first reacquired no-op output");
        check(fastxlsx::test::read_zip_entries(reacquired_noop_output) ==
                reacquired_noop_entries,
            scenario + " same-budget reacquired second no-op save should leave the first reacquired no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            scenario + " same-budget reacquired second no-op save should leave the saved input package unchanged");
        check_not_contains(
            reacquired_second_noop_entries.at("xl/worksheets/sheet1.xml"),
            "rejected",
            scenario + " same-budget reacquired second no-op output should not leak the rejected insertion payload");
        check_reopened_clean_sheet_output(reacquired_second_noop_output, "Data",
            scenario + " same-budget reacquired second no-op",
            inspect_recovered_output);

        budget_reacquired_sheet.set_cell("D4", fastxlsx::CellValue::text("go"));
        check(budget_reacquired_sheet.has_pending_changes() &&
                budget_reacquired_editor.has_pending_changes(),
            scenario + " same-budget reacquired post-noop overwrite should dirty the session");
        check(budget_reacquired_sheet.cell_count() == final_count,
            scenario + " same-budget reacquired post-noop overwrite should keep sparse count");
        check(budget_reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            scenario + " same-budget reacquired post-noop overwrite should stay within the original exact budget");
        check(budget_reacquired_sheet.get_cell("D4").kind() ==
                    fastxlsx::CellValueKind::Text &&
                budget_reacquired_sheet.get_cell("D4").text_value() == "go",
            scenario + " same-budget reacquired post-noop overwrite should read the later value");
        check_public_state_single_data_dirty_materialized_summary(
            budget_reacquired_editor,
            budget_reacquired_sheet,
            0,
            scenario + " same-budget reacquired post-noop overwrite");

        budget_reacquired_editor.save_as(reacquired_post_noop_output);
        check(!budget_reacquired_sheet.has_pending_changes(),
            scenario + " same-budget reacquired post-noop save should clean the session");
        check(budget_reacquired_editor.pending_change_count() == 1,
            scenario + " same-budget reacquired post-noop save should record one materialized handoff");
        check(budget_reacquired_editor.pending_materialized_worksheet_names().empty() &&
                budget_reacquired_editor.pending_materialized_cell_count() == 0 &&
                budget_reacquired_editor.estimated_pending_materialized_memory_usage() == 0 &&
                budget_reacquired_editor.pending_worksheet_edits().empty(),
            scenario + " same-budget reacquired post-noop save should clear dirty diagnostics");
        check_workbook_editor_no_replacement_diagnostics(
            budget_reacquired_editor,
            scenario + " same-budget reacquired post-noop save");
        check(!budget_reacquired_editor.last_edit_error().has_value(),
            scenario + " same-budget reacquired post-noop save should keep diagnostics clear");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            scenario + " same-budget reacquired post-noop save should leave the saved input package unchanged");
        check(fastxlsx::test::read_zip_entries(reacquired_noop_output) ==
                reacquired_noop_entries,
            scenario + " same-budget reacquired post-noop save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(reacquired_second_noop_output) ==
                reacquired_second_noop_entries,
            scenario + " same-budget reacquired post-noop save should leave the second no-op output unchanged");
        const auto reacquired_post_noop_entries =
            fastxlsx::test::read_zip_entries(reacquired_post_noop_output);
        const std::string reacquired_post_noop_xml =
            reacquired_post_noop_entries.at("xl/worksheets/sheet1.xml");
        check_contains(reacquired_post_noop_xml, R"(<dimension ref="A1:D4"/>)",
            scenario + " same-budget reacquired post-noop output should keep recovered bounds");
        check_contains(reacquired_post_noop_xml, R"(<c r="D4")",
            scenario + " same-budget reacquired post-noop output should keep the recovered cell");
        check_contains(reacquired_post_noop_xml, R"(<t>go</t>)",
            scenario + " same-budget reacquired post-noop output should write the later value");
        check_not_contains(reacquired_post_noop_xml, R"(<t>ok</t>)",
            scenario + " same-budget reacquired post-noop output should replace the earlier recovered value");
        check_not_contains(reacquired_post_noop_xml, "rejected",
            scenario + " same-budget reacquired post-noop output should not leak the rejected insertion payload");
        check_reopened_clean_sheet_output(reacquired_post_noop_output, "Data",
            scenario + " same-budget reacquired post-noop save",
            [delete_columns, final_count, scenario](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == final_count,
                    scenario + " same-budget reacquired post-noop reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                    scenario + " same-budget reacquired post-noop reopened output should keep recovered bounds");
                check(reopened_sheet.get_cell("D4").kind() ==
                            fastxlsx::CellValueKind::Text &&
                        reopened_sheet.get_cell("D4").text_value() == "go",
                    scenario + " same-budget reacquired post-noop reopened output should read the later value");
                check(!reopened_sheet.try_cell("A2").has_value() &&
                        !reopened_sheet.try_cell("E4").has_value(),
                    scenario + " same-budget reacquired post-noop reopened output should omit deleted cells and unrelated cells");
                if (delete_columns) {
                    check(reopened_sheet.get_cell("A1").kind() ==
                                fastxlsx::CellValueKind::Number &&
                            reopened_sheet.get_cell("A1").number_value() == 1.0 &&
                            !reopened_sheet.try_cell("B1").has_value(),
                        scenario + " same-budget reacquired post-noop reopened output should keep shifted number at A1");
                } else {
                    check(reopened_sheet.get_cell("A1").text_value() ==
                                "placeholder-a1" &&
                            reopened_sheet.get_cell("B1").number_value() == 1.0,
                        scenario + " same-budget reacquired post-noop reopened output should keep row-one source cells");
                }
            });

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            scenario + " noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            scenario + " noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            scenario + " noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, scenario + " noop save");
        check(!editor.last_edit_error().has_value(),
            scenario + " noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop, scenario + " noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop, scenario + " noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            scenario + " noop output should match the first save");
        check_not_contains(noop_entries.at("xl/worksheets/sheet1.xml"),
            "rejected",
            scenario + " noop output should not leak the rejected insertion payload");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            scenario + " noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            scenario + " noop save",
            inspect_recovered_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            scenario + " second noop save should keep the materialized session clean");
        check(editor.pending_change_count() == 1,
            scenario + " second noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0 &&
                editor.pending_worksheet_edits().empty(),
            scenario + " second noop save should keep dirty materialized diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, scenario + " second noop save");
        check(!editor.last_edit_error().has_value(),
            scenario + " second noop save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            scenario + " second noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            scenario + " second noop save");
        const auto second_noop_entries =
            fastxlsx::test::read_zip_entries(second_noop_output);
        check(second_noop_entries == noop_entries,
            scenario + " second noop output should match the first noop output");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            scenario + " second noop save should leave the first noop output unchanged");
        check_not_contains(second_noop_entries.at("xl/worksheets/sheet1.xml"),
            "rejected",
            scenario + " second noop output should not leak the rejected insertion payload");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            scenario + " second noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(second_noop_output, "Data",
            scenario + " second noop save",
            inspect_recovered_output);
    };

    run_case(false, false);
    run_case(true, false);
    run_case(false, true);
    run_case(true, true);
}

} // namespace

int main()
{
    try {
        test_row_column_overloads_reject_invalid_coordinates();
        test_invalid_coordinate_mutations_noop_save();
        test_invalid_cell_reads_preserve_prior_diagnostic();
        test_public_worksheet_editor_shift_valid_after_invalid_preserves_state();
        test_public_worksheet_editor_dirty_shift_valid_after_invalid_preserves_state();
        test_public_worksheet_editor_shift_formula_out_of_bounds_references();
        test_public_worksheet_editor_row_column_shift_noop_and_invalid_preserve_state();
        test_public_worksheet_editor_shift_memory_guard_failure_preserves_state();
        test_public_worksheet_editor_exact_guardrail_budgets_allow_sparse_shifts();
        test_public_worksheet_editor_delete_shifts_release_guardrail_budget_for_insertions();
        std::cout << "WorkbookEditor public-state coordinate guard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state coordinate guard test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
