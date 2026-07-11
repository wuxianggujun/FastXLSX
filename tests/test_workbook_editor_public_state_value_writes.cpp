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

void test_public_worksheet_editor_set_row_values_preserves_styles_and_tail()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-row-values-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-second-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_row_values(1, {
            fastxlsx::CellValue::text("row-value-a1"),
            fastxlsx::CellValue::blank(),
            fastxlsx::CellValue::formula("A1+B1"),
        });

        check(sheet.cell_count() == 4,
            "set_row_values should update the row prefix without removing sparse tail cells");
        check(sheet.get_cell("A1").text_value() == "row-value-a1",
            "set_row_values should write the first value to column A");
        check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
            "set_row_values should represent explicit blank prefix values");
        const fastxlsx::CellValue formula = sheet.get_cell("C1");
        check(formula.kind() == fastxlsx::CellValueKind::Formula
                && formula.text_value() == "A1+B1",
            "set_row_values should write formulas without evaluating them");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values should preserve represented cells outside the written prefix");
        check(sheet.has_pending_changes(),
            "set_row_values should dirty the materialized worksheet when values change");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "set_row_values dirty summary");
        check(!editor.last_edit_error().has_value(),
            "successful set_row_values should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
            "set_row_values should refresh the dirty worksheet dimension");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>row-value-a1</t></is></c>)",
            "set_row_values should persist row prefix text cells");
        check_contains(worksheet_xml, R"(<c r="B1"/>)",
            "set_row_values should persist explicit blank prefix cells");
        check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
            "set_row_values should persist row prefix formula cells");
        check_contains(worksheet_xml, "placeholder-a2",
            "set_row_values should preserve non-target row source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "set_row_values should omit the overwritten source value");
        check_not_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_row_values should omit overwritten numeric payloads");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "set_row_values should preserve untouched worksheets");
        const auto inspect_set_row_values_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "set_row_values reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                    "set_row_values reopened output should keep written row bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "row-value-a1",
                    "set_row_values reopened output should read row prefix text");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Blank,
                    "set_row_values reopened output should read explicit blank prefix");
                const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
                check(reopened_c1.kind() == fastxlsx::CellValueKind::Formula &&
                        reopened_c1.text_value() == "A1+B1",
                    "set_row_values reopened output should read formula prefix");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "set_row_values reopened output should keep non-target row cells");
            };
        check_reopened_clean_sheet_output(output, "Data", "set_row_values",
            inspect_set_row_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_row_values no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "set_row_values no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_row_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_row_values no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_row_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_row_values no-op save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_row_values no-op output should match the first materialized output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data", "set_row_values no-op save",
            inspect_set_row_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "set_row_values second no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "set_row_values second no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_row_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_row_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "set_row_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "set_row_values second no-op save");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
            "set_row_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "set_row_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values second no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            second_noop_output, "Data", "set_row_values second no-op save",
            inspect_set_row_values_output);
    }

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-style-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-second-noop-output.xlsx");
        const std::filesystem::path style_reject_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-reject-output.xlsx");
        const std::filesystem::path style_reject_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-reject-noop-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                    fastxlsx::CellView::text("row-tail"),
                });
            }
            writer.close();
        }
        const auto style_source_entries = fastxlsx::test::read_zip_entries(style_source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
        sheet.set_row_values(1, {fastxlsx::CellValue::text("styled-row-value")});

        const fastxlsx::CellValue styled_a1 = sheet.get_cell("A1");
        check(styled_a1.kind() == fastxlsx::CellValueKind::Text
                && styled_a1.text_value() == "styled-row-value"
                && styled_a1.has_style()
                && styled_a1.style_id().value() == non_default_style.value(),
            "set_row_values should preserve source style ids on overwritten targets");
        check(sheet.get_cell("B1").text_value() == "row-tail",
            "set_row_values should leave row cells beyond the input prefix untouched");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_row_values save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_text =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value())
            + R"(" t="inlineStr"><is><t>styled-row-value</t></is></c>)";
        check_contains(worksheet_xml, styled_text,
            "set_row_values should persist value-only edits with the source style id");
        check_contains(worksheet_xml, "row-tail",
            "set_row_values should persist untouched row tail cells");
        const auto inspect_styled_set_row_values_output =
            [non_default_style](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 2,
                    "styled set_row_values reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 1, 2,
                    "styled set_row_values reopened output should keep row bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "styled-row-value" &&
                        reopened_a1.has_style() &&
                        reopened_a1.style_id().value() == non_default_style.value(),
                    "styled set_row_values reopened output should preserve source style id");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_b1.text_value() == "row-tail",
                    "styled set_row_values reopened output should keep row tail");
            };
        const auto check_styled_set_row_values_saved_snapshot =
            [&](std::size_t expected_pending_count, std::string_view scenario) {
                const std::string prefix(scenario);

                check(sheet.cell_count() == 2,
                    prefix + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    sheet.sparse_cells();
                check(cells.size() == 2,
                    prefix + " should expose the two represented records");
                if (cells.size() == 2) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "styled-row-value" &&
                            cells[0].value.has_style() &&
                            cells[0].value.style_id().value() == non_default_style.value(),
                        prefix + " should keep styled A1 first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[1].value.text_value() == "row-tail" &&
                            !cells[1].value.has_style(),
                        prefix + " should keep unstyled row tail second");
                }

                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    sheet.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "styled-row-value" &&
                        row_one[0].value.has_style() &&
                        row_one[0].value.style_id().value() ==
                            non_default_style.value() &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[1].value.text_value() == "row-tail" &&
                        !row_one[1].value.has_style(),
                    prefix + " should keep row-one styled value and source tail");

                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    sheet.column_cells(1);
                check(column_one.size() == 1 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "styled-row-value" &&
                        column_one[0].value.has_style() &&
                        column_one[0].value.style_id().value() ==
                            non_default_style.value(),
                    prefix + " should keep column-one styled A1");

                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    sheet.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_two[0].value.text_value() == "row-tail" &&
                        !column_two[0].value.has_style(),
                    prefix + " should keep column-two row tail");

                check_cell_range_equals(sheet.used_range(), 1, 1, 1, 2,
                    prefix + " should keep saved row bounds");
                check(!sheet.has_pending_changes(),
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
            };
        check_reopened_clean_sheet_output(output, "Styled", "styled set_row_values",
            inspect_styled_set_row_values_output);
        const std::size_t pending_count_after_save = editor.pending_change_count();

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "styled set_row_values no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "styled set_row_values no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "styled set_row_values no-op save should keep dirty diagnostics clear");
        check(editor.pending_worksheet_edits().empty(),
            "styled set_row_values no-op save should not leave dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "styled set_row_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "styled set_row_values no-op save should keep diagnostics clear");
        check_styled_set_row_values_saved_snapshot(
            pending_count_after_save, "styled set_row_values saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "styled set_row_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "styled set_row_values no-op save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "styled set_row_values no-op output should match the materialized output");
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_row_values no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            noop_output, "Styled", "styled set_row_values no-op save",
            inspect_styled_set_row_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "styled set_row_values second no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "styled set_row_values second no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "styled set_row_values second no-op save should keep dirty diagnostics clear");
        check(editor.pending_worksheet_edits().empty(),
            "styled set_row_values second no-op save should not leave dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "styled set_row_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "styled set_row_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "styled set_row_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "styled set_row_values second no-op save");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
            "styled set_row_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "styled set_row_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_row_values second no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            second_noop_output, "Styled", "styled set_row_values second no-op save",
            inspect_styled_set_row_values_output);

        fastxlsx::WorkbookEditor style_reject_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor reject_sheet = style_reject_editor.worksheet("Data");
        bool failed = false;
        try {
            reject_sheet.set_row_values(1, {
                fastxlsx::CellValue::text("styled-row-rejected")
                    .with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_row_values style failure should expose the unsupported StyleId boundary");
        }
        check(failed, "set_row_values should reject caller-supplied non-default StyleId values");
        check(style_reject_editor.last_edit_error().has_value(),
            "failed set_row_values style mutation should update last_edit_error");
        check(!reject_sheet.has_pending_changes(),
            "set_row_values style failure should not dirty the materialized worksheet");
        check(reject_sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row_values style failure should preserve existing cells");
        const auto style_reject_source_entries = fastxlsx::test::read_zip_entries(source);
        const WorkbookEditorPublicCatalogSnapshot catalog_before_style_reject_save =
            workbook_editor_public_catalog_snapshot(style_reject_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_style_reject_save =
            workbook_editor_public_save_state_snapshot(style_reject_editor);

        style_reject_editor.save_as(style_reject_output);
        check_workbook_editor_public_save_state_preserved(
            style_reject_editor,
            save_state_before_style_reject_save,
            "set_row_values style rejection save");
        check_workbook_editor_public_catalog_preserved(
            style_reject_editor,
            catalog_before_style_reject_save,
            "set_row_values style rejection save");
        check_workbook_editor_public_no_pending_state(
            style_reject_editor,
            "set_row_values style rejection save");
        check(!reject_sheet.has_pending_changes(),
            "set_row_values style rejection save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            style_reject_editor,
            "set_row_values style rejection save should not queue replacement diagnostics");
        const auto style_reject_output_entries =
            fastxlsx::test::read_zip_entries(style_reject_output);
        check(style_reject_output_entries == style_reject_source_entries,
            "set_row_values style rejection save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == style_reject_source_entries,
            "set_row_values style rejection save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            style_reject_output, "set_row_values style rejection save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_style_reject_noop =
            workbook_editor_public_catalog_snapshot(style_reject_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_style_reject_noop =
            workbook_editor_public_save_state_snapshot(style_reject_editor);
        style_reject_editor.save_as(style_reject_noop_output);
        check_workbook_editor_public_save_state_preserved(
            style_reject_editor,
            save_state_before_style_reject_noop,
            "set_row_values style rejection noop save");
        check_workbook_editor_public_catalog_preserved(
            style_reject_editor,
            catalog_before_style_reject_noop,
            "set_row_values style rejection noop save");
        check_workbook_editor_public_no_pending_state(
            style_reject_editor,
            "set_row_values style rejection noop save");
        check(!reject_sheet.has_pending_changes(),
            "set_row_values style rejection noop save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            style_reject_editor,
            "set_row_values style rejection noop save should not queue replacement diagnostics");
        const auto style_reject_noop_entries =
            fastxlsx::test::read_zip_entries(style_reject_noop_output);
        check(style_reject_noop_entries == style_reject_source_entries,
            "set_row_values style rejection noop save should still copy source entries");
        check(style_reject_noop_entries == style_reject_output_entries,
            "set_row_values style rejection noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == style_reject_source_entries,
            "set_row_values style rejection noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            style_reject_noop_output, "set_row_values style rejection noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-empty-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-empty-noop-second-output.xlsx");
        const std::filesystem::path invalid_row_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-invalid-row-output.xlsx");
        const std::filesystem::path invalid_row_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-invalid-row-noop-output.xlsx");
        const std::filesystem::path overflow_row_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-overflow-row-output.xlsx");
        const std::filesystem::path overflow_row_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-overflow-row-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_row_values empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_row_values empty no-op");

        const std::vector<fastxlsx::CellValue> empty_values;
        sheet.set_row_values(3, empty_values);
        check(!editor.last_edit_error().has_value(),
            "empty set_row_values should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_row_values should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "empty set_row_values should not create sparse row metadata");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "empty set_row_values no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty set_row_values no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty set_row_values no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty set_row_values no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty set_row_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty set_row_values no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save,
            "empty set_row_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save,
            "empty set_row_values no-op save");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "empty set_row_values no-op save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty set_row_values no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(output, "empty set_row_values no-op save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "empty set_row_values second no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty set_row_values second no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty set_row_values second no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty set_row_values second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty set_row_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty set_row_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "empty set_row_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "empty set_row_values second no-op save");
        check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
            "empty set_row_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "empty set_row_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty set_row_values second no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "empty set_row_values second no-op save");

        bool invalid_failed = false;
        try {
            sheet.set_row_values(0, {fastxlsx::CellValue::text("invalid-row")});
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "set_row_values should reject invalid row numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values invalid-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_row_values invalid-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_row_values invalid-row failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row_values invalid-row failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_row_values invalid-row failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values invalid-row failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "set_row_values invalid-row failure");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_row_values invalid-row failure should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_row_values invalid-row failure should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values invalid-row failure should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values invalid-row failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_row_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_invalid_row_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(invalid_row_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_invalid_row_save,
            "set_row_values invalid-row failure save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_invalid_row_save,
            "set_row_values invalid-row failure save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values invalid-row failure save");
        check(!sheet.has_pending_changes(),
            "set_row_values invalid-row failure save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values invalid-row failure save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values invalid-row failure save should not queue replacement diagnostics");
        const auto invalid_row_output_entries =
            fastxlsx::test::read_zip_entries(invalid_row_output);
        check(invalid_row_output_entries == source_entries,
            "set_row_values invalid-row failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values invalid-row failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            invalid_row_output, "set_row_values invalid-row failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_row_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_invalid_row_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(invalid_row_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_invalid_row_noop,
            "set_row_values invalid-row failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_invalid_row_noop,
            "set_row_values invalid-row failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values invalid-row failure noop save");
        check(!sheet.has_pending_changes(),
            "set_row_values invalid-row failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values invalid-row failure noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values invalid-row failure noop save should not queue replacement diagnostics");
        const auto invalid_row_noop_entries =
            fastxlsx::test::read_zip_entries(invalid_row_noop_output);
        check(invalid_row_noop_entries == source_entries,
            "set_row_values invalid-row failure noop save should still copy source entries");
        check(invalid_row_noop_entries == invalid_row_output_entries,
            "set_row_values invalid-row failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values invalid-row failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            invalid_row_noop_output, "set_row_values invalid-row failure noop save");

        bool overflow_failed = false;
        try {
            sheet.set_row_values(1048577, {fastxlsx::CellValue::text("overflow-row")});
        } catch (const fastxlsx::FastXlsxError&) {
            overflow_failed = true;
        }
        check(overflow_failed, "set_row_values should reject rows beyond the worksheet limit");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values overflow-row mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_row_values overflow-row failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_row_values overflow-row failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_row_values overflow-row failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_row_values overflow-row failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values overflow-row failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "set_row_values overflow-row failure");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_row_values overflow-row failure should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_row_values overflow-row failure should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values overflow-row failure should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values overflow-row failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_overflow_row_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_overflow_row_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(overflow_row_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_overflow_row_save,
            "set_row_values overflow-row failure save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_overflow_row_save,
            "set_row_values overflow-row failure save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values overflow-row failure save");
        check(!sheet.has_pending_changes(),
            "set_row_values overflow-row failure save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values overflow-row failure save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values overflow-row failure save should not queue replacement diagnostics");
        const auto overflow_row_output_entries =
            fastxlsx::test::read_zip_entries(overflow_row_output);
        check(overflow_row_output_entries == source_entries,
            "set_row_values overflow-row failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values overflow-row failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            overflow_row_output, "set_row_values overflow-row failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_overflow_row_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_overflow_row_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(overflow_row_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_overflow_row_noop,
            "set_row_values overflow-row failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_overflow_row_noop,
            "set_row_values overflow-row failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values overflow-row failure noop save");
        check(!sheet.has_pending_changes(),
            "set_row_values overflow-row failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values overflow-row failure noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values overflow-row failure noop save should not queue replacement diagnostics");
        const auto overflow_row_noop_entries =
            fastxlsx::test::read_zip_entries(overflow_row_noop_output);
        check(overflow_row_noop_entries == source_entries,
            "set_row_values overflow-row failure noop save should still copy source entries");
        check(overflow_row_noop_entries == overflow_row_output_entries,
            "set_row_values overflow-row failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values overflow-row failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            overflow_row_noop_output, "set_row_values overflow-row failure noop save");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-noop-output.xlsx");
        const std::filesystem::path output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-reacquire-output.xlsx");
        const std::filesystem::path noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-reacquire-noop-output.xlsx");
        const std::filesystem::path post_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-reacquire-post-noop-output.xlsx");
        const std::filesystem::path reject_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-reject-output.xlsx");
        const std::filesystem::path reject_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-budget-reject-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_row_values(3, {fastxlsx::CellValue::text("max-cells-rejected")});
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_row_values should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed, "set_row_values should enforce max_cells on staged row-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_row_values max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_row_values max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_row_values max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("A3").has_value(),
            "failed set_row_values max_cells mutation should not leave rejected cells readable");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "failed set_row_values max_cells mutation should preserve row-prefix text");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "failed set_row_values max_cells mutation should preserve row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "failed set_row_values max_cells mutation should preserve non-target rows");
        check_workbook_editor_public_no_pending_state(
            editor, "failed set_row_values max_cells mutation");
        check(editor.pending_materialized_worksheet_names().empty(),
            "failed set_row_values max_cells mutation should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "failed set_row_values max_cells mutation should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "failed set_row_values max_cells mutation should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "failed set_row_values max_cells mutation should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reject_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reject_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(reject_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_reject_save,
            "set_row_values max_cells rejection save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_reject_save,
            "set_row_values max_cells rejection save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values max_cells rejection save");
        check(!sheet.has_pending_changes(),
            "set_row_values max_cells rejection save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values max_cells rejection save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values max_cells rejection save should not queue replacement diagnostics");
        const auto reject_output_entries = fastxlsx::test::read_zip_entries(reject_output);
        check(reject_output_entries == source_entries,
            "set_row_values max_cells rejection save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values max_cells rejection save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            reject_output, "set_row_values max_cells rejection save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reject_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reject_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(reject_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_reject_noop,
            "set_row_values max_cells rejection noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_reject_noop,
            "set_row_values max_cells rejection noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_row_values max_cells rejection noop save");
        check(!sheet.has_pending_changes(),
            "set_row_values max_cells rejection noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values max_cells rejection noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_row_values max_cells rejection noop save should not queue replacement diagnostics");
        const auto reject_noop_entries = fastxlsx::test::read_zip_entries(reject_noop_output);
        check(reject_noop_entries == source_entries,
            "set_row_values max_cells rejection noop save should still copy source entries");
        check(reject_noop_entries == reject_output_entries,
            "set_row_values max_cells rejection noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values max_cells rejection noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            reject_noop_output, "set_row_values max_cells rejection noop save");

        sheet.set_row_values(1, {fastxlsx::CellValue::text("in-budget-row-value")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_row_values should clear last_edit_error");
        check(sheet.cell_count() == 3,
            "in-budget set_row_values should keep existing sparse tail records");
        check(sheet.get_cell("A1").text_value() == "in-budget-row-value",
            "in-budget set_row_values should update existing prefix cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "in-budget set_row_values should preserve row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "in-budget set_row_values should preserve non-target rows");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values guardrail recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "in-budget-row-value",
            "in-budget set_row_values should persist through save_as");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "in-budget set_row_values should persist row tail cells");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_row_values payload should not leak into saved output");
        const auto inspect_set_row_values_guardrail_recovery_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "set_row_values guardrail recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    "set_row_values guardrail recovery reopened output should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "in-budget-row-value",
                    "set_row_values guardrail recovery reopened output should read replacement A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "set_row_values guardrail recovery reopened output should keep row tail B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "set_row_values guardrail recovery reopened output should keep non-target A2");
                check(!reopened_sheet.try_cell("A3").has_value(),
                    "set_row_values guardrail recovery reopened output should keep rejected A3 absent");
            };
        const auto inspect_set_row_values_guardrail_post_noop_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "set_row_values guardrail recovery post-noop output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    "set_row_values guardrail recovery post-noop output should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "row-max-2",
                    "set_row_values guardrail recovery post-noop output should read later A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "set_row_values guardrail recovery post-noop output should keep row tail B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "set_row_values guardrail recovery post-noop output should keep non-target A2");
                check(!reopened_sheet.try_cell("A3").has_value(),
                    "set_row_values guardrail recovery post-noop output should keep rejected A3 absent");
            };
        check_reopened_clean_sheet_output(output, "Data", "set_row_values guardrail recovery",
            inspect_set_row_values_guardrail_recovery_output);
        const auto check_set_row_values_guardrail_recovery_saved_snapshot =
            [&](fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "in-budget-row-value",
                        label + " should keep replaced A1 first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep row-tail B1 second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "placeholder-a2",
                        label + " should keep non-target A2 third");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "in-budget-row-value" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should expose row-one replacement and tail cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2",
                    label + " should keep the non-target row-two cell");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "in-budget-row-value" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    label + " should keep column-one replacement and source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep the row-tail column-two cell");
                check(!handle.try_cell("A3").has_value(),
                    label + " should keep rejected A3 absent");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(editor.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(editor.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(editor.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(editor.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(editor.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    editor,
                    label + " should keep replacement diagnostics empty");
                check(!editor.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_row_values guardrail recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "set_row_values guardrail recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_row_values guardrail recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_row_values guardrail recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values guardrail recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "set_row_values guardrail recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_row_values guardrail recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_row_values guardrail recovery noop save should keep diagnostics clear");
        check_set_row_values_guardrail_recovery_saved_snapshot(
            sheet,
            pending_count_after_save,
            "set_row_values guardrail recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_row_values guardrail recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_row_values guardrail recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_row_values guardrail recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values guardrail recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "set_row_values guardrail recovery noop save",
            inspect_set_row_values_guardrail_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values guardrail recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values guardrail recovery strict-options reacquire");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquire should keep the sheet clean");
        inspect_set_row_values_guardrail_recovery_output(reacquired_sheet);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_save =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_save =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_save,
            "set_row_values guardrail recovery strict-options reacquire save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_save,
            "set_row_values guardrail recovery strict-options reacquire save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values guardrail recovery strict-options reacquire save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquire save should keep the sheet clean");
        const auto reacquire_entries =
            fastxlsx::test::read_zip_entries(output_after_reacquire);
        check(reacquire_entries == output_entries,
            "set_row_values guardrail recovery strict-options reacquire save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values guardrail recovery strict-options reacquire save should leave the source package unchanged");
        check_reopened_clean_sheet_output(output_after_reacquire, "Data",
            "set_row_values guardrail recovery strict-options reacquire save",
            inspect_set_row_values_guardrail_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_noop,
            "set_row_values guardrail recovery strict-options reacquire noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_noop,
            "set_row_values guardrail recovery strict-options reacquire noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values guardrail recovery strict-options reacquire noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquire noop save should keep the sheet clean");
        const auto reacquire_noop_entries =
            fastxlsx::test::read_zip_entries(noop_output_after_reacquire);
        check(reacquire_noop_entries == reacquire_entries,
            "set_row_values guardrail recovery strict-options reacquire noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values guardrail recovery strict-options reacquire noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output_after_reacquire, "Data",
            "set_row_values guardrail recovery strict-options reacquire noop save",
            inspect_set_row_values_guardrail_recovery_output);

        reacquired_sheet.set_row_values(1, {fastxlsx::CellValue::text("row-max-2")});
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should keep diagnostics clear");
        check(reacquired_sheet.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should dirty the sheet");
        check(reacquired_editor.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should dirty the editor");
        check(reacquired_sheet.cell_count() == 3,
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should keep sparse count stable");
        check(reacquired_editor.pending_materialized_cell_count() == 3,
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should expose dirty sparse count");
        check(reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A1").text_value() == "row-max-2",
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should overwrite A1");
        check(reacquired_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number &&
                reacquired_sheet.get_cell("B1").number_value() == 1.0,
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should keep row tail B1");
        check(reacquired_sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should keep non-target A2");
        check(!reacquired_sheet.try_cell("A3").has_value(),
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should keep rejected A3 absent");
        check_public_state_single_data_dirty_materialized_summary(
            reacquired_editor,
            reacquired_sheet,
            0,
            "set_row_values guardrail recovery strict-options reacquired post-noop edit");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_row_values guardrail recovery strict-options reacquired post-noop edit should not queue replacement diagnostics");

        reacquired_editor.save_as(post_noop_output_after_reacquire);
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values guardrail recovery strict-options reacquired post-noop save should clean the sheet");
        check(reacquired_editor.pending_change_count() == 1,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should keep one handoff");
        check(reacquired_editor.pending_materialized_worksheet_names().empty(),
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
        check(reacquired_editor.pending_materialized_cell_count() == 0,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
        check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
        check(reacquired_editor.pending_worksheet_edits().empty(),
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values guardrail recovery strict-options reacquired post-noop save should keep diagnostics clear");
        const auto post_noop_reacquire_entries =
            fastxlsx::test::read_zip_entries(post_noop_output_after_reacquire);
        const std::string post_noop_reacquire_xml =
            post_noop_reacquire_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reacquire_xml, "row-max-2",
            "set_row_values guardrail recovery strict-options reacquired post-noop save should persist the later overwrite");
        check_not_contains(post_noop_reacquire_xml, "in-budget-row-value",
            "set_row_values guardrail recovery strict-options reacquired post-noop save should replace the earlier A1 text");
        check_not_contains(post_noop_reacquire_xml, "max-cells-rejected",
            "set_row_values guardrail recovery strict-options reacquired post-noop save should not leak rejected payload");
        check_contains(post_noop_reacquire_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_row_values guardrail recovery strict-options reacquired post-noop save should keep row tail B1");
        check_contains(post_noop_reacquire_xml, "placeholder-a2",
            "set_row_values guardrail recovery strict-options reacquired post-noop save should keep non-target A2");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should leave the first no-op output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should leave the second no-op output stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values guardrail recovery strict-options reacquired post-noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(post_noop_output_after_reacquire, "Data",
            "set_row_values guardrail recovery strict-options reacquired post-noop save",
            inspect_set_row_values_guardrail_post_noop_output);
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-recovery-noop-output.xlsx");
        const std::filesystem::path output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-reacquire-output.xlsx");
        const std::filesystem::path noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-reacquire-noop-output.xlsx");
        const std::filesystem::path second_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-reacquire-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-row-values-memory-budget-reacquire-post-noop-output.xlsx");
        const std::string rejected_value =
            "set-row-values-memory-rejected-" + std::string(4096, 'r');
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
        const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.memory_budget_bytes = exact_memory_budget;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();
        check(baseline_memory == exact_memory_budget,
            "set_row_values memory-budget precondition should load with an exact sparse budget");

        bool failed = false;
        try {
            sheet.set_row_values(1, {
                fastxlsx::CellValue::text(rejected_value),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
                "set_row_values should expose CellStore memory-budget diagnostics");
        }
        check(failed,
            "set_row_values should enforce memory_budget_bytes on staged row-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_row_values memory-budget mutation should update last_edit_error");
        if (editor.last_edit_error().has_value()) {
            check_contains(*editor.last_edit_error(),
                "CellStore memory_budget_bytes guardrail exceeded",
                "last_edit_error should retain the set_row_values memory-budget diagnostic");
        }
        check(!sheet.has_pending_changes(),
            "failed set_row_values memory-budget mutation should not dirty the session");
        check(!editor.has_pending_changes(),
            "failed set_row_values memory-budget mutation should not dirty the editor");
        check(sheet.cell_count() == 3,
            "failed set_row_values memory-budget mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_row_values memory-budget mutation should preserve sparse memory estimate");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "failed set_row_values memory-budget mutation should preserve row-prefix text");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "failed set_row_values memory-budget mutation should preserve row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "failed set_row_values memory-budget mutation should preserve non-target rows");

        sheet.set_row_values(1, {fastxlsx::CellValue::text("row-mb-ok")});
        check(!editor.last_edit_error().has_value(),
            "successful set_row_values recovery should clear memory-budget diagnostics");
        check(sheet.has_pending_changes(),
            "successful set_row_values recovery should dirty the session");
        check(editor.has_pending_changes(),
            "successful set_row_values recovery should dirty the editor");
        check(sheet.cell_count() == 3,
            "successful set_row_values recovery should preserve sparse tail records");
        check(sheet.estimated_memory_usage() <= exact_memory_budget,
            "successful set_row_values recovery should stay within the exact memory budget");
        check(sheet.get_cell("A1").text_value() == "row-mb-ok",
            "successful set_row_values recovery should update the prefix cell");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "successful set_row_values recovery should preserve row tail cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "successful set_row_values recovery should preserve non-target rows");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "row-mb-ok",
            "set_row_values memory-budget recovery should persist through save_as");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_row_values memory-budget recovery should persist row tail cells");
        check_contains(worksheet_xml, "placeholder-a2",
            "set_row_values memory-budget recovery should persist non-target rows");
        check_not_contains(worksheet_xml, "set-row-values-memory-rejected",
            "rejected set_row_values memory-budget payload should not leak into saved output");
        const auto inspect_set_row_values_memory_budget_output_with_a1 =
            [](fastxlsx::WorksheetEditor& reopened_sheet,
                std::string_view expected_a1,
                std::string_view prefix) {
                const std::string label(prefix);
                check(reopened_sheet.cell_count() == 3,
                    label + " should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == expected_a1,
                    label + " should read A1");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    label + " should keep row tail B1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    label + " should keep non-target A2");
            };
        const auto inspect_set_row_values_memory_budget_recovery_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_set_row_values_memory_budget_output_with_a1(
                    reopened_sheet,
                    "row-mb-ok",
                    "set_row_values memory-budget recovery reopened output");
            };
        const auto inspect_set_row_values_memory_budget_post_noop_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_set_row_values_memory_budget_output_with_a1(
                    reopened_sheet,
                    "row-mb-2",
                    "set_row_values memory-budget recovery post-noop output");
            };
        check_reopened_clean_sheet_output(output, "Data", "set_row_values memory-budget recovery",
            inspect_set_row_values_memory_budget_recovery_output);
        const auto check_set_row_values_memory_budget_recovery_saved_snapshot =
            [&](fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "row-mb-ok",
                        label + " should keep recovered A1 first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep row-tail B1 second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "placeholder-a2",
                        label + " should keep non-target A2 third");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "row-mb-ok" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should expose row-one recovery and tail cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2",
                    label + " should keep the non-target row-two cell");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "row-mb-ok" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    label + " should keep column-one recovery and source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep the row-tail column-two cell");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(editor.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(editor.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(editor.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(editor.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(editor.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    editor,
                    label + " should keep replacement diagnostics empty");
                check(!editor.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_row_values memory-budget recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "set_row_values memory-budget recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_row_values memory-budget recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_row_values memory-budget recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values memory-budget recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "set_row_values memory-budget recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_row_values memory-budget recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_row_values memory-budget recovery noop save should keep diagnostics clear");
        check_set_row_values_memory_budget_recovery_saved_snapshot(
            sheet,
            pending_count_after_save,
            "set_row_values memory-budget recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_row_values memory-budget recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_row_values memory-budget recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_row_values memory-budget recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "set_row_values memory-budget recovery noop save",
            inspect_set_row_values_memory_budget_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values memory-budget recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquire");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquire should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_row_values memory-budget recovery strict-options reacquire should stay within the original budget");
        inspect_set_row_values_memory_budget_recovery_output(reacquired_sheet);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_save =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_save =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_save,
            "set_row_values memory-budget recovery strict-options reacquire save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_save,
            "set_row_values memory-budget recovery strict-options reacquire save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquire save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquire save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_row_values memory-budget recovery strict-options reacquire save should keep the original memory budget");
        const auto reacquire_entries =
            fastxlsx::test::read_zip_entries(output_after_reacquire);
        check(reacquire_entries == output_entries,
            "set_row_values memory-budget recovery strict-options reacquire save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery strict-options reacquire save should leave the source package unchanged");
        check_reopened_clean_sheet_output(output_after_reacquire, "Data",
            "set_row_values memory-budget recovery strict-options reacquire save",
            inspect_set_row_values_memory_budget_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_noop,
            "set_row_values memory-budget recovery strict-options reacquire noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_noop,
            "set_row_values memory-budget recovery strict-options reacquire noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquire noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquire noop save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_row_values memory-budget recovery strict-options reacquire noop save should keep the original memory budget");
        const auto reacquire_noop_entries =
            fastxlsx::test::read_zip_entries(noop_output_after_reacquire);
        check(reacquire_noop_entries == reacquire_entries,
            "set_row_values memory-budget recovery strict-options reacquire noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery strict-options reacquire noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output_after_reacquire, "Data",
            "set_row_values memory-budget recovery strict-options reacquire noop save",
            inspect_set_row_values_memory_budget_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_second_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_second_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(second_noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_second_noop,
            "set_row_values memory-budget recovery strict-options reacquire second noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_second_noop,
            "set_row_values memory-budget recovery strict-options reacquire second noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquire second noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquire second noop save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should keep the original memory budget");
        const auto reacquire_second_noop_entries =
            fastxlsx::test::read_zip_entries(second_noop_output_after_reacquire);
        check(reacquire_second_noop_entries == reacquire_noop_entries,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should leave the source package unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should leave the first reacquire output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_row_values memory-budget recovery strict-options reacquire second noop save should leave the first no-op output stable");
        check_reopened_clean_sheet_output(second_noop_output_after_reacquire, "Data",
            "set_row_values memory-budget recovery strict-options reacquire second noop save",
            inspect_set_row_values_memory_budget_recovery_output);

        reacquired_sheet.set_row_values(1, {fastxlsx::CellValue::text("row-mb-2")});
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should keep diagnostics clear");
        check(reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should dirty the sheet");
        check(reacquired_editor.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should dirty the editor");
        check(reacquired_sheet.cell_count() == 3,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should keep sparse count stable");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should stay within the original budget");
        check(reacquired_editor.pending_materialized_cell_count() == 3,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should expose dirty sparse count");
        check(reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A1").text_value() == "row-mb-2",
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should overwrite A1");
        check(reacquired_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number &&
                reacquired_sheet.get_cell("B1").number_value() == 1.0,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should keep row tail B1");
        check(reacquired_sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should keep non-target A2");
        check_public_state_single_data_dirty_materialized_summary(
            reacquired_editor,
            reacquired_sheet,
            0,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquired post-noop edit should not queue replacement diagnostics");

        reacquired_editor.save_as(post_noop_output_after_reacquire);
        check(!reacquired_sheet.has_pending_changes(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should clean the sheet");
        check(reacquired_editor.pending_change_count() == 1,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should keep one handoff");
        check(reacquired_editor.pending_materialized_worksheet_names().empty(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
        check(reacquired_editor.pending_materialized_cell_count() == 0,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
        check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
        check(reacquired_editor.pending_worksheet_edits().empty(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should keep diagnostics clear");
        const auto post_noop_reacquire_entries =
            fastxlsx::test::read_zip_entries(post_noop_output_after_reacquire);
        const std::string post_noop_reacquire_xml =
            post_noop_reacquire_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reacquire_xml, "row-mb-2",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should persist the later overwrite");
        check_not_contains(post_noop_reacquire_xml, "row-mb-ok",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should replace the earlier A1 text");
        check_not_contains(post_noop_reacquire_xml, "set-row-values-memory-rejected",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should not leak rejected payload");
        check_contains(post_noop_reacquire_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should keep row tail B1");
        check_contains(post_noop_reacquire_xml, "placeholder-a2",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should keep non-target A2");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should leave the first no-op output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should leave the second no-op output stable");
        check(fastxlsx::test::read_zip_entries(second_noop_output_after_reacquire) == reacquire_second_noop_entries,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should leave the repeated no-op output stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_row_values memory-budget recovery strict-options reacquired post-noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(post_noop_output_after_reacquire, "Data",
            "set_row_values memory-budget recovery strict-options reacquired post-noop save",
            inspect_set_row_values_memory_budget_post_noop_output);
    }
}

void test_public_worksheet_editor_set_row_values_accepts_default_style_id_as_style_preserving_prefix()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-default-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-default-style-post-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("row-value-default-tail"),
        });
        styled_sheet.append_row({fastxlsx::CellView::text("row-value-default-a2")});
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_row_values(1, {
        fastxlsx::CellValue::number(2.5).with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {}),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Number &&
            live_a1.number_value() == 2.5 &&
            live_a1.has_style() &&
            live_a1.style_id().value() == non_default_style.value(),
        "set_row_values explicit default StyleId should preserve source style on A1");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Blank &&
            !live_b1.has_style(),
        "set_row_values explicit default StyleId should keep unstyled B1 blank unstyled");
    const fastxlsx::CellValue live_c1 = sheet.get_cell("C1");
    check(live_c1.kind() == fastxlsx::CellValueKind::Formula &&
            live_c1.text_value() == "A1+B1" &&
            !live_c1.has_style(),
        "set_row_values explicit default StyleId should insert missing C1 without a style");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Text &&
            live_a2.text_value() == "row-value-default-a2" &&
            !live_a2.has_style(),
        "set_row_values explicit default StyleId should keep untouched A2 unstyled");

    const auto check_row_value_default_a1_snapshot =
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
    const auto check_row_value_default_b1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 blank");
        };
    const auto check_row_value_default_c1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled C1 formula");
        };
    const auto check_row_value_default_a2_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "row-value-default-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 text");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("B1") &&
            sheet.contains_cell("C1") && sheet.contains_cell("A2"),
        "set_row_values explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("B2") && !sheet.contains_cell("D4"),
        "set_row_values explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one =
        sheet.row_cells(1);
    check(live_row_one.size() == 3,
        "set_row_values explicit default StyleId row_cells should expose edited row");
    if (live_row_one.size() == 3) {
        check_row_value_default_a1_snapshot(live_row_one[0],
            "set_row_values explicit default StyleId row_cells");
        check_row_value_default_b1_snapshot(live_row_one[1],
            "set_row_values explicit default StyleId row_cells");
        check_row_value_default_c1_snapshot(live_row_one[2],
            "set_row_values explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two =
        sheet.row_cells(2);
    check(live_row_two.size() == 1,
        "set_row_values explicit default StyleId row_cells should expose non-target row");
    if (live_row_two.size() == 1) {
        check_row_value_default_a2_snapshot(live_row_two[0],
            "set_row_values explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one =
        sheet.column_cells(1);
    check(live_column_one.size() == 2,
        "set_row_values explicit default StyleId column_cells should expose column one");
    if (live_column_one.size() == 2) {
        check_row_value_default_a1_snapshot(live_column_one[0],
            "set_row_values explicit default StyleId column_cells");
        check_row_value_default_a2_snapshot(live_column_one[1],
            "set_row_values explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two =
        sheet.column_cells(2);
    check(live_column_two.size() == 1,
        "set_row_values explicit default StyleId column_cells should expose B1 only");
    if (live_column_two.size() == 1) {
        check_row_value_default_b1_snapshot(live_column_two[0],
            "set_row_values explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_three =
        sheet.column_cells(3);
    check(live_column_three.size() == 1,
        "set_row_values explicit default StyleId column_cells should expose C1 only");
    if (live_column_three.size() == 1) {
        check_row_value_default_c1_snapshot(live_column_three[0],
            "set_row_values explicit default StyleId column_cells");
    }
    check(sheet.row_cells(3).empty() && sheet.column_cells(4).empty(),
        "set_row_values explicit default StyleId sparse views should keep gaps empty");
    check(sheet.cell_count() == 4,
        "set_row_values explicit default StyleId should keep represented sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "set_row_values explicit default StyleId should extend bounds to C1");
    check(sheet.has_pending_changes(),
        "set_row_values explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 4,
        "set_row_values explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row_values explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId should keep diagnostics clear");

    const auto inspect_default_style_row_values_output =
        [non_default_style, check_row_value_default_a1_snapshot,
            check_row_value_default_b1_snapshot, check_row_value_default_c1_snapshot,
            check_row_value_default_a2_snapshot](
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
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c1.text_value() == "A1+B1" &&
                    !reopened_c1.has_style(),
                prefix + " reopened output should keep inserted C1 unstyled");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "row-value-default-a2" &&
                    !reopened_a2.has_style(),
                prefix + " reopened output should keep untouched A2 unstyled");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("B1") &&
                    reopened_sheet.contains_cell("C1") &&
                    reopened_sheet.contains_cell("A2"),
                prefix + " reopened output should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("B2") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 3,
                prefix + " reopened row_cells should expose edited row");
            if (row_one.size() == 3) {
                check_row_value_default_a1_snapshot(row_one[0],
                    prefix + " reopened row_cells");
                check_row_value_default_b1_snapshot(row_one[1],
                    prefix + " reopened row_cells");
                check_row_value_default_c1_snapshot(row_one[2],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 1,
                prefix + " reopened row_cells should expose non-target row");
            if (row_two.size() == 1) {
                check_row_value_default_a2_snapshot(row_two[0],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " reopened column_cells should expose column one");
            if (column_one.size() == 2) {
                check_row_value_default_a1_snapshot(column_one[0],
                    prefix + " reopened column_cells");
                check_row_value_default_a2_snapshot(column_one[1],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " reopened column_cells should expose B1 only");
            if (column_two.size() == 1) {
                check_row_value_default_b1_snapshot(column_two[0],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                reopened_sheet.column_cells(3);
            check(column_three.size() == 1,
                prefix + " reopened column_cells should expose C1 only");
            if (column_three.size() == 1) {
                check_row_value_default_c1_snapshot(column_three[0],
                    prefix + " reopened column_cells");
            }
            check(reopened_sheet.row_cells(3).empty() &&
                    reopened_sheet.column_cells(4).empty(),
                prefix + " reopened sparse views should keep gaps empty");
        };

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "set_row_values explicit default StyleId save should clean the materialized worksheet");
    check(editor.pending_change_count() == 1,
        "set_row_values explicit default StyleId save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row_values explicit default StyleId save should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "set_row_values explicit default StyleId save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values explicit default StyleId save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId save should keep diagnostics clear");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row_values explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_a1 =
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "set_row_values explicit default StyleId should project extended bounds");
    check_contains(worksheet_xml, styled_a1,
        "set_row_values explicit default StyleId should persist source-styled A1");
    check_contains(worksheet_xml, R"(<c r="B1"/>)",
        "set_row_values explicit default StyleId should persist B1 blank without style");
    check_contains(worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
        "set_row_values explicit default StyleId should persist C1 formula without style");
    check_contains(worksheet_xml, "row-value-default-a2",
        "set_row_values explicit default StyleId should keep untouched A2 text");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "set_row_values explicit default StyleId should not write a style id on B1");
    check_not_contains(worksheet_xml, R"(<c r="C1" s=")",
        "set_row_values explicit default StyleId should not write a style id on C1");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_row_values explicit default StyleId should not write default style ids");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_row_values explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_row_values_output(
                reopened_sheet, "set_row_values explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_row_values explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row_values explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row_values explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row_values explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_row_values explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "set_row_values explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row_values explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_row_values explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_row_values_output(
                reopened_sheet, "set_row_values explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_row_values explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row_values explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row_values explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row_values explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_row_values explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_row_values explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_row_values explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_row_values explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_row_values explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_row_values explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_row_values_output(
                reopened_sheet,
                "set_row_values explicit default StyleId second no-op save");
        });

    sheet.set_row_values(1, {
        fastxlsx::CellValue::formula("A2").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::text("row-values-default-post-noop")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    const fastxlsx::CellValue post_noop_live_a1 = sheet.get_cell("A1");
    check(post_noop_live_a1.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_live_a1.text_value() == "A2" &&
            post_noop_live_a1.has_style() &&
            post_noop_live_a1.style_id().value() == non_default_style.value(),
        "set_row_values explicit default StyleId post-noop edit should keep A1 source style");
    const fastxlsx::CellValue post_noop_live_d1 = sheet.get_cell("D1");
    check(post_noop_live_d1.kind() == fastxlsx::CellValueKind::Boolean &&
            post_noop_live_d1.boolean_value() &&
            !post_noop_live_d1.has_style(),
        "set_row_values explicit default StyleId post-noop edit should insert D1 without style");
    const fastxlsx::CellValue post_noop_live_a2 = sheet.get_cell("A2");
    check(post_noop_live_a2.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_live_a2.text_value() == "row-value-default-a2" &&
            !post_noop_live_a2.has_style(),
        "set_row_values explicit default StyleId post-noop edit should preserve untouched A2");
    check(sheet.cell_count() == 5,
        "set_row_values explicit default StyleId post-noop edit should expand sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "set_row_values explicit default StyleId post-noop edit should expand row bounds");
    check(sheet.has_pending_changes(),
        "set_row_values explicit default StyleId post-noop edit should dirty the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row_values explicit default StyleId post-noop edit should not record a handoff before save");
    check(editor.pending_materialized_cell_count() == 5,
        "set_row_values explicit default StyleId post-noop edit should expose dirty sparse count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_row_values explicit default StyleId post-noop edit dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId post-noop edit should keep diagnostics clear");

    const auto check_post_noop_row_values_a1 =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A2" &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should expose source-styled A1 formula");
        };
    const auto check_post_noop_row_values_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "row-values-default-post-noop" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 text");
        };
    const auto check_post_noop_row_values_c1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled C1 blank");
        };
    const auto check_post_noop_row_values_d1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 4 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled D1 boolean");
        };

    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_one =
        sheet.row_cells(1);
    check(post_noop_row_one.size() == 4,
        "set_row_values explicit default StyleId post-noop edit row_cells should expose edited row");
    if (post_noop_row_one.size() == 4) {
        check_post_noop_row_values_a1(post_noop_row_one[0],
            "set_row_values explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_values_b1(post_noop_row_one[1],
            "set_row_values explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_values_c1(post_noop_row_one[2],
            "set_row_values explicit default StyleId post-noop edit row_cells");
        check_post_noop_row_values_d1(post_noop_row_one[3],
            "set_row_values explicit default StyleId post-noop edit row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_two =
        sheet.row_cells(2);
    check(post_noop_row_two.size() == 1,
        "set_row_values explicit default StyleId post-noop edit row_cells should keep non-target row");
    if (post_noop_row_two.size() == 1) {
        check_row_value_default_a2_snapshot(post_noop_row_two[0],
            "set_row_values explicit default StyleId post-noop edit row_cells");
    }

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "set_row_values explicit default StyleId post-noop save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_row_values explicit default StyleId post-noop save should record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_row_values explicit default StyleId post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_row_values explicit default StyleId post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values explicit default StyleId post-noop save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row_values explicit default StyleId post-noop save should keep diagnostics clear");

    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values explicit default StyleId post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_row_values explicit default StyleId post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_row_values explicit default StyleId post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "set_row_values explicit default StyleId post-noop save should leave the second no-op output unchanged");
    check(post_noop_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row_values explicit default StyleId post-noop save should preserve source styles.xml bytes");

    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "set_row_values explicit default StyleId post-noop save should expand row bounds");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) +
            R"("><f>A2</f></c>)",
        "set_row_values explicit default StyleId post-noop save should persist source-styled A1 formula");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>row-values-default-post-noop</t></is></c>)",
        "set_row_values explicit default StyleId post-noop save should persist B1 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="C1"/>)",
        "set_row_values explicit default StyleId post-noop save should persist C1 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="D1" t="b"><v>1</v></c>)",
        "set_row_values explicit default StyleId post-noop save should persist D1 without a style id");
    check_contains(post_noop_worksheet_xml, "row-value-default-a2",
        "set_row_values explicit default StyleId post-noop save should preserve untouched A2");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="B1" s=")",
        "set_row_values explicit default StyleId post-noop save should not write a default style on B1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="C1" s=")",
        "set_row_values explicit default StyleId post-noop save should not write a default style on C1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="D1" s=")",
        "set_row_values explicit default StyleId post-noop save should not write a default style on D1");
    check_not_contains(post_noop_worksheet_xml, R"(s="0")",
        "set_row_values explicit default StyleId post-noop save should not write default style ids");
    check_not_contains(post_noop_worksheet_xml, "A1+B1",
        "set_row_values explicit default StyleId post-noop save should replace the earlier C1 formula");

    check_reopened_clean_sheet_output(
        post_noop_output, "Styled",
        "set_row_values explicit default StyleId post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "set_row_values explicit default StyleId post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "set_row_values explicit default StyleId post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 4,
                "set_row_values explicit default StyleId post-noop reopened row_cells should expose row one");
            if (row_one.size() == 4) {
                check_post_noop_row_values_a1(row_one[0],
                    "set_row_values explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_values_b1(row_one[1],
                    "set_row_values explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_values_c1(row_one[2],
                    "set_row_values explicit default StyleId post-noop reopened row_cells");
                check_post_noop_row_values_d1(row_one[3],
                    "set_row_values explicit default StyleId post-noop reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 1,
                "set_row_values explicit default StyleId post-noop reopened row_cells should expose row two");
            if (row_two.size() == 1) {
                check_row_value_default_a2_snapshot(row_two[0],
                    "set_row_values explicit default StyleId post-noop reopened row_cells");
            }
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a1.text_value() == "A2" &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                "set_row_values explicit default StyleId post-noop reopened output should read A1 formula with source style");
            const fastxlsx::CellValue reopened_d1 = reopened_sheet.get_cell("D1");
            check(reopened_d1.kind() == fastxlsx::CellValueKind::Boolean &&
                    reopened_d1.boolean_value() &&
                    !reopened_d1.has_style(),
                "set_row_values explicit default StyleId post-noop reopened output should read D1 without a style handle");
        });
}

void test_public_worksheet_editor_set_row_values_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-row-values-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("set-row-values-dirty-source-b1"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-row-values-dirty-source-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row_values dirty style rejection source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    const auto check_styled_a1 =
        [source_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 1.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == source_style.value(),
                prefix + " should expose source-styled A1");
        };
    const auto check_source_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-values-dirty-source-b1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled source B1");
        };
    const auto check_dirty_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-values-dirty-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty A2");
        };
    const auto check_dirty_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B2 formula");
        };
    const auto check_recovered_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-row-values-dirty-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered A2");
        };
    const auto check_recovered_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered B2 blank");
        };
    const auto check_recovered_c2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 3 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered C2 boolean");
        };
    const auto check_dirty_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 4,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C2") &&
                    !current_sheet.contains_cell("A3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " sparse_cells should expose four represented records");
            if (cells.size() == 4) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_source_b1(cells[1], prefix + " sparse_cells");
                check_dirty_a2(cells[2], prefix + " sparse_cells");
                check_dirty_b2(cells[3], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose source row one");
            if (row_one.size() == 2) {
                check_styled_a1(row_one[0], prefix + " row_cells");
                check_source_b1(row_one[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " row_cells should expose dirty row two");
            if (row_two.size() == 2) {
                check_dirty_a2(row_two[0], prefix + " row_cells");
                check_dirty_b2(row_two[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_dirty_a2(column_one[1], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " column_cells should expose column two");
            if (column_two.size() == 2) {
                check_source_b1(column_two[0], prefix + " column_cells");
                check_dirty_b2(column_two[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "set-row-values-dirty-kept" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve dirty A2 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Formula &&
                    b2.text_value() == "A1" &&
                    !b2.has_style(),
                prefix + " get_cell should preserve dirty B2 without a style");
        };
    const auto check_recovery_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 5,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 3,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2") &&
                    current_sheet.contains_cell("C2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("A3") &&
                    !current_sheet.contains_cell("D2"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " sparse_cells should expose five represented records");
            if (cells.size() == 5) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_source_b1(cells[1], prefix + " sparse_cells");
                check_recovered_a2(cells[2], prefix + " sparse_cells");
                check_recovered_b2(cells[3], prefix + " sparse_cells");
                check_recovered_c2(cells[4], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 3,
                prefix + " row_cells should expose recovered row two");
            if (row_two.size() == 3) {
                check_recovered_a2(row_two[0], prefix + " row_cells");
                check_recovered_b2(row_two[1], prefix + " row_cells");
                check_recovered_c2(row_two[2], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_recovered_a2(column_one[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue a2 = current_sheet.get_cell("A2");
            check(a2.kind() == fastxlsx::CellValueKind::Text &&
                    a2.text_value() == "set-row-values-dirty-recovered" &&
                    !a2.has_style(),
                prefix + " get_cell should preserve recovered A2 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Blank &&
                    !b2.has_style(),
                prefix + " get_cell should preserve recovered B2 without a style");
            const fastxlsx::CellValue c2 = current_sheet.get_cell("C2");
            check(c2.kind() == fastxlsx::CellValueKind::Boolean &&
                    c2.boolean_value() &&
                    !c2.has_style(),
                prefix + " get_cell should preserve recovered C2 without a style");
        };

    sheet.set_row_values(2, {
        fastxlsx::CellValue::text("set-row-values-dirty-kept")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1").with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_row_values dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "set_row_values dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row_values dirty style rejection setup");
    check_dirty_views(sheet, "set_row_values dirty style rejection setup");

    bool failed = false;
    try {
        sheet.set_row_values(3, {
            fastxlsx::CellValue::text("set-row-values-dirty-rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "set_row_values dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "set_row_values dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row_values dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_row_values dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_row_values dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row_values dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "set_row_values dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "set_row_values dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "set_row_values dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row_values dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row_values dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row_values dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row_values dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "set_row_values dirty style rejection save should keep dirty bounds");
    check_contains(worksheet_xml, "set-row-values-dirty-kept",
        "set_row_values dirty style rejection save should persist prior dirty A2");
    check_contains(worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_row_values dirty style rejection save should persist prior dirty B2 formula");
    check_not_contains(worksheet_xml, "set-row-values-dirty-rejected",
        "set_row_values dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row_values dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "set_row_values dirty style rejection save should keep dirty A2 unstyled");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "set_row_values dirty style rejection save should keep dirty B2 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_row_values dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_row_values dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_row_values dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_row_values dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "set_row_values dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_row_values dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_row_values dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row_values dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_row_values dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_row_values dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_row_values dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_row_values dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_row_values dirty style rejection noop save");
        });

    sheet.set_row_values(2, {
        fastxlsx::CellValue::text("set-row-values-dirty-recovered")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_row_values dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_row_values dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_row_values dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_row_values dirty style rejection recovery should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_row_values dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save,
        "set_row_values dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "set_row_values dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_row_values dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row_values dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_row_values dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_row_values dirty style rejection recovery save should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_row_values dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_row_values dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "set_row_values dirty style rejection recovery save should extend bounds to C2");
    check_contains(recovery_worksheet_xml, "set-row-values-dirty-source-b1",
        "set_row_values dirty style rejection recovery save should preserve source B1");
    check_contains(recovery_worksheet_xml, "set-row-values-dirty-recovered",
        "set_row_values dirty style rejection recovery save should persist recovered A2");
    check_contains(recovery_worksheet_xml, R"(<c r="B2"/>)",
        "set_row_values dirty style rejection recovery save should persist recovered B2 blank");
    check_contains(recovery_worksheet_xml, R"(<c r="C2" t="b"><v>1</v></c>)",
        "set_row_values dirty style rejection recovery save should persist recovered C2 boolean");
    check_not_contains(recovery_worksheet_xml, "set-row-values-dirty-kept",
        "set_row_values dirty style rejection recovery save should replace prior dirty A2");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_row_values dirty style rejection recovery save should replace prior dirty B2 formula");
    check_not_contains(recovery_worksheet_xml, "set-row-values-dirty-source-a2",
        "set_row_values dirty style rejection recovery save should not revive source A2");
    check_not_contains(recovery_worksheet_xml, "set-row-values-dirty-rejected",
        "set_row_values dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_row_values dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="A2" s=")",
        "set_row_values dirty style rejection recovery save should keep recovered A2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2" s=")",
        "set_row_values dirty style rejection recovery save should keep recovered B2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="C2" s=")",
        "set_row_values dirty style rejection recovery save should keep recovered C2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "set_row_values dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Styled",
        "set_row_values dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_row_values dirty style rejection recovery save");
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
        "set_row_values dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "set_row_values dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "set_row_values dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_row_values dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_row_values dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "set_row_values dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_row_values dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_recovery_views(
        sheet, "set_row_values dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "set_row_values dirty style rejection recovery noop output should match the recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_row_values dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Styled",
        "set_row_values dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_row_values dirty style rejection recovery noop save");
        });
}

void test_public_worksheet_editor_set_column_values_accepts_default_style_id_as_style_preserving_prefix()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-default-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-default-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-default-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-default-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-default-style-post-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("column-value-default-tail"),
        });
        styled_sheet.append_row({fastxlsx::CellView::text("column-value-default-a2")});
        writer.close();
    }
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_column_values(1, {
        fastxlsx::CellValue::number(2.5).with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1+B1").with_style(fastxlsx::StyleId {}),
    });

    const fastxlsx::CellValue live_a1 = sheet.get_cell("A1");
    check(live_a1.kind() == fastxlsx::CellValueKind::Number &&
            live_a1.number_value() == 2.5 &&
            live_a1.has_style() &&
            live_a1.style_id().value() == non_default_style.value(),
        "set_column_values explicit default StyleId should preserve source style on A1");
    const fastxlsx::CellValue live_a2 = sheet.get_cell("A2");
    check(live_a2.kind() == fastxlsx::CellValueKind::Blank &&
            !live_a2.has_style(),
        "set_column_values explicit default StyleId should keep unstyled A2 blank unstyled");
    const fastxlsx::CellValue live_a3 = sheet.get_cell("A3");
    check(live_a3.kind() == fastxlsx::CellValueKind::Formula &&
            live_a3.text_value() == "A1+B1" &&
            !live_a3.has_style(),
        "set_column_values explicit default StyleId should insert missing A3 without a style");
    const fastxlsx::CellValue live_b1 = sheet.get_cell("B1");
    check(live_b1.kind() == fastxlsx::CellValueKind::Text &&
            live_b1.text_value() == "column-value-default-tail" &&
            !live_b1.has_style(),
        "set_column_values explicit default StyleId should keep untouched B1 unstyled");

    const auto check_column_value_default_a1_snapshot =
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
    const auto check_column_value_default_a2_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 blank");
        };
    const auto check_column_value_default_a3_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1+B1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A3 formula");
        };
    const auto check_column_value_default_b1_snapshot =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "column-value-default-tail" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled B1 text");
        };

    check(sheet.contains_cell("A1") && sheet.contains_cell("A2") &&
            sheet.contains_cell("A3") && sheet.contains_cell("B1"),
        "set_column_values explicit default StyleId should keep represented cells queryable");
    check(!sheet.contains_cell("B2") && !sheet.contains_cell("D4"),
        "set_column_values explicit default StyleId should keep unrelated missing cells absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_one =
        sheet.column_cells(1);
    check(live_column_one.size() == 3,
        "set_column_values explicit default StyleId column_cells should expose edited column");
    if (live_column_one.size() == 3) {
        check_column_value_default_a1_snapshot(live_column_one[0],
            "set_column_values explicit default StyleId column_cells");
        check_column_value_default_a2_snapshot(live_column_one[1],
            "set_column_values explicit default StyleId column_cells");
        check_column_value_default_a3_snapshot(live_column_one[2],
            "set_column_values explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_column_two =
        sheet.column_cells(2);
    check(live_column_two.size() == 1,
        "set_column_values explicit default StyleId column_cells should expose non-target column");
    if (live_column_two.size() == 1) {
        check_column_value_default_b1_snapshot(live_column_two[0],
            "set_column_values explicit default StyleId column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_one =
        sheet.row_cells(1);
    check(live_row_one.size() == 2,
        "set_column_values explicit default StyleId row_cells should expose row one");
    if (live_row_one.size() == 2) {
        check_column_value_default_a1_snapshot(live_row_one[0],
            "set_column_values explicit default StyleId row_cells");
        check_column_value_default_b1_snapshot(live_row_one[1],
            "set_column_values explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_two =
        sheet.row_cells(2);
    check(live_row_two.size() == 1,
        "set_column_values explicit default StyleId row_cells should expose A2 only");
    if (live_row_two.size() == 1) {
        check_column_value_default_a2_snapshot(live_row_two[0],
            "set_column_values explicit default StyleId row_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> live_row_three =
        sheet.row_cells(3);
    check(live_row_three.size() == 1,
        "set_column_values explicit default StyleId row_cells should expose A3 only");
    if (live_row_three.size() == 1) {
        check_column_value_default_a3_snapshot(live_row_three[0],
            "set_column_values explicit default StyleId row_cells");
    }
    check(sheet.row_cells(4).empty() && sheet.column_cells(3).empty(),
        "set_column_values explicit default StyleId sparse views should keep gaps empty");
    check(sheet.cell_count() == 4,
        "set_column_values explicit default StyleId should keep represented sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 2,
        "set_column_values explicit default StyleId should extend bounds to A3");
    check(sheet.has_pending_changes(),
        "set_column_values explicit default StyleId should dirty the materialized worksheet");
    check(editor.pending_materialized_cell_count() == 4,
        "set_column_values explicit default StyleId should expose aggregate materialized count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column_values explicit default StyleId dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId should keep diagnostics clear");

    const auto inspect_default_style_column_values_output =
        [non_default_style, check_column_value_default_a1_snapshot,
            check_column_value_default_a2_snapshot,
            check_column_value_default_a3_snapshot,
            check_column_value_default_b1_snapshot](
            fastxlsx::WorksheetEditor& reopened_sheet,
            std::string_view scenario) {
            const std::string prefix(scenario);

            check(reopened_sheet.cell_count() == 4,
                prefix + " reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                prefix + " reopened output should keep bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 2.5 &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                prefix + " reopened output should preserve source style on A1");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Blank &&
                    !reopened_a2.has_style(),
                prefix + " reopened output should keep A2 blank unstyled");
            const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
            check(reopened_a3.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a3.text_value() == "A1+B1" &&
                    !reopened_a3.has_style(),
                prefix + " reopened output should keep inserted A3 unstyled");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "column-value-default-tail" &&
                    !reopened_b1.has_style(),
                prefix + " reopened output should keep untouched B1 unstyled");
            check(reopened_sheet.contains_cell("A1") &&
                    reopened_sheet.contains_cell("A2") &&
                    reopened_sheet.contains_cell("A3") &&
                    reopened_sheet.contains_cell("B1"),
                prefix + " reopened output should keep represented cells queryable");
            check(!reopened_sheet.contains_cell("B2") &&
                    !reopened_sheet.contains_cell("D4"),
                prefix + " reopened output should keep unrelated missing cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 3,
                prefix + " reopened column_cells should expose edited column");
            if (column_one.size() == 3) {
                check_column_value_default_a1_snapshot(column_one[0],
                    prefix + " reopened column_cells");
                check_column_value_default_a2_snapshot(column_one[1],
                    prefix + " reopened column_cells");
                check_column_value_default_a3_snapshot(column_one[2],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                reopened_sheet.column_cells(2);
            check(column_two.size() == 1,
                prefix + " reopened column_cells should expose non-target column");
            if (column_two.size() == 1) {
                check_column_value_default_b1_snapshot(column_two[0],
                    prefix + " reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " reopened row_cells should expose row one");
            if (row_one.size() == 2) {
                check_column_value_default_a1_snapshot(row_one[0],
                    prefix + " reopened row_cells");
                check_column_value_default_b1_snapshot(row_one[1],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                reopened_sheet.row_cells(2);
            check(row_two.size() == 1,
                prefix + " reopened row_cells should expose A2 only");
            if (row_two.size() == 1) {
                check_column_value_default_a2_snapshot(row_two[0],
                    prefix + " reopened row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                reopened_sheet.row_cells(3);
            check(row_three.size() == 1,
                prefix + " reopened row_cells should expose A3 only");
            if (row_three.size() == 1) {
                check_column_value_default_a3_snapshot(row_three[0],
                    prefix + " reopened row_cells");
            }
            check(reopened_sheet.row_cells(4).empty() &&
                    reopened_sheet.column_cells(3).empty(),
                prefix + " reopened sparse views should keep gaps empty");
        };

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "set_column_values explicit default StyleId save should clean the materialized worksheet");
    check(editor.pending_change_count() == 1,
        "set_column_values explicit default StyleId save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column_values explicit default StyleId save should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "set_column_values explicit default StyleId save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values explicit default StyleId save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId save should keep diagnostics clear");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values explicit default StyleId save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column_values explicit default StyleId save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_a1 =
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) + R"("><v>2.5</v></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "set_column_values explicit default StyleId should project extended bounds");
    check_contains(worksheet_xml, styled_a1,
        "set_column_values explicit default StyleId should persist source-styled A1");
    check_contains(worksheet_xml, "column-value-default-tail",
        "set_column_values explicit default StyleId should keep untouched B1 text");
    check_contains(worksheet_xml, R"(<c r="A2"/>)",
        "set_column_values explicit default StyleId should persist A2 blank without style");
    check_contains(worksheet_xml, R"(<c r="A3"><f>A1+B1</f></c>)",
        "set_column_values explicit default StyleId should persist A3 formula without style");
    check_not_contains(worksheet_xml, R"(<c r="A2" s=")",
        "set_column_values explicit default StyleId should not write a style id on A2");
    check_not_contains(worksheet_xml, R"(<c r="A3" s=")",
        "set_column_values explicit default StyleId should not write a style id on A3");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_column_values explicit default StyleId should not write default style ids");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_column_values explicit default StyleId",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_column_values_output(
                reopened_sheet, "set_column_values explicit default StyleId");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "set_column_values explicit default StyleId no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column_values explicit default StyleId no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column_values explicit default StyleId no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column_values explicit default StyleId no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values explicit default StyleId no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_column_values explicit default StyleId no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "set_column_values explicit default StyleId no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column_values explicit default StyleId no-op output should match materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values explicit default StyleId no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_column_values explicit default StyleId no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_column_values_output(
                reopened_sheet, "set_column_values explicit default StyleId no-op save");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "set_column_values explicit default StyleId second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column_values explicit default StyleId second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column_values explicit default StyleId second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column_values explicit default StyleId second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values explicit default StyleId second no-op save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "set_column_values explicit default StyleId second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "set_column_values explicit default StyleId second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "set_column_values explicit default StyleId second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_column_values explicit default StyleId second no-op save should leave the materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_column_values explicit default StyleId second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values explicit default StyleId second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled",
        "set_column_values explicit default StyleId second no-op save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            inspect_default_style_column_values_output(
                reopened_sheet,
                "set_column_values explicit default StyleId second no-op save");
        });

    sheet.set_column_values(1, {
        fastxlsx::CellValue::formula("B1").with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::text("column-values-default-post-noop")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    const fastxlsx::CellValue post_noop_live_a1 = sheet.get_cell("A1");
    check(post_noop_live_a1.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_live_a1.text_value() == "B1" &&
            post_noop_live_a1.has_style() &&
            post_noop_live_a1.style_id().value() == non_default_style.value(),
        "set_column_values explicit default StyleId post-noop edit should keep A1 source style");
    const fastxlsx::CellValue post_noop_live_a4 = sheet.get_cell("A4");
    check(post_noop_live_a4.kind() == fastxlsx::CellValueKind::Boolean &&
            post_noop_live_a4.boolean_value() &&
            !post_noop_live_a4.has_style(),
        "set_column_values explicit default StyleId post-noop edit should insert A4 without style");
    const fastxlsx::CellValue post_noop_live_b1 = sheet.get_cell("B1");
    check(post_noop_live_b1.kind() == fastxlsx::CellValueKind::Text &&
            post_noop_live_b1.text_value() == "column-value-default-tail" &&
            !post_noop_live_b1.has_style(),
        "set_column_values explicit default StyleId post-noop edit should preserve untouched B1");
    check(sheet.cell_count() == 5,
        "set_column_values explicit default StyleId post-noop edit should expand sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 4, 2,
        "set_column_values explicit default StyleId post-noop edit should expand column bounds");
    check(sheet.has_pending_changes(),
        "set_column_values explicit default StyleId post-noop edit should dirty the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column_values explicit default StyleId post-noop edit should not record a handoff before save");
    check(editor.pending_materialized_cell_count() == 5,
        "set_column_values explicit default StyleId post-noop edit should expose dirty sparse count");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_column_values explicit default StyleId post-noop edit dirty summary");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId post-noop edit should keep diagnostics clear");

    const auto check_post_noop_column_values_a1 =
        [non_default_style](
            const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "B1" &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == non_default_style.value(),
                prefix + " should expose source-styled A1 formula");
        };
    const auto check_post_noop_column_values_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "column-values-default-post-noop" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A2 text");
        };
    const auto check_post_noop_column_values_a3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A3 blank");
        };
    const auto check_post_noop_column_values_a4 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 4 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled A4 boolean");
        };

    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_column_one =
        sheet.column_cells(1);
    check(post_noop_column_one.size() == 4,
        "set_column_values explicit default StyleId post-noop edit column_cells should expose edited column");
    if (post_noop_column_one.size() == 4) {
        check_post_noop_column_values_a1(post_noop_column_one[0],
            "set_column_values explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_values_a2(post_noop_column_one[1],
            "set_column_values explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_values_a3(post_noop_column_one[2],
            "set_column_values explicit default StyleId post-noop edit column_cells");
        check_post_noop_column_values_a4(post_noop_column_one[3],
            "set_column_values explicit default StyleId post-noop edit column_cells");
    }
    const std::vector<fastxlsx::WorksheetCellSnapshot> post_noop_row_one =
        sheet.row_cells(1);
    check(post_noop_row_one.size() == 2,
        "set_column_values explicit default StyleId post-noop edit row_cells should keep row one");
    if (post_noop_row_one.size() == 2) {
        check_post_noop_column_values_a1(post_noop_row_one[0],
            "set_column_values explicit default StyleId post-noop edit row_cells");
        check_column_value_default_b1_snapshot(post_noop_row_one[1],
            "set_column_values explicit default StyleId post-noop edit row_cells");
    }

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "set_column_values explicit default StyleId post-noop save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_column_values explicit default StyleId post-noop save should record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "set_column_values explicit default StyleId post-noop save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "set_column_values explicit default StyleId post-noop save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values explicit default StyleId post-noop save should not queue diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column_values explicit default StyleId post-noop save should keep diagnostics clear");

    const auto post_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values explicit default StyleId post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "set_column_values explicit default StyleId post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "set_column_values explicit default StyleId post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "set_column_values explicit default StyleId post-noop save should leave the second no-op output unchanged");
    check(post_noop_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column_values explicit default StyleId post-noop save should preserve source styles.xml bytes");

    const std::string post_noop_worksheet_xml =
        post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_worksheet_xml, R"(<dimension ref="A1:B4"/>)",
        "set_column_values explicit default StyleId post-noop save should expand column bounds");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value()) +
            R"("><f>B1</f></c>)",
        "set_column_values explicit default StyleId post-noop save should persist source-styled A1 formula");
    check_contains(post_noop_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>column-values-default-post-noop</t></is></c>)",
        "set_column_values explicit default StyleId post-noop save should persist A2 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="A3"/>)",
        "set_column_values explicit default StyleId post-noop save should persist A3 without a style id");
    check_contains(post_noop_worksheet_xml, R"(<c r="A4" t="b"><v>1</v></c>)",
        "set_column_values explicit default StyleId post-noop save should persist A4 without a style id");
    check_contains(post_noop_worksheet_xml, "column-value-default-tail",
        "set_column_values explicit default StyleId post-noop save should preserve untouched B1");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A2" s=")",
        "set_column_values explicit default StyleId post-noop save should not write a default style on A2");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A3" s=")",
        "set_column_values explicit default StyleId post-noop save should not write a default style on A3");
    check_not_contains(post_noop_worksheet_xml, R"(<c r="A4" s=")",
        "set_column_values explicit default StyleId post-noop save should not write a default style on A4");
    check_not_contains(post_noop_worksheet_xml, R"(s="0")",
        "set_column_values explicit default StyleId post-noop save should not write default style ids");
    check_not_contains(post_noop_worksheet_xml, "A1+B1",
        "set_column_values explicit default StyleId post-noop save should replace the earlier A3 formula");

    check_reopened_clean_sheet_output(
        post_noop_output, "Styled",
        "set_column_values explicit default StyleId post-noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "set_column_values explicit default StyleId post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 2,
                "set_column_values explicit default StyleId post-noop reopened output should keep bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                reopened_sheet.column_cells(1);
            check(column_one.size() == 4,
                "set_column_values explicit default StyleId post-noop reopened column_cells should expose column one");
            if (column_one.size() == 4) {
                check_post_noop_column_values_a1(column_one[0],
                    "set_column_values explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_values_a2(column_one[1],
                    "set_column_values explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_values_a3(column_one[2],
                    "set_column_values explicit default StyleId post-noop reopened column_cells");
                check_post_noop_column_values_a4(column_one[3],
                    "set_column_values explicit default StyleId post-noop reopened column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                reopened_sheet.row_cells(1);
            check(row_one.size() == 2,
                "set_column_values explicit default StyleId post-noop reopened row_cells should expose row one");
            if (row_one.size() == 2) {
                check_post_noop_column_values_a1(row_one[0],
                    "set_column_values explicit default StyleId post-noop reopened row_cells");
                check_column_value_default_b1_snapshot(row_one[1],
                    "set_column_values explicit default StyleId post-noop reopened row_cells");
            }
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_a1.text_value() == "B1" &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                "set_column_values explicit default StyleId post-noop reopened output should read A1 formula with source style");
            const fastxlsx::CellValue reopened_a4 = reopened_sheet.get_cell("A4");
            check(reopened_a4.kind() == fastxlsx::CellValueKind::Boolean &&
                    reopened_a4.boolean_value() &&
                    !reopened_a4.has_style(),
                "set_column_values explicit default StyleId post-noop reopened output should read A4 without a style handle");
        });
}

void test_public_worksheet_editor_set_column_values_style_rejection_preserves_dirty_session()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-rejection-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-rejection-dirty-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-rejection-dirty-noop-output.xlsx");
    const std::filesystem::path recovery_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-rejection-dirty-recovery-output.xlsx");
    const std::filesystem::path recovery_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-rejection-dirty-recovery-noop-output.xlsx");

    fastxlsx::StyleId source_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        source_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(source_style),
            fastxlsx::CellView::text("set-column-values-dirty-source-b1"),
        });
        styled_sheet.append_row({
            fastxlsx::CellView::text("set-column-values-dirty-source-a2"),
        });
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column_values dirty style rejection source fixture should start with styled A1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    const auto check_styled_a1 =
        [source_style](const fastxlsx::WorksheetCellSnapshot& snapshot,
            std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Number &&
                    snapshot.value.number_value() == 1.0 &&
                    snapshot.value.has_style() &&
                    snapshot.value.style_id().value() == source_style.value(),
                prefix + " should expose source-styled A1");
        };
    const auto check_source_a2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 1 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-values-dirty-source-a2" &&
                    !snapshot.value.has_style(),
                prefix + " should expose unstyled source A2");
        };
    const auto check_dirty_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-values-dirty-kept" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B1");
        };
    const auto check_dirty_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Formula &&
                    snapshot.value.text_value() == "A1" &&
                    !snapshot.value.has_style(),
                prefix + " should expose preserved dirty B2 formula");
        };
    const auto check_recovered_b1 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 1 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Text &&
                    snapshot.value.text_value() == "set-column-values-dirty-recovered" &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered B1");
        };
    const auto check_recovered_b2 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 2 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Blank &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered B2 blank");
        };
    const auto check_recovered_b3 =
        [](const fastxlsx::WorksheetCellSnapshot& snapshot, std::string_view scenario) {
            const std::string prefix(scenario);
            check(snapshot.reference.row == 3 &&
                    snapshot.reference.column == 2 &&
                    snapshot.value.kind() == fastxlsx::CellValueKind::Boolean &&
                    snapshot.value.boolean_value() &&
                    !snapshot.value.has_style(),
                prefix + " should expose recovered B3 boolean");
        };
    const auto check_dirty_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 4,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 2, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("B3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " sparse_cells should expose four represented records");
            if (cells.size() == 4) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_dirty_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
                check_dirty_b2(cells[3], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                current_sheet.row_cells(1);
            check(row_one.size() == 2,
                prefix + " row_cells should expose row one");
            if (row_one.size() == 2) {
                check_styled_a1(row_one[0], prefix + " row_cells");
                check_dirty_b1(row_one[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                current_sheet.row_cells(2);
            check(row_two.size() == 2,
                prefix + " row_cells should expose row two");
            if (row_two.size() == 2) {
                check_source_a2(row_two[0], prefix + " row_cells");
                check_dirty_b2(row_two[1], prefix + " row_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                current_sheet.column_cells(1);
            check(column_one.size() == 2,
                prefix + " column_cells should expose source column one");
            if (column_one.size() == 2) {
                check_styled_a1(column_one[0], prefix + " column_cells");
                check_source_a2(column_one[1], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 2,
                prefix + " column_cells should expose dirty column two");
            if (column_two.size() == 2) {
                check_dirty_b1(column_two[0], prefix + " column_cells");
                check_dirty_b2(column_two[1], prefix + " column_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "set-column-values-dirty-kept" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve dirty B1 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Formula &&
                    b2.text_value() == "A1" &&
                    !b2.has_style(),
                prefix + " get_cell should preserve dirty B2 without a style");
        };
    const auto check_recovery_views =
        [&](fastxlsx::WorksheetEditor& current_sheet, std::string_view scenario) {
            const std::string prefix(scenario);
            check(current_sheet.cell_count() == 5,
                prefix + " should keep the represented sparse count");
            check_cell_range_equals(current_sheet.used_range(), 1, 1, 3, 2,
                prefix + " should keep the represented bounds");
            check(current_sheet.contains_cell("A1") &&
                    current_sheet.contains_cell("B1") &&
                    current_sheet.contains_cell("A2") &&
                    current_sheet.contains_cell("B2") &&
                    current_sheet.contains_cell("B3"),
                prefix + " contains_cell should keep represented cells visible");
            check(!current_sheet.contains_cell("C1") &&
                    !current_sheet.contains_cell("A3"),
                prefix + " contains_cell should keep rejected cells absent");

            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                current_sheet.sparse_cells();
            check(cells.size() == 5,
                prefix + " sparse_cells should expose five represented records");
            if (cells.size() == 5) {
                check_styled_a1(cells[0], prefix + " sparse_cells");
                check_recovered_b1(cells[1], prefix + " sparse_cells");
                check_source_a2(cells[2], prefix + " sparse_cells");
                check_recovered_b2(cells[3], prefix + " sparse_cells");
                check_recovered_b3(cells[4], prefix + " sparse_cells");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                current_sheet.column_cells(2);
            check(column_two.size() == 3,
                prefix + " column_cells should expose recovered column two");
            if (column_two.size() == 3) {
                check_recovered_b1(column_two[0], prefix + " column_cells");
                check_recovered_b2(column_two[1], prefix + " column_cells");
                check_recovered_b3(column_two[2], prefix + " column_cells");
            }
            const std::vector<fastxlsx::WorksheetCellSnapshot> row_three =
                current_sheet.row_cells(3);
            check(row_three.size() == 1,
                prefix + " row_cells should expose recovered row three");
            if (row_three.size() == 1) {
                check_recovered_b3(row_three[0], prefix + " row_cells");
            }

            const fastxlsx::CellValue a1 = current_sheet.get_cell("A1");
            check(a1.kind() == fastxlsx::CellValueKind::Number &&
                    a1.number_value() == 1.0 &&
                    a1.has_style() &&
                    a1.style_id().value() == source_style.value(),
                prefix + " get_cell should preserve source-styled A1");
            const fastxlsx::CellValue b1 = current_sheet.get_cell("B1");
            check(b1.kind() == fastxlsx::CellValueKind::Text &&
                    b1.text_value() == "set-column-values-dirty-recovered" &&
                    !b1.has_style(),
                prefix + " get_cell should preserve recovered B1 without a style");
            const fastxlsx::CellValue b2 = current_sheet.get_cell("B2");
            check(b2.kind() == fastxlsx::CellValueKind::Blank &&
                    !b2.has_style(),
                prefix + " get_cell should preserve recovered B2 without a style");
            const fastxlsx::CellValue b3 = current_sheet.get_cell("B3");
            check(b3.kind() == fastxlsx::CellValueKind::Boolean &&
                    b3.boolean_value() &&
                    !b3.has_style(),
                prefix + " get_cell should preserve recovered B3 without a style");
        };

    sheet.set_column_values(2, {
        fastxlsx::CellValue::text("set-column-values-dirty-kept")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::formula("A1").with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_column_values dirty style rejection setup should start diagnostic-clean");
    check(sheet.has_pending_changes(),
        "set_column_values dirty style rejection setup should dirty the materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column_values dirty style rejection setup");
    check_dirty_views(sheet, "set_column_values dirty style rejection setup");

    bool failed = false;
    try {
        sheet.set_column_values(3, {
            fastxlsx::CellValue::text("set-column-values-dirty-rejected")
                .with_style(source_style),
        });
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "StyleId",
            "set_column_values dirty style rejection should expose the unsupported StyleId boundary");
    }
    check(failed,
        "set_column_values dirty style rejection should reject caller-supplied non-default StyleId values");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column_values dirty style rejection should retain the public StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_column_values dirty style rejection should keep the prior dirty materialized sheet");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_column_values dirty style rejection",
        editor.last_edit_error());
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values dirty style rejection should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column_values dirty style rejection live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_save, "set_column_values dirty style rejection save");
    check(!sheet.has_pending_changes(),
        "set_column_values dirty style rejection save should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "set_column_values dirty style rejection save should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column_values dirty style rejection save should clear dirty materialized diagnostics");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column_values dirty style rejection save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "set_column_values dirty style rejection save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column_values dirty style rejection saved handle");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column_values dirty style rejection save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "set_column_values dirty style rejection save should keep dirty bounds");
    check_contains(worksheet_xml, "set-column-values-dirty-kept",
        "set_column_values dirty style rejection save should persist prior dirty B1");
    check_contains(worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_column_values dirty style rejection save should persist prior dirty B2 formula");
    check_not_contains(worksheet_xml, "set-column-values-dirty-rejected",
        "set_column_values dirty style rejection save should not leak rejected payloads");
    check_contains(worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column_values dirty style rejection save should keep source A1 styled");
    check_not_contains(worksheet_xml, R"(<c r="B1" s=")",
        "set_column_values dirty style rejection save should keep dirty B1 unstyled");
    check_not_contains(worksheet_xml, R"(<c r="B2" s=")",
        "set_column_values dirty style rejection save should keep dirty B2 unstyled");
    check_not_contains(worksheet_xml, R"(s="0")",
        "set_column_values dirty style rejection save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values dirty style rejection save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        output, "Styled", "set_column_values dirty style rejection save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_column_values dirty style rejection save");
        });

    const std::size_t pending_count_after_save = editor.pending_change_count();
    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "set_column_values dirty style rejection noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "set_column_values dirty style rejection noop save");
    check(editor.pending_change_count() == pending_count_after_save,
        "set_column_values dirty style rejection noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_column_values dirty style rejection noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column_values dirty style rejection noop save should keep dirty diagnostics clear");
    check(editor.last_edit_error().has_value() &&
            editor.last_edit_error()->find("StyleId") != std::string::npos,
        "set_column_values dirty style rejection noop save should preserve the rejection diagnostic");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column_values dirty style rejection noop save should not queue replacement diagnostics");
    check_dirty_views(sheet, "set_column_values dirty style rejection noop saved handle");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "set_column_values dirty style rejection noop output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values dirty style rejection noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "set_column_values dirty style rejection noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_dirty_views(reopened_sheet, "set_column_values dirty style rejection noop save");
        });

    sheet.set_column_values(2, {
        fastxlsx::CellValue::text("set-column-values-dirty-recovered")
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::blank().with_style(fastxlsx::StyleId {}),
        fastxlsx::CellValue::boolean(true).with_style(fastxlsx::StyleId {}),
    });
    check(!editor.last_edit_error().has_value(),
        "set_column_values dirty style rejection recovery should clear the retained StyleId diagnostic");
    check(sheet.has_pending_changes(),
        "set_column_values dirty style rejection recovery should dirty the materialized sheet again");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", pending_count_after_save,
        "set_column_values dirty style rejection recovery");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column_values dirty style rejection recovery should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_column_values dirty style rejection recovery live");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_recovery_save =
        workbook_editor_public_catalog_snapshot(editor);
    editor.save_as(recovery_output);
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_save,
        "set_column_values dirty style rejection recovery save");
    check(!sheet.has_pending_changes(),
        "set_column_values dirty style rejection recovery save should clean the materialized sheet");
    check(editor.pending_change_count() == pending_count_after_save + 1,
        "set_column_values dirty style rejection recovery save should record one more materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column_values dirty style rejection recovery save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "set_column_values dirty style rejection recovery save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column_values dirty style rejection recovery save should not queue replacement diagnostics");
    check_recovery_views(sheet, "set_column_values dirty style rejection recovery saved handle");

    const auto recovery_entries = fastxlsx::test::read_zip_entries(recovery_output);
    check(recovery_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "set_column_values dirty style rejection recovery save should preserve source styles.xml bytes");
    const std::string recovery_worksheet_xml =
        recovery_entries.at("xl/worksheets/sheet1.xml");
    check_contains(recovery_worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "set_column_values dirty style rejection recovery save should extend bounds to B3");
    check_contains(recovery_worksheet_xml, "set-column-values-dirty-source-a2",
        "set_column_values dirty style rejection recovery save should preserve source A2");
    check_contains(recovery_worksheet_xml, "set-column-values-dirty-recovered",
        "set_column_values dirty style rejection recovery save should persist recovered B1");
    check_contains(recovery_worksheet_xml, R"(<c r="B2"/>)",
        "set_column_values dirty style rejection recovery save should persist recovered B2 blank");
    check_contains(recovery_worksheet_xml, R"(<c r="B3" t="b"><v>1</v></c>)",
        "set_column_values dirty style rejection recovery save should persist recovered B3 boolean");
    check_not_contains(recovery_worksheet_xml, "set-column-values-dirty-kept",
        "set_column_values dirty style rejection recovery save should replace prior dirty B1");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2"><f>A1</f></c>)",
        "set_column_values dirty style rejection recovery save should replace prior dirty B2 formula");
    check_not_contains(recovery_worksheet_xml, "set-column-values-dirty-source-b1",
        "set_column_values dirty style rejection recovery save should not revive source B1");
    check_not_contains(recovery_worksheet_xml, "set-column-values-dirty-rejected",
        "set_column_values dirty style rejection recovery save should not leak rejected payloads");
    check_contains(recovery_worksheet_xml,
        R"(<c r="A1" s=")" + std::to_string(source_style.value()) + R"("><v>1</v></c>)",
        "set_column_values dirty style rejection recovery save should keep source A1 styled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B1" s=")",
        "set_column_values dirty style rejection recovery save should keep recovered B1 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B2" s=")",
        "set_column_values dirty style rejection recovery save should keep recovered B2 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(<c r="B3" s=")",
        "set_column_values dirty style rejection recovery save should keep recovered B3 unstyled");
    check_not_contains(recovery_worksheet_xml, R"(s="0")",
        "set_column_values dirty style rejection recovery save should not write default style ids");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values dirty style rejection recovery save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_output, "Styled",
        "set_column_values dirty style rejection recovery save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_column_values dirty style rejection recovery save");
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
        "set_column_values dirty style rejection recovery noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_recovery_noop,
        "set_column_values dirty style rejection recovery noop save");
    check(editor.pending_change_count() == pending_count_after_recovery_save,
        "set_column_values dirty style rejection recovery noop save should not add another handoff");
    check(!sheet.has_pending_changes(),
        "set_column_values dirty style rejection recovery noop save should keep the sheet clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "set_column_values dirty style rejection recovery noop save should keep dirty diagnostics clear");
    check(!editor.last_edit_error().has_value(),
        "set_column_values dirty style rejection recovery noop save should keep diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "set_column_values dirty style rejection recovery noop save should not queue replacement diagnostics");
    check_recovery_views(
        sheet, "set_column_values dirty style rejection recovery noop saved handle");
    const auto recovery_noop_entries =
        fastxlsx::test::read_zip_entries(recovery_noop_output);
    check(recovery_noop_entries == recovery_entries,
        "set_column_values dirty style rejection recovery noop output should match the recovered output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "set_column_values dirty style rejection recovery noop save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        recovery_noop_output, "Styled",
        "set_column_values dirty style rejection recovery noop save",
        [&](fastxlsx::WorksheetEditor& reopened_sheet) {
            check_recovery_views(
                reopened_sheet, "set_column_values dirty style rejection recovery noop save");
        });
}

void test_public_worksheet_editor_set_column_values_noop_invalid_and_budget()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-column-values-source.xlsx");

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-second-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_column_values(1, {
            fastxlsx::CellValue::text("column-value-a1"),
            fastxlsx::CellValue::blank(),
            fastxlsx::CellValue::number(7.0),
        });

        check(sheet.cell_count() == 4,
            "set_column_values should update the column prefix without removing sparse tail cells");
        check(sheet.get_cell("A1").text_value() == "column-value-a1",
            "set_column_values should write the first value to row one");
        check(sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Blank,
            "set_column_values should represent explicit blank prefix values");
        check(sheet.get_cell("A3").number_value() == 7.0,
            "set_column_values should write numeric prefix values");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values should preserve represented cells outside the written prefix");
        check(sheet.has_pending_changes(),
            "set_column_values should dirty the materialized worksheet when values change");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 0, "set_column_values dirty summary");
        check(!editor.last_edit_error().has_value(),
            "successful set_column_values should keep diagnostics clear");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
            "set_column_values should refresh the dirty worksheet dimension");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="inlineStr"><is><t>column-value-a1</t></is></c>)",
            "set_column_values should persist column prefix text cells");
        check_contains(worksheet_xml, R"(<c r="A2"/>)",
            "set_column_values should persist explicit blank prefix cells");
        check_contains(worksheet_xml, R"(<c r="A3"><v>7</v></c>)",
            "set_column_values should persist column prefix numeric cells");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_column_values should keep non-target column source cells");
        check_not_contains(worksheet_xml, "placeholder-a1",
            "set_column_values should omit the overwritten row-one source value");
        check_not_contains(worksheet_xml, "placeholder-a2",
            "set_column_values should omit the overwritten row-two source value");
        check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
            "set_column_values should preserve untouched worksheets");
        const auto inspect_set_column_values_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 4,
                    "set_column_values reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
                    "set_column_values reopened output should keep written column bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "column-value-a1",
                    "set_column_values reopened output should read column prefix text");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Blank,
                    "set_column_values reopened output should read explicit blank prefix");
                const fastxlsx::CellValue reopened_a3 = reopened_sheet.get_cell("A3");
                check(reopened_a3.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_a3.number_value() == 7.0,
                    "set_column_values reopened output should read numeric prefix");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "set_column_values reopened output should keep non-target columns");
            };
        check_reopened_clean_sheet_output(output, "Data", "set_column_values",
            inspect_set_column_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_column_values no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "set_column_values no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_column_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_column_values no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_column_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_column_values no-op save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_column_values no-op output should match the first materialized output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            noop_output, "Data", "set_column_values no-op save",
            inspect_set_column_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "set_column_values second no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "set_column_values second no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_column_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_column_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "set_column_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "set_column_values second no-op save");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
            "set_column_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "set_column_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values second no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            second_noop_output, "Data", "set_column_values second no-op save",
            inspect_set_column_values_output);
    }

    {
        const std::filesystem::path style_source =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-style-source.xlsx");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-style-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-noop-output.xlsx");
        const std::filesystem::path second_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-second-noop-output.xlsx");
        const std::filesystem::path style_reject_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-reject-output.xlsx");
        const std::filesystem::path style_reject_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-style-reject-noop-output.xlsx");
        fastxlsx::StyleId non_default_style;
        {
            fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(style_source);
            {
                fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
                non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
                styled_sheet.append_row({
                    fastxlsx::CellView::number(1.0).with_style(non_default_style),
                });
                styled_sheet.append_row({fastxlsx::CellView::text("column-tail")});
            }
            writer.close();
        }
        const auto style_source_entries = fastxlsx::test::read_zip_entries(style_source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(style_source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");
        sheet.set_column_values(1, {fastxlsx::CellValue::text("styled-column-value")});

        const fastxlsx::CellValue styled_a1 = sheet.get_cell("A1");
        check(styled_a1.kind() == fastxlsx::CellValueKind::Text
                && styled_a1.text_value() == "styled-column-value"
                && styled_a1.has_style()
                && styled_a1.style_id().value() == non_default_style.value(),
            "set_column_values should preserve source style ids on overwritten targets");
        check(sheet.get_cell("A2").text_value() == "column-tail",
            "set_column_values should leave column cells beyond the input prefix untouched");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_column_values save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_text =
            R"(<c r="A1" s=")" + std::to_string(non_default_style.value())
            + R"(" t="inlineStr"><is><t>styled-column-value</t></is></c>)";
        check_contains(worksheet_xml, styled_text,
            "set_column_values should persist value-only edits with the source style id");
        check_contains(worksheet_xml, "column-tail",
            "set_column_values should persist untouched column tail cells");
        const auto inspect_styled_set_column_values_output =
            [non_default_style](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 2,
                    "styled set_column_values reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 1,
                    "styled set_column_values reopened output should keep column bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "styled-column-value" &&
                        reopened_a1.has_style() &&
                        reopened_a1.style_id().value() == non_default_style.value(),
                    "styled set_column_values reopened output should preserve source style id");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "column-tail",
                    "styled set_column_values reopened output should keep column tail");
            };
        const auto check_styled_set_column_values_saved_snapshot =
            [&](std::size_t expected_pending_count, std::string_view scenario) {
                const std::string prefix(scenario);

                check(sheet.cell_count() == 2,
                    prefix + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    sheet.sparse_cells();
                check(cells.size() == 2,
                    prefix + " should expose the two represented records");
                if (cells.size() == 2) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "styled-column-value" &&
                            cells[0].value.has_style() &&
                            cells[0].value.style_id().value() == non_default_style.value(),
                        prefix + " should keep styled A1 first");
                    check(cells[1].reference.row == 2 &&
                            cells[1].reference.column == 1 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[1].value.text_value() == "column-tail" &&
                            !cells[1].value.has_style(),
                        prefix + " should keep unstyled column tail second");
                }

                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    sheet.row_cells(1);
                check(row_one.size() == 1 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "styled-column-value" &&
                        row_one[0].value.has_style() &&
                        row_one[0].value.style_id().value() ==
                            non_default_style.value(),
                    prefix + " should keep row-one styled A1");

                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    sheet.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "column-tail" &&
                        !row_two[0].value.has_style(),
                    prefix + " should keep row-two column tail");

                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    sheet.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "styled-column-value" &&
                        column_one[0].value.has_style() &&
                        column_one[0].value.style_id().value() ==
                            non_default_style.value() &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "column-tail" &&
                        !column_one[1].value.has_style(),
                    prefix + " should keep column-one styled value and source tail");

                check_cell_range_equals(sheet.used_range(), 1, 1, 2, 1,
                    prefix + " should keep saved column bounds");
                check(!sheet.has_pending_changes(),
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
            };
        check_reopened_clean_sheet_output(output, "Styled", "styled set_column_values",
            inspect_styled_set_column_values_output);
        const std::size_t pending_count_after_save = editor.pending_change_count();

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "styled set_column_values no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "styled set_column_values no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "styled set_column_values no-op save should keep dirty diagnostics clear");
        check(editor.pending_worksheet_edits().empty(),
            "styled set_column_values no-op save should not leave dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "styled set_column_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "styled set_column_values no-op save should keep diagnostics clear");
        check_styled_set_column_values_saved_snapshot(
            pending_count_after_save, "styled set_column_values saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "styled set_column_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "styled set_column_values no-op save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "styled set_column_values no-op output should match the materialized output");
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_column_values no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            noop_output, "Styled", "styled set_column_values no-op save",
            inspect_styled_set_column_values_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(second_noop_output);
        check(!sheet.has_pending_changes(),
            "styled set_column_values second no-op save should keep the materialized sheet clean");
        check(editor.pending_change_count() == 1,
            "styled set_column_values second no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "styled set_column_values second no-op save should keep dirty diagnostics clear");
        check(editor.pending_worksheet_edits().empty(),
            "styled set_column_values second no-op save should not leave dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "styled set_column_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "styled set_column_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "styled set_column_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "styled set_column_values second no-op save");
        check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
            "styled set_column_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
            "styled set_column_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(style_source) == style_source_entries,
            "styled set_column_values second no-op save should leave the source package unchanged");
        check_reopened_clean_sheet_output(
            second_noop_output, "Styled", "styled set_column_values second no-op save",
            inspect_styled_set_column_values_output);

        fastxlsx::WorkbookEditor style_reject_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor reject_sheet = style_reject_editor.worksheet("Data");
        bool failed = false;
        try {
            reject_sheet.set_column_values(1, {
                fastxlsx::CellValue::text("styled-column-rejected")
                    .with_style(non_default_style),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "StyleId",
                "set_column_values style failure should expose the unsupported StyleId boundary");
        }
        check(failed,
            "set_column_values should reject caller-supplied non-default StyleId values");
        check(style_reject_editor.last_edit_error().has_value(),
            "failed set_column_values style mutation should update last_edit_error");
        check(!reject_sheet.has_pending_changes(),
            "set_column_values style failure should not dirty the materialized worksheet");
        check(reject_sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_column_values style failure should preserve existing cells");
        const auto style_reject_source_entries = fastxlsx::test::read_zip_entries(source);
        const WorkbookEditorPublicCatalogSnapshot catalog_before_style_reject_save =
            workbook_editor_public_catalog_snapshot(style_reject_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_style_reject_save =
            workbook_editor_public_save_state_snapshot(style_reject_editor);

        style_reject_editor.save_as(style_reject_output);
        check_workbook_editor_public_save_state_preserved(
            style_reject_editor,
            save_state_before_style_reject_save,
            "set_column_values style rejection save");
        check_workbook_editor_public_catalog_preserved(
            style_reject_editor,
            catalog_before_style_reject_save,
            "set_column_values style rejection save");
        check_workbook_editor_public_no_pending_state(
            style_reject_editor,
            "set_column_values style rejection save");
        check(!reject_sheet.has_pending_changes(),
            "set_column_values style rejection save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            style_reject_editor,
            "set_column_values style rejection save should not queue replacement diagnostics");
        const auto style_reject_output_entries =
            fastxlsx::test::read_zip_entries(style_reject_output);
        check(style_reject_output_entries == style_reject_source_entries,
            "set_column_values style rejection save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == style_reject_source_entries,
            "set_column_values style rejection save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            style_reject_output, "set_column_values style rejection save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_style_reject_noop =
            workbook_editor_public_catalog_snapshot(style_reject_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_style_reject_noop =
            workbook_editor_public_save_state_snapshot(style_reject_editor);
        style_reject_editor.save_as(style_reject_noop_output);
        check_workbook_editor_public_save_state_preserved(
            style_reject_editor,
            save_state_before_style_reject_noop,
            "set_column_values style rejection noop save");
        check_workbook_editor_public_catalog_preserved(
            style_reject_editor,
            catalog_before_style_reject_noop,
            "set_column_values style rejection noop save");
        check_workbook_editor_public_no_pending_state(
            style_reject_editor,
            "set_column_values style rejection noop save");
        check(!reject_sheet.has_pending_changes(),
            "set_column_values style rejection noop save should keep the materialized sheet clean");
        check_workbook_editor_no_replacement_diagnostics(
            style_reject_editor,
            "set_column_values style rejection noop save should not queue replacement diagnostics");
        const auto style_reject_noop_entries =
            fastxlsx::test::read_zip_entries(style_reject_noop_output);
        check(style_reject_noop_entries == style_reject_source_entries,
            "set_column_values style rejection noop save should still copy source entries");
        check(style_reject_noop_entries == style_reject_output_entries,
            "set_column_values style rejection noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == style_reject_source_entries,
            "set_column_values style rejection noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            style_reject_noop_output, "set_column_values style rejection noop save");
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-empty-noop-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-empty-noop-second-output.xlsx");
        const std::filesystem::path invalid_column_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-invalid-column-output.xlsx");
        const std::filesystem::path invalid_column_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-invalid-column-noop-output.xlsx");
        const std::filesystem::path overflow_column_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-overflow-column-output.xlsx");
        const std::filesystem::path overflow_column_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-overflow-column-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        check(threw_fastxlsx_error([&] {
            sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-lowercase"));
        }), "invalid mutation should seed last_edit_error before set_column_values empty no-op");
        check(editor.last_edit_error().has_value(),
            "invalid mutation should populate last_edit_error before set_column_values empty no-op");

        const std::vector<fastxlsx::CellValue> empty_values;
        sheet.set_column_values(3, empty_values);
        check(!editor.last_edit_error().has_value(),
            "empty set_column_values should clear prior public edit diagnostics");
        check(!sheet.has_pending_changes(),
            "empty set_column_values should not dirty a clean materialized worksheet");
        check(sheet.cell_count() == 3,
            "empty set_column_values should not create sparse column metadata");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "empty set_column_values no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty set_column_values no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty set_column_values no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty set_column_values no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty set_column_values no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty set_column_values no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_save,
            "empty set_column_values no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_save,
            "empty set_column_values no-op save");
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(output_entries == source_entries,
            "empty set_column_values no-op save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty set_column_values no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            output, "empty set_column_values no-op save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "empty set_column_values second no-op save should keep the materialized sheet clean");
        check(!editor.has_pending_changes(),
            "empty set_column_values second no-op save should keep the editor clean");
        check(editor.pending_change_count() == 0,
            "empty set_column_values second no-op save should not record a materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "empty set_column_values second no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "empty set_column_values second no-op save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "empty set_column_values second no-op save should keep diagnostics clear");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_second_noop,
            "empty set_column_values second no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_second_noop,
            "empty set_column_values second no-op save");
        check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
            "empty set_column_values second no-op output should match the first no-op output");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "empty set_column_values second no-op save should leave the first no-op output unchanged");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "empty set_column_values second no-op save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            noop_output, "empty set_column_values second no-op save");

        bool invalid_failed = false;
        try {
            sheet.set_column_values(0, {fastxlsx::CellValue::text("invalid-column")});
        } catch (const fastxlsx::FastXlsxError&) {
            invalid_failed = true;
        }
        check(invalid_failed, "set_column_values should reject invalid column numbers");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values invalid-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_column_values invalid-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_column_values invalid-column failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_column_values invalid-column failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values invalid-column failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_column_values invalid-column failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "set_column_values invalid-column failure");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_column_values invalid-column failure should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_column_values invalid-column failure should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values invalid-column failure should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values invalid-column failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_column_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_invalid_column_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(invalid_column_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_invalid_column_save,
            "set_column_values invalid-column failure save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_invalid_column_save,
            "set_column_values invalid-column failure save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values invalid-column failure save");
        check(!sheet.has_pending_changes(),
            "set_column_values invalid-column failure save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values invalid-column failure save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values invalid-column failure save should not queue replacement diagnostics");
        const auto invalid_column_output_entries =
            fastxlsx::test::read_zip_entries(invalid_column_output);
        check(invalid_column_output_entries == source_entries,
            "set_column_values invalid-column failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values invalid-column failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            invalid_column_output, "set_column_values invalid-column failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_invalid_column_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_invalid_column_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(invalid_column_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_invalid_column_noop,
            "set_column_values invalid-column failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_invalid_column_noop,
            "set_column_values invalid-column failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values invalid-column failure noop save");
        check(!sheet.has_pending_changes(),
            "set_column_values invalid-column failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values invalid-column failure noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values invalid-column failure noop save should not queue replacement diagnostics");
        const auto invalid_column_noop_entries =
            fastxlsx::test::read_zip_entries(invalid_column_noop_output);
        check(invalid_column_noop_entries == source_entries,
            "set_column_values invalid-column failure noop save should still copy source entries");
        check(invalid_column_noop_entries == invalid_column_output_entries,
            "set_column_values invalid-column failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values invalid-column failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            invalid_column_noop_output, "set_column_values invalid-column failure noop save");

        bool overflow_failed = false;
        try {
            sheet.set_column_values(16385, {fastxlsx::CellValue::text("overflow-column")});
        } catch (const fastxlsx::FastXlsxError&) {
            overflow_failed = true;
        }
        check(overflow_failed,
            "set_column_values should reject columns beyond the worksheet limit");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values overflow-column mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "set_column_values overflow-column failure should not dirty the materialized worksheet");
        check(sheet.cell_count() == 3,
            "set_column_values overflow-column failure should preserve sparse cell count");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "set_column_values overflow-column failure should preserve source A1");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values overflow-column failure should preserve source B1");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_column_values overflow-column failure should preserve source A2");
        check_workbook_editor_public_no_pending_state(
            editor, "set_column_values overflow-column failure");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_column_values overflow-column failure should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_column_values overflow-column failure should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values overflow-column failure should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values overflow-column failure should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_overflow_column_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_overflow_column_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(overflow_column_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_overflow_column_save,
            "set_column_values overflow-column failure save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_overflow_column_save,
            "set_column_values overflow-column failure save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values overflow-column failure save");
        check(!sheet.has_pending_changes(),
            "set_column_values overflow-column failure save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values overflow-column failure save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values overflow-column failure save should not queue replacement diagnostics");
        const auto overflow_column_output_entries =
            fastxlsx::test::read_zip_entries(overflow_column_output);
        check(overflow_column_output_entries == source_entries,
            "set_column_values overflow-column failure save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values overflow-column failure save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            overflow_column_output, "set_column_values overflow-column failure save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_overflow_column_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_overflow_column_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(overflow_column_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_overflow_column_noop,
            "set_column_values overflow-column failure noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_overflow_column_noop,
            "set_column_values overflow-column failure noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values overflow-column failure noop save");
        check(!sheet.has_pending_changes(),
            "set_column_values overflow-column failure noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values overflow-column failure noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values overflow-column failure noop save should not queue replacement diagnostics");
        const auto overflow_column_noop_entries =
            fastxlsx::test::read_zip_entries(overflow_column_noop_output);
        check(overflow_column_noop_entries == source_entries,
            "set_column_values overflow-column failure noop save should still copy source entries");
        check(overflow_column_noop_entries == overflow_column_output_entries,
            "set_column_values overflow-column failure noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values overflow-column failure noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            overflow_column_noop_output, "set_column_values overflow-column failure noop save");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-noop-output.xlsx");
        const std::filesystem::path output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-reacquire-output.xlsx");
        const std::filesystem::path noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-reacquire-noop-output.xlsx");
        const std::filesystem::path post_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-reacquire-post-noop-output.xlsx");
        const std::filesystem::path reject_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-reject-output.xlsx");
        const std::filesystem::path reject_noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-budget-reject-noop-output.xlsx");
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.max_cells = 3;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();

        bool failed = false;
        try {
            sheet.set_column_values(3, {
                fastxlsx::CellValue::text("max-cells-rejected"),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore max_cells guardrail exceeded",
                "set_column_values should expose CellStore max_cells guardrail diagnostics");
        }
        check(failed,
            "set_column_values should enforce max_cells on staged column-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values max_cells mutation should update last_edit_error");
        check(!sheet.has_pending_changes(),
            "failed set_column_values max_cells mutation should not dirty the session");
        check(sheet.cell_count() == 3,
            "failed set_column_values max_cells mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_column_values max_cells mutation should preserve sparse memory estimate");
        check(!sheet.try_cell("C1").has_value(),
            "failed set_column_values max_cells mutation should not leave rejected cells readable");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "failed set_column_values max_cells mutation should preserve column-prefix row one");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "failed set_column_values max_cells mutation should preserve column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "failed set_column_values max_cells mutation should preserve non-target columns");
        check_workbook_editor_public_no_pending_state(
            editor, "failed set_column_values max_cells mutation");
        check(editor.pending_materialized_worksheet_names().empty(),
            "failed set_column_values max_cells mutation should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "failed set_column_values max_cells mutation should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "failed set_column_values max_cells mutation should not expose dirty materialized memory");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "failed set_column_values max_cells mutation should not queue replacement diagnostics");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reject_save =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reject_save =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(reject_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_reject_save,
            "set_column_values max_cells rejection save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_reject_save,
            "set_column_values max_cells rejection save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values max_cells rejection save");
        check(!sheet.has_pending_changes(),
            "set_column_values max_cells rejection save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values max_cells rejection save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values max_cells rejection save should not queue replacement diagnostics");
        const auto reject_output_entries = fastxlsx::test::read_zip_entries(reject_output);
        check(reject_output_entries == source_entries,
            "set_column_values max_cells rejection save should copy source entries");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values max_cells rejection save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            reject_output, "set_column_values max_cells rejection save");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reject_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reject_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(reject_noop_output);
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_reject_noop,
            "set_column_values max_cells rejection noop save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_reject_noop,
            "set_column_values max_cells rejection noop save");
        check_workbook_editor_public_no_pending_state(
            editor,
            "set_column_values max_cells rejection noop save");
        check(!sheet.has_pending_changes(),
            "set_column_values max_cells rejection noop save should keep the materialized sheet clean");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values max_cells rejection noop save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor,
            "set_column_values max_cells rejection noop save should not queue replacement diagnostics");
        const auto reject_noop_entries = fastxlsx::test::read_zip_entries(reject_noop_output);
        check(reject_noop_entries == source_entries,
            "set_column_values max_cells rejection noop save should still copy source entries");
        check(reject_noop_entries == reject_output_entries,
            "set_column_values max_cells rejection noop output should match the first output");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values max_cells rejection noop save should leave the source package unchanged");
        check_reopened_default_data_sheet_output(
            reject_noop_output, "set_column_values max_cells rejection noop save");

        sheet.set_column_values(1, {fastxlsx::CellValue::text("in-budget-column-value")});
        check(!editor.last_edit_error().has_value(),
            "successful in-budget set_column_values should clear last_edit_error");
        check(sheet.cell_count() == 3,
            "in-budget set_column_values should keep existing sparse tail records");
        check(sheet.get_cell("A1").text_value() == "in-budget-column-value",
            "in-budget set_column_values should update existing prefix cells");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "in-budget set_column_values should preserve column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "in-budget set_column_values should preserve non-target columns");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values guardrail recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "in-budget-column-value",
            "in-budget set_column_values should persist through save_as");
        check_contains(worksheet_xml, "placeholder-a2",
            "in-budget set_column_values should persist column tail cells");
        check_not_contains(worksheet_xml, "max-cells-rejected",
            "rejected set_column_values payload should not leak into saved output");
        const auto inspect_set_column_values_guardrail_recovery_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "set_column_values guardrail recovery reopened output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    "set_column_values guardrail recovery reopened output should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "in-budget-column-value",
                    "set_column_values guardrail recovery reopened output should read replacement A1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "set_column_values guardrail recovery reopened output should keep column tail A2");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "set_column_values guardrail recovery reopened output should keep non-target B1");
                check(!reopened_sheet.try_cell("C1").has_value(),
                    "set_column_values guardrail recovery reopened output should keep rejected C1 absent");
            };
        const auto inspect_set_column_values_guardrail_post_noop_output =
            [](fastxlsx::WorksheetEditor& reopened_sheet) {
                check(reopened_sheet.cell_count() == 3,
                    "set_column_values guardrail recovery post-noop output should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    "set_column_values guardrail recovery post-noop output should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == "col-max-2",
                    "set_column_values guardrail recovery post-noop output should read later A1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    "set_column_values guardrail recovery post-noop output should keep column tail A2");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    "set_column_values guardrail recovery post-noop output should keep non-target B1");
                check(!reopened_sheet.try_cell("C1").has_value(),
                    "set_column_values guardrail recovery post-noop output should keep rejected C1 absent");
                check(!reopened_sheet.try_cell("C2").has_value(),
                    "set_column_values guardrail recovery post-noop output should keep rejected C2 absent");
            };
        check_reopened_clean_sheet_output(output, "Data",
            "set_column_values guardrail recovery",
            inspect_set_column_values_guardrail_recovery_output);
        const auto check_set_column_values_guardrail_recovery_saved_snapshot =
            [&](fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "in-budget-column-value",
                        label + " should keep replaced A1 first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep non-target B1 second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "placeholder-a2",
                        label + " should keep column-tail A2 third");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "in-budget-column-value" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should expose row-one replacement and source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2",
                    label + " should keep the column-tail row-two cell");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "in-budget-column-value" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    label + " should keep column-one replacement and tail cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep the non-target column-two cell");
                check(!handle.try_cell("C1").has_value(),
                    label + " should keep rejected C1 absent");
                check(!handle.try_cell("C2").has_value(),
                    label + " should keep rejected C2 absent");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(editor.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(editor.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(editor.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(editor.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(editor.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    editor,
                    label + " should keep replacement diagnostics empty");
                check(!editor.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_column_values guardrail recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "set_column_values guardrail recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_column_values guardrail recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_column_values guardrail recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values guardrail recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "set_column_values guardrail recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_column_values guardrail recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_column_values guardrail recovery noop save should keep diagnostics clear");
        check_set_column_values_guardrail_recovery_saved_snapshot(
            sheet,
            pending_count_after_save,
            "set_column_values guardrail recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_column_values guardrail recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_column_values guardrail recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_column_values guardrail recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values guardrail recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "set_column_values guardrail recovery noop save",
            inspect_set_column_values_guardrail_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values guardrail recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values guardrail recovery strict-options reacquire");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquire should keep the sheet clean");
        inspect_set_column_values_guardrail_recovery_output(reacquired_sheet);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_save =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_save =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_save,
            "set_column_values guardrail recovery strict-options reacquire save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_save,
            "set_column_values guardrail recovery strict-options reacquire save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values guardrail recovery strict-options reacquire save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquire save should keep the sheet clean");
        const auto reacquire_entries =
            fastxlsx::test::read_zip_entries(output_after_reacquire);
        check(reacquire_entries == output_entries,
            "set_column_values guardrail recovery strict-options reacquire save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values guardrail recovery strict-options reacquire save should leave the source package unchanged");
        check_reopened_clean_sheet_output(output_after_reacquire, "Data",
            "set_column_values guardrail recovery strict-options reacquire save",
            inspect_set_column_values_guardrail_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_noop,
            "set_column_values guardrail recovery strict-options reacquire noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_noop,
            "set_column_values guardrail recovery strict-options reacquire noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values guardrail recovery strict-options reacquire noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquire noop save should keep the sheet clean");
        const auto reacquire_noop_entries =
            fastxlsx::test::read_zip_entries(noop_output_after_reacquire);
        check(reacquire_noop_entries == reacquire_entries,
            "set_column_values guardrail recovery strict-options reacquire noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values guardrail recovery strict-options reacquire noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output_after_reacquire, "Data",
            "set_column_values guardrail recovery strict-options reacquire noop save",
            inspect_set_column_values_guardrail_recovery_output);

        reacquired_sheet.set_column_values(1, {fastxlsx::CellValue::text("col-max-2")});
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep diagnostics clear");
        check(reacquired_sheet.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should dirty the sheet");
        check(reacquired_editor.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should dirty the editor");
        check(reacquired_sheet.cell_count() == 3,
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep sparse count stable");
        check(reacquired_editor.pending_materialized_cell_count() == 3,
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should expose dirty sparse count");
        check(reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A1").text_value() == "col-max-2",
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should overwrite A1");
        check(reacquired_sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep column tail A2");
        check(reacquired_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number &&
                reacquired_sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep non-target B1");
        check(!reacquired_sheet.try_cell("C1").has_value(),
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep rejected C1 absent");
        check(!reacquired_sheet.try_cell("C2").has_value(),
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should keep rejected C2 absent");
        check_public_state_single_data_dirty_materialized_summary(
            reacquired_editor,
            reacquired_sheet,
            0,
            "set_column_values guardrail recovery strict-options reacquired post-noop edit");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_column_values guardrail recovery strict-options reacquired post-noop edit should not queue replacement diagnostics");

        reacquired_editor.save_as(post_noop_output_after_reacquire);
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values guardrail recovery strict-options reacquired post-noop save should clean the sheet");
        check(reacquired_editor.pending_change_count() == 1,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should keep one handoff");
        check(reacquired_editor.pending_materialized_worksheet_names().empty(),
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
        check(reacquired_editor.pending_materialized_cell_count() == 0,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
        check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
        check(reacquired_editor.pending_worksheet_edits().empty(),
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values guardrail recovery strict-options reacquired post-noop save should keep diagnostics clear");
        const auto post_noop_reacquire_entries =
            fastxlsx::test::read_zip_entries(post_noop_output_after_reacquire);
        const std::string post_noop_reacquire_xml =
            post_noop_reacquire_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reacquire_xml, "col-max-2",
            "set_column_values guardrail recovery strict-options reacquired post-noop save should persist the later overwrite");
        check_not_contains(post_noop_reacquire_xml, "in-budget-column-value",
            "set_column_values guardrail recovery strict-options reacquired post-noop save should replace the earlier A1 text");
        check_not_contains(post_noop_reacquire_xml, "max-cells-rejected",
            "set_column_values guardrail recovery strict-options reacquired post-noop save should not leak rejected payload");
        check_contains(post_noop_reacquire_xml, "placeholder-a2",
            "set_column_values guardrail recovery strict-options reacquired post-noop save should keep column tail A2");
        check_contains(post_noop_reacquire_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_column_values guardrail recovery strict-options reacquired post-noop save should keep non-target B1");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should leave the first no-op output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should leave the second no-op output stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values guardrail recovery strict-options reacquired post-noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(post_noop_output_after_reacquire, "Data",
            "set_column_values guardrail recovery strict-options reacquired post-noop save",
            inspect_set_column_values_guardrail_post_noop_output);
    }

    {
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-recovery-output.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-recovery-noop-output.xlsx");
        const std::filesystem::path output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-reacquire-output.xlsx");
        const std::filesystem::path noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-reacquire-noop-output.xlsx");
        const std::filesystem::path second_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-reacquire-second-noop-output.xlsx");
        const std::filesystem::path post_noop_output_after_reacquire = artifact(
            "fastxlsx-workbook-editor-public-worksheet-set-column-values-memory-budget-reacquire-post-noop-output.xlsx");
        const std::string rejected_value =
            "set-column-values-memory-rejected-" + std::string(4096, 'c');
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sizing_sheet = sizing_editor.worksheet("Data");
        const std::size_t exact_memory_budget = sizing_sheet.estimated_memory_usage();

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditorOptions options;
        options.memory_budget_bytes = exact_memory_budget;
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", options);
        const std::size_t baseline_memory = sheet.estimated_memory_usage();
        check(baseline_memory == exact_memory_budget,
            "set_column_values memory-budget precondition should load with an exact sparse budget");

        bool failed = false;
        try {
            sheet.set_column_values(1, {
                fastxlsx::CellValue::text(rejected_value),
            });
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "CellStore memory_budget_bytes guardrail exceeded",
                "set_column_values should expose CellStore memory-budget diagnostics");
        }
        check(failed,
            "set_column_values should enforce memory_budget_bytes on staged column-prefix writes");
        check(editor.last_edit_error().has_value(),
            "failed set_column_values memory-budget mutation should update last_edit_error");
        if (editor.last_edit_error().has_value()) {
            check_contains(*editor.last_edit_error(),
                "CellStore memory_budget_bytes guardrail exceeded",
                "last_edit_error should retain the set_column_values memory-budget diagnostic");
        }
        check(!sheet.has_pending_changes(),
            "failed set_column_values memory-budget mutation should not dirty the session");
        check(!editor.has_pending_changes(),
            "failed set_column_values memory-budget mutation should not dirty the editor");
        check(sheet.cell_count() == 3,
            "failed set_column_values memory-budget mutation should preserve sparse cell count");
        check(sheet.estimated_memory_usage() == baseline_memory,
            "failed set_column_values memory-budget mutation should preserve sparse memory estimate");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1",
            "failed set_column_values memory-budget mutation should preserve column-prefix row one");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "failed set_column_values memory-budget mutation should preserve column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "failed set_column_values memory-budget mutation should preserve non-target columns");

        sheet.set_column_values(1, {fastxlsx::CellValue::text("col-mb-ok")});
        check(!editor.last_edit_error().has_value(),
            "successful set_column_values recovery should clear memory-budget diagnostics");
        check(sheet.has_pending_changes(),
            "successful set_column_values recovery should dirty the session");
        check(editor.has_pending_changes(),
            "successful set_column_values recovery should dirty the editor");
        check(sheet.cell_count() == 3,
            "successful set_column_values recovery should preserve sparse tail records");
        check(sheet.estimated_memory_usage() <= exact_memory_budget,
            "successful set_column_values recovery should stay within the exact memory budget");
        check(sheet.get_cell("A1").text_value() == "col-mb-ok",
            "successful set_column_values recovery should update the prefix cell");
        check(sheet.get_cell("A2").text_value() == "placeholder-a2",
            "successful set_column_values recovery should preserve column tail cells");
        check(sheet.get_cell("B1").number_value() == 1.0,
            "successful set_column_values recovery should preserve non-target columns");

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery save should leave the source package unchanged");
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, "col-mb-ok",
            "set_column_values memory-budget recovery should persist through save_as");
        check_contains(worksheet_xml, "placeholder-a2",
            "set_column_values memory-budget recovery should persist column tail cells");
        check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_column_values memory-budget recovery should persist non-target columns");
        check_not_contains(worksheet_xml, "set-column-values-memory-rejected",
            "rejected set_column_values memory-budget payload should not leak into saved output");
        const auto inspect_set_column_values_memory_budget_output_with_a1 =
            [](fastxlsx::WorksheetEditor& reopened_sheet,
                std::string_view expected_a1,
                std::string_view prefix) {
                const std::string label(prefix);
                check(reopened_sheet.cell_count() == 3,
                    label + " should keep sparse count");
                check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
                check(reopened_a1.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a1.text_value() == expected_a1,
                    label + " should read A1");
                const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
                check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                        reopened_a2.text_value() == "placeholder-a2",
                    label + " should keep column tail A2");
                const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
                check(reopened_b1.kind() == fastxlsx::CellValueKind::Number &&
                        reopened_b1.number_value() == 1.0,
                    label + " should keep non-target B1");
            };
        const auto inspect_set_column_values_memory_budget_recovery_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_set_column_values_memory_budget_output_with_a1(
                    reopened_sheet,
                    "col-mb-ok",
                    "set_column_values memory-budget recovery reopened output");
            };
        const auto inspect_set_column_values_memory_budget_post_noop_output =
            [&](fastxlsx::WorksheetEditor& reopened_sheet) {
                inspect_set_column_values_memory_budget_output_with_a1(
                    reopened_sheet,
                    "col-mb-2",
                    "set_column_values memory-budget recovery post-noop output");
            };
        check_reopened_clean_sheet_output(output, "Data",
            "set_column_values memory-budget recovery",
            inspect_set_column_values_memory_budget_recovery_output);
        const auto check_set_column_values_memory_budget_recovery_saved_snapshot =
            [&](fastxlsx::WorksheetEditor& handle,
                std::size_t expected_pending_count,
                std::string_view prefix) {
                const std::string label(prefix);
                check(handle.cell_count() == 3,
                    label + " should keep the represented sparse count");
                const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                    handle.sparse_cells();
                check(cells.size() == 3,
                    label + " should expose the three represented records");
                if (cells.size() == 3) {
                    check(cells[0].reference.row == 1 &&
                            cells[0].reference.column == 1 &&
                            cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[0].value.text_value() == "col-mb-ok",
                        label + " should keep recovered A1 first");
                    check(cells[1].reference.row == 1 &&
                            cells[1].reference.column == 2 &&
                            cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
                            cells[1].value.number_value() == 1.0,
                        label + " should keep non-target B1 second");
                    check(cells[2].reference.row == 2 &&
                            cells[2].reference.column == 1 &&
                            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
                            cells[2].value.text_value() == "placeholder-a2",
                        label + " should keep column-tail A2 third");
                }
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                    handle.row_cells(1);
                check(row_one.size() == 2 &&
                        row_one[0].reference.row == 1 &&
                        row_one[0].reference.column == 1 &&
                        row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_one[0].value.text_value() == "col-mb-ok" &&
                        row_one[1].reference.row == 1 &&
                        row_one[1].reference.column == 2 &&
                        row_one[1].value.kind() == fastxlsx::CellValueKind::Number &&
                        row_one[1].value.number_value() == 1.0,
                    label + " should expose row-one recovery and source cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                    handle.row_cells(2);
                check(row_two.size() == 1 &&
                        row_two[0].reference.row == 2 &&
                        row_two[0].reference.column == 1 &&
                        row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        row_two[0].value.text_value() == "placeholder-a2",
                    label + " should keep the column-tail row-two cell");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                    handle.column_cells(1);
                check(column_one.size() == 2 &&
                        column_one[0].reference.row == 1 &&
                        column_one[0].reference.column == 1 &&
                        column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[0].value.text_value() == "col-mb-ok" &&
                        column_one[1].reference.row == 2 &&
                        column_one[1].reference.column == 1 &&
                        column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        column_one[1].value.text_value() == "placeholder-a2",
                    label + " should keep column-one recovery and tail cells");
                const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                    handle.column_cells(2);
                check(column_two.size() == 1 &&
                        column_two[0].reference.row == 1 &&
                        column_two[0].reference.column == 2 &&
                        column_two[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        column_two[0].value.number_value() == 1.0,
                    label + " should keep the non-target column-two cell");
                check_cell_range_equals(handle.used_range(), 1, 1, 2, 2,
                    label + " should keep source bounds");
                check(!handle.has_pending_changes(),
                    label + " should keep the handle clean");
                check(editor.pending_change_count() == expected_pending_count,
                    label + " should not add another materialized handoff");
                check(editor.pending_materialized_worksheet_names().empty(),
                    label + " should keep dirty materialized names empty");
                check(editor.pending_materialized_cell_count() == 0,
                    label + " should keep dirty materialized cells empty");
                check(editor.estimated_pending_materialized_memory_usage() == 0,
                    label + " should keep dirty materialized memory empty");
                check(editor.pending_worksheet_edits().empty(),
                    label + " should keep dirty summaries empty");
                check_workbook_editor_no_replacement_diagnostics(
                    editor,
                    label + " should keep replacement diagnostics empty");
                check(!editor.last_edit_error().has_value(),
                    label + " should keep diagnostics clear");
            };

        const std::size_t pending_count_after_save = editor.pending_change_count();
        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            "set_column_values memory-budget recovery noop save should keep the materialized handle clean");
        check(editor.pending_change_count() == pending_count_after_save,
            "set_column_values memory-budget recovery noop save should not add another handoff");
        check(editor.pending_materialized_worksheet_names().empty(),
            "set_column_values memory-budget recovery noop save should not expose dirty worksheet names");
        check(editor.pending_materialized_cell_count() == 0,
            "set_column_values memory-budget recovery noop save should not expose dirty materialized cells");
        check(editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values memory-budget recovery noop save should not expose dirty materialized memory");
        check(editor.pending_worksheet_edits().empty(),
            "set_column_values memory-budget recovery noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "set_column_values memory-budget recovery noop save should not queue replacement diagnostics");
        check(!editor.last_edit_error().has_value(),
            "set_column_values memory-budget recovery noop save should keep diagnostics clear");
        check_set_column_values_memory_budget_recovery_saved_snapshot(
            sheet,
            pending_count_after_save,
            "set_column_values memory-budget recovery saved handle");
        check_workbook_editor_public_save_state_preserved(
            editor, save_state_before_noop,
            "set_column_values memory-budget recovery noop save");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before_noop,
            "set_column_values memory-budget recovery noop save");
        const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
        check(noop_entries == output_entries,
            "set_column_values memory-budget recovery noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output, "Data",
            "set_column_values memory-budget recovery noop save",
            inspect_set_column_values_memory_budget_recovery_output);

        fastxlsx::WorkbookEditor reacquired_editor = fastxlsx::WorkbookEditor::open(output);
        fastxlsx::WorksheetEditor reacquired_sheet =
            reacquired_editor.worksheet("Data", options);
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values memory-budget recovery strict-options reacquire should keep diagnostics clear");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquire");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquire should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_column_values memory-budget recovery strict-options reacquire should stay within the original budget");
        inspect_set_column_values_memory_budget_recovery_output(reacquired_sheet);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_save =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_save =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_save,
            "set_column_values memory-budget recovery strict-options reacquire save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_save,
            "set_column_values memory-budget recovery strict-options reacquire save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquire save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquire save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_column_values memory-budget recovery strict-options reacquire save should keep the original memory budget");
        const auto reacquire_entries =
            fastxlsx::test::read_zip_entries(output_after_reacquire);
        check(reacquire_entries == output_entries,
            "set_column_values memory-budget recovery strict-options reacquire save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery strict-options reacquire save should leave the source package unchanged");
        check_reopened_clean_sheet_output(output_after_reacquire, "Data",
            "set_column_values memory-budget recovery strict-options reacquire save",
            inspect_set_column_values_memory_budget_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_noop,
            "set_column_values memory-budget recovery strict-options reacquire noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_noop,
            "set_column_values memory-budget recovery strict-options reacquire noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquire noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquire noop save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_column_values memory-budget recovery strict-options reacquire noop save should keep the original memory budget");
        const auto reacquire_noop_entries =
            fastxlsx::test::read_zip_entries(noop_output_after_reacquire);
        check(reacquire_noop_entries == reacquire_entries,
            "set_column_values memory-budget recovery strict-options reacquire noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery strict-options reacquire noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(noop_output_after_reacquire, "Data",
            "set_column_values memory-budget recovery strict-options reacquire noop save",
            inspect_set_column_values_memory_budget_recovery_output);

        const WorkbookEditorPublicCatalogSnapshot catalog_before_reacquire_second_noop =
            workbook_editor_public_catalog_snapshot(reacquired_editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_reacquire_second_noop =
            workbook_editor_public_save_state_snapshot(reacquired_editor);
        reacquired_editor.save_as(second_noop_output_after_reacquire);
        check_workbook_editor_public_save_state_preserved(
            reacquired_editor,
            save_state_before_reacquire_second_noop,
            "set_column_values memory-budget recovery strict-options reacquire second noop save");
        check_workbook_editor_public_catalog_preserved(
            reacquired_editor,
            catalog_before_reacquire_second_noop,
            "set_column_values memory-budget recovery strict-options reacquire second noop save");
        check_workbook_editor_public_no_pending_state(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquire second noop save");
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquire second noop save should keep the sheet clean");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should keep the original memory budget");
        const auto reacquire_second_noop_entries =
            fastxlsx::test::read_zip_entries(second_noop_output_after_reacquire);
        check(reacquire_second_noop_entries == reacquire_noop_entries,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should keep output entries stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should leave the source package unchanged");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should leave the first reacquire output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_column_values memory-budget recovery strict-options reacquire second noop save should leave the first no-op output stable");
        check_reopened_clean_sheet_output(second_noop_output_after_reacquire, "Data",
            "set_column_values memory-budget recovery strict-options reacquire second noop save",
            inspect_set_column_values_memory_budget_recovery_output);

        reacquired_sheet.set_column_values(1, {fastxlsx::CellValue::text("col-mb-2")});
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should keep diagnostics clear");
        check(reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should dirty the sheet");
        check(reacquired_editor.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should dirty the editor");
        check(reacquired_sheet.cell_count() == 3,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should keep sparse count stable");
        check(reacquired_sheet.estimated_memory_usage() <= exact_memory_budget,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should stay within the original budget");
        check(reacquired_editor.pending_materialized_cell_count() == 3,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should expose dirty sparse count");
        check(reacquired_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A1").text_value() == "col-mb-2",
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should overwrite A1");
        check(reacquired_sheet.get_cell("A2").kind() == fastxlsx::CellValueKind::Text &&
                reacquired_sheet.get_cell("A2").text_value() == "placeholder-a2",
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should keep column tail A2");
        check(reacquired_sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number &&
                reacquired_sheet.get_cell("B1").number_value() == 1.0,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should keep non-target B1");
        check_public_state_single_data_dirty_materialized_summary(
            reacquired_editor,
            reacquired_sheet,
            0,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquired post-noop edit should not queue replacement diagnostics");

        reacquired_editor.save_as(post_noop_output_after_reacquire);
        check(!reacquired_sheet.has_pending_changes(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should clean the sheet");
        check(reacquired_editor.pending_change_count() == 1,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should keep one handoff");
        check(reacquired_editor.pending_materialized_worksheet_names().empty(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty worksheet names");
        check(reacquired_editor.pending_materialized_cell_count() == 0,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized cells");
        check(reacquired_editor.estimated_pending_materialized_memory_usage() == 0,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty materialized memory");
        check(reacquired_editor.pending_worksheet_edits().empty(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not expose dirty summaries");
        check_workbook_editor_no_replacement_diagnostics(
            reacquired_editor,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not queue replacement diagnostics");
        check(!reacquired_editor.last_edit_error().has_value(),
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should keep diagnostics clear");
        const auto post_noop_reacquire_entries =
            fastxlsx::test::read_zip_entries(post_noop_output_after_reacquire);
        const std::string post_noop_reacquire_xml =
            post_noop_reacquire_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reacquire_xml, "col-mb-2",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should persist the later overwrite");
        check_not_contains(post_noop_reacquire_xml, "col-mb-ok",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should replace the earlier A1 text");
        check_not_contains(post_noop_reacquire_xml, "set-column-values-memory-rejected",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should not leak rejected payload");
        check_contains(post_noop_reacquire_xml, "placeholder-a2",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should keep column tail A2");
        check_contains(post_noop_reacquire_xml, R"(<c r="B1"><v>1</v></c>)",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should keep non-target B1");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should leave the saved input unchanged");
        check(fastxlsx::test::read_zip_entries(output_after_reacquire) == reacquire_entries,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should leave the first no-op output stable");
        check(fastxlsx::test::read_zip_entries(noop_output_after_reacquire) == reacquire_noop_entries,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should leave the second no-op output stable");
        check(fastxlsx::test::read_zip_entries(second_noop_output_after_reacquire) == reacquire_second_noop_entries,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should leave the repeated no-op output stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            "set_column_values memory-budget recovery strict-options reacquired post-noop save should leave the source package unchanged");
        check_reopened_clean_sheet_output(post_noop_output_after_reacquire, "Data",
            "set_column_values memory-budget recovery strict-options reacquired post-noop save",
            inspect_set_column_values_memory_budget_post_noop_output);
    }
}

void test_public_worksheet_editor_set_cell_values_preserves_styles_and_order()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-second-noop-output.xlsx");

    fastxlsx::StyleId non_default_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter styled_sheet = writer.add_worksheet("Styled");
        styled_sheet.append_row({
            fastxlsx::CellView::number(1.0).with_style(non_default_style),
            fastxlsx::CellView::text("styled-tail"),
        });
        fastxlsx::WorksheetWriter untouched_sheet = writer.add_worksheet("Untouched");
        untouched_sheet.append_row({fastxlsx::CellView::text("keep-me")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Styled");

    sheet.set_cell_values({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("styled-batch-first")},
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::number(2.5)},
        {fastxlsx::WorksheetCellReference {1, 3},
            fastxlsx::CellValue::text("batch-first")},
        {fastxlsx::WorksheetCellReference {1, 3},
            fastxlsx::CellValue::formula("A1")},
        {fastxlsx::WorksheetCellReference {2, 4},
            fastxlsx::CellValue::boolean(true)},
    });

    check(sheet.cell_count() == 4,
        "set_cell_values should preserve sparse count while inserting missing cells");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "set_cell_values should expand sparse bounds for inserted cells");
    const fastxlsx::CellValue styled_a1 = sheet.get_cell("A1");
    check(styled_a1.kind() == fastxlsx::CellValueKind::Number &&
            styled_a1.number_value() == 2.5 &&
            styled_a1.has_style() &&
            styled_a1.style_id().value() == non_default_style.value(),
        "set_cell_values should preserve source style ids on duplicate overwritten targets");
    check(sheet.get_cell("B1").text_value() == "styled-tail",
        "set_cell_values should leave non-target source cells untouched");
    const fastxlsx::CellValue duplicate_c1 = sheet.get_cell("C1");
    check(duplicate_c1.kind() == fastxlsx::CellValueKind::Formula &&
            duplicate_c1.text_value() == "A1" &&
            !duplicate_c1.has_style(),
        "set_cell_values should apply duplicate coordinates in input order");
    const fastxlsx::CellValue inserted_d2 = sheet.get_cell("D2");
    check(inserted_d2.kind() == fastxlsx::CellValueKind::Boolean &&
            inserted_d2.boolean_value() &&
            !inserted_d2.has_style(),
        "set_cell_values should insert missing cells without source style ids");
    check_public_state_single_named_dirty_materialized_summary(
        editor, sheet, "Styled", 0, "set_cell_values styled dirty summary");
    check(!editor.last_edit_error().has_value(),
        "successful set_cell_values should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled set_cell_values save should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "styled set_cell_values save should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "set_cell_values should refresh the dirty worksheet dimension");
    const std::string styled_number =
        R"(<c r="A1" s=")" + std::to_string(non_default_style.value())
        + R"("><v>2.5</v></c>)";
    check_contains(worksheet_xml, styled_number,
        "set_cell_values should persist value-only edits with the source style id");
    check_not_contains(worksheet_xml, "styled-batch-first",
        "set_cell_values should omit earlier duplicate styled-target payloads");
    check_contains(worksheet_xml, "styled-tail",
        "set_cell_values should persist untouched source cells");
    check_contains(worksheet_xml, R"(<c r="C1"><f>A1</f></c>)",
        "set_cell_values should persist the later duplicate-coordinate formula");
    check_contains(worksheet_xml, R"(<c r="D2" t="b"><v>1</v></c>)",
        "set_cell_values should persist inserted boolean cells");
    check_not_contains(worksheet_xml, "batch-first",
        "set_cell_values should omit earlier duplicate-coordinate payloads");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "set_cell_values should preserve untouched worksheets");
    const auto inspect_styled_set_cell_values_output =
        [non_default_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "styled set_cell_values reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "styled set_cell_values reopened output should keep sparse bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 2.5 &&
                    reopened_a1.has_style() &&
                    reopened_a1.style_id().value() == non_default_style.value(),
                "styled set_cell_values reopened output should preserve source style id");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "styled-tail",
                "styled set_cell_values reopened output should keep untouched source cells");
            const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
            check(reopened_c1.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c1.text_value() == "A1" &&
                    !reopened_c1.has_style(),
                "styled set_cell_values reopened output should keep later duplicate formula");
            const fastxlsx::CellValue reopened_d2 = reopened_sheet.get_cell("D2");
            check(reopened_d2.kind() == fastxlsx::CellValueKind::Boolean &&
                    reopened_d2.boolean_value() &&
                    !reopened_d2.has_style(),
                "styled set_cell_values reopened output should keep inserted boolean");
        };
    const auto check_styled_set_cell_values_saved_snapshot =
        [&](std::size_t expected_pending_count, std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 4,
                prefix + " should keep the represented sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 4,
                prefix + " should expose the four represented records");
            if (cells.size() == 4) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 1 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
                        cells[0].value.number_value() == 2.5 &&
                        cells[0].value.has_style() &&
                        cells[0].value.style_id().value() == non_default_style.value(),
                    prefix + " should keep styled A1 first");
                check(cells[1].reference.row == 1 &&
                        cells[1].reference.column == 2 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[1].value.text_value() == "styled-tail" &&
                        !cells[1].value.has_style(),
                    prefix + " should keep unstyled source B1 second");
                check(cells[2].reference.row == 1 &&
                        cells[2].reference.column == 3 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                        cells[2].value.text_value() == "A1" &&
                        !cells[2].value.has_style(),
                    prefix + " should keep later-wins C1 formula third");
                check(cells[3].reference.row == 2 &&
                        cells[3].reference.column == 4 &&
                        cells[3].value.kind() == fastxlsx::CellValueKind::Boolean &&
                        cells[3].value.boolean_value() &&
                        !cells[3].value.has_style(),
                    prefix + " should keep inserted D2 boolean last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 3 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 1 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    row_one[0].value.number_value() == 2.5 &&
                    row_one[0].value.has_style() &&
                    row_one[0].value.style_id().value() == non_default_style.value() &&
                    row_one[1].reference.row == 1 &&
                    row_one[1].reference.column == 2 &&
                    row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_one[1].value.text_value() == "styled-tail" &&
                    !row_one[1].value.has_style() &&
                    row_one[2].reference.row == 1 &&
                    row_one[2].reference.column == 3 &&
                    row_one[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    row_one[2].value.text_value() == "A1" &&
                    !row_one[2].value.has_style(),
                prefix + " should keep row-one styled number, source text, and formula");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 4 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Boolean &&
                    row_two[0].value.boolean_value() &&
                    !row_two[0].value.has_style(),
                prefix + " should keep row-two inserted boolean");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 1 &&
                    column_one[0].reference.row == 1 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Number &&
                    column_one[0].value.number_value() == 2.5 &&
                    column_one[0].value.has_style() &&
                    column_one[0].value.style_id().value() == non_default_style.value(),
                prefix + " should keep column-one styled A1");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_two[0].value.text_value() == "styled-tail" &&
                    !column_two[0].value.has_style(),
                prefix + " should keep column-two source B1");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
                sheet.column_cells(3);
            check(column_three.size() == 1 &&
                    column_three[0].reference.row == 1 &&
                    column_three[0].reference.column == 3 &&
                    column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    column_three[0].value.text_value() == "A1" &&
                    !column_three[0].value.has_style(),
                prefix + " should keep column-three formula C1");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 2 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Boolean &&
                    column_four[0].value.boolean_value() &&
                    !column_four[0].value.has_style(),
                prefix + " should keep column-four boolean D2");

            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
                prefix + " should keep saved sparse bounds");
            check(!sheet.has_pending_changes(),
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
        };
    check_reopened_clean_sheet_output(output, "Styled", "styled set_cell_values",
        inspect_styled_set_cell_values_output);
    const std::size_t pending_count_after_save = editor.pending_change_count();

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "styled set_cell_values no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "styled set_cell_values no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled set_cell_values no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled set_cell_values no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "styled set_cell_values no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled set_cell_values no-op save should keep diagnostics clear");
    check_styled_set_cell_values_saved_snapshot(
        pending_count_after_save, "styled set_cell_values saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "styled set_cell_values no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "styled set_cell_values no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "styled set_cell_values no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled set_cell_values no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "Styled", "styled set_cell_values no-op save",
        inspect_styled_set_cell_values_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "styled set_cell_values second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == pending_count_after_save,
        "styled set_cell_values second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "styled set_cell_values second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "styled set_cell_values second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "styled set_cell_values second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "styled set_cell_values second no-op save should keep diagnostics clear");
    check_styled_set_cell_values_saved_snapshot(
        pending_count_after_save, "styled set_cell_values second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "styled set_cell_values second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "styled set_cell_values second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "styled set_cell_values second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "styled set_cell_values second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "styled set_cell_values second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Styled", "styled set_cell_values second no-op save",
        inspect_styled_set_cell_values_output);
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_set_row_values_preserves_styles_and_tail();
        test_public_worksheet_editor_set_row_values_accepts_default_style_id_as_style_preserving_prefix();
        test_public_worksheet_editor_set_row_values_style_rejection_preserves_dirty_session();
        test_public_worksheet_editor_set_column_values_accepts_default_style_id_as_style_preserving_prefix();
        test_public_worksheet_editor_set_column_values_style_rejection_preserves_dirty_session();
        test_public_worksheet_editor_set_column_values_noop_invalid_and_budget();
        test_public_worksheet_editor_set_cell_values_preserves_styles_and_order();
        std::cout << "WorkbookEditor public-state value write tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state value write test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
