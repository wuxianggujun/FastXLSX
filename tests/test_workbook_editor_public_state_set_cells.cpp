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

void test_public_worksheet_editor_set_cells_replacements_drop_source_styles()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cells-full-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cells-full-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cells-full-style-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("set-cells-full-tail"),
            fastxlsx::CellView::number(3.0).with_style(non_default_style),
        });
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_cells({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("batch-replacement-unstyled")},
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::number(42.0)},
        {fastxlsx::WorksheetCellReference {1, 4},
            fastxlsx::CellValue::formula("A1+B1")},
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Text &&
            live_a1.text_value() == "batch-replacement-unstyled" &&
            !live_a1.has_style(),
        "set_cells full replacement should drop overwritten source style ids");
    const fastxlsx::CellValue live_c1 = sheet.get_cell("C1");
    check(live_c1.kind() == fastxlsx::CellValueKind::Number &&
            live_c1.number_value() == 3.0 &&
            live_c1.has_style() &&
            live_c1.style_id().value() == non_default_style.value(),
        "set_cells full replacement should preserve untouched source style ids");
    const fastxlsx::CellValue live_d1 = sheet.get_cell("D1");
    check(live_d1.kind() == fastxlsx::CellValueKind::Formula &&
            live_d1.text_value() == "A1+B1" &&
            !live_d1.has_style(),
        "set_cells full replacement should insert missing cells without style ids");
    check(sheet.cell_count() == 4,
        "set_cells full replacement should keep overwritten plus inserted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 1, 4,
        "set_cells full replacement should expand row bounds");
    check(sheet.has_pending_changes(),
        "set_cells full replacement should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 4,
        "set_cells full replacement should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_cells full replacement dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_cells full replacement should keep diagnostics clear");

    const auto check_batch_a1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "batch-replacement-unstyled" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement A1");
        };
    const auto check_batch_b1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 42.0 &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled replacement B1");
        };
    const auto check_batch_c1_projection =
        [non_default_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 3.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should preserve untouched styled C1");
        };
    const auto check_batch_d1_projection =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 4 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose inserted unstyled D1 formula");
        };
    const auto inspect_set_cells_replacement_output =
        [check_batch_a1_projection, check_batch_b1_projection,
            check_batch_c1_projection, check_batch_d1_projection](
            fastxlsx::WorksheetEditor& reopened_sheet, std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 4,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 4,
                prefix + " reopened output should keep expanded bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                reopened_sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " reopened sparse_cells should expose four records");
            if (cells.size() == 4) {
                check_batch_a1_projection(cells[0],
                    prefix + " reopened sparse_cells");
                check_batch_b1_projection(cells[1],
                    prefix + " reopened sparse_cells");
                check_batch_c1_projection(cells[2],
                    prefix + " reopened sparse_cells");
                check_batch_d1_projection(cells[3],
                    prefix + " reopened sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 4,
                prefix + " reopened row_cells should expose row-one records");
            if (row_one.size() == 4) {
                check_batch_a1_projection(row_one[0],
                    prefix + " reopened row_cells");
                check_batch_b1_projection(row_one[1],
                    prefix + " reopened row_cells");
                check_batch_c1_projection(row_one[2],
                    prefix + " reopened row_cells");
                check_batch_d1_projection(row_one[3],
                    prefix + " reopened row_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 1,
                prefix + " reopened column_cells should expose A1 only");
            if (column_one.size() == 1) {
                check_batch_a1_projection(column_one[0],
                    prefix + " reopened column_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " reopened column_cells should expose C1 only");
            if (column_three.size() == 1) {
                check_batch_c1_projection(column_three[0],
                    prefix + " reopened column_cells");
            }

            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a1.text_value() == "batch-replacement-unstyled" &&
                    !reopened_a1.has_style(),
                prefix + " reopened output should read replacement A1 without style");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_c1.number_value() == 3.0 &&
                    reopened_c1.has_style(),
                prefix + " reopened output should keep untouched C1 styled");
        };

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cells full replacement save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_cells full replacement save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D1"/>)",
        "set_cells full replacement should persist expanded dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>batch-replacement-unstyled</t></is></c>)",
        "set_cells full replacement should persist A1 without a style id");
    check_contains(worksheet_xml, R"(<c r="B1"><v>42</v></c>)",
        "set_cells full replacement should persist B1 without a style id");
    check_contains(worksheet_xml,
        R"(<c r="C1" s=")" + std::to_string(non_default_style.value()) +
            R"("><v>3</v></c>)",
        "set_cells full replacement should preserve untouched styled C1");
    check_contains(worksheet_xml, R"(<c r="D1"><f>A1+B1</f></c>)",
        "set_cells full replacement should persist inserted formula");
    check_not_contains(worksheet_xml, R"(<c r="A1" s=")",
        "set_cells full replacement should not keep the old source style on A1");
    check_not_contains(worksheet_xml, "set-cells-full-tail",
        "set_cells full replacement should omit overwritten B1 text");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "set_cells full replacement should omit overwritten A1 number");
    check_reopened_clean_sheet_output(output, "Styled", "set_cells full replacement",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_cells_replacement_output(
                reopened_sheet, "set_cells full replacement");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_set_cells_replacement_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 4,
                prefix + " should keep the saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " should expose four saved records");
            if (cells.size() == 4) {
                check_batch_a1_projection(cells[0],
                    prefix + " sparse_cells");
                check_batch_b1_projection(cells[1],
                    prefix + " sparse_cells");
                check_batch_c1_projection(cells[2],
                    prefix + " sparse_cells");
                check_batch_d1_projection(cells[3],
                    prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 4,
                prefix + " should expose row-one snapshots");
            if (row_one.size() == 4) {
                check_batch_a1_projection(row_one[0],
                    prefix + " row_cells");
                check_batch_b1_projection(row_one[1],
                    prefix + " row_cells");
                check_batch_c1_projection(row_one[2],
                    prefix + " row_cells");
                check_batch_d1_projection(row_one[3],
                    prefix + " row_cells");
            }

            check_cell_range_equals(sheet.used_range(), 1, 1, 1, 4,
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
    check_set_cells_replacement_saved_snapshot(
        pending_count_after_save, "set_cells full replacement saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_cells full replacement no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_cells full replacement no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_cells full replacement no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_cells full replacement no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_cells full replacement no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_cells full replacement no-op save should keep diagnostics clear");
    check_set_cells_replacement_saved_snapshot(
        pending_count_after_save, "set_cells full replacement no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "set_cells full replacement no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_cells full replacement no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_cells full replacement no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_cells full replacement no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_cells full replacement no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_set_cells_replacement_output(
                reopened_sheet, "set_cells full replacement no-op save");
        });
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_set_cells_replacements_drop_source_styles();
        std::cout << "WorkbookEditor public-state set cells tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state set cells test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
