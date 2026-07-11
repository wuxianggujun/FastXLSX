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

} // namespace

int main()
{
    try {
        test_row_column_overloads_reject_invalid_coordinates();
        test_invalid_coordinate_mutations_noop_save();
        test_invalid_cell_reads_preserve_prior_diagnostic();
        std::cout << "WorkbookEditor public-state coordinate guard tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state coordinate guard test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
