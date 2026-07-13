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

void test_public_worksheet_editor_initializer_list_batch_overloads()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-init-list-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-init-list-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-init-list-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-init-list-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-init-list-post-noop-output.xlsx");
    const std::filesystem::path style_id_source = artifact(
        "fastxlsx-workbook-editor-public-worksheet-init-list-style-id-source.xlsx");
    const std::filesystem::path set_cells_style_reject_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cells-style-reject-output.xlsx");
    const std::filesystem::path set_cells_style_reject_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cells-style-reject-noop-output.xlsx");
    const std::filesystem::path set_cell_values_style_reject_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-reject-output.xlsx");
    const std::filesystem::path set_cell_values_style_reject_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-set-cell-values-style-reject-noop-output.xlsx");

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
    auto check_batch_style_rejection_noop =
        [&](std::string_view api_name, const std::filesystem::path& reject_output,
            const std::filesystem::path& reject_noop_output,
            const std::function<void(fastxlsx::WorksheetEditor&)>& reject_action) {
            const std::string prefix(api_name);
            fastxlsx::WorkbookEditor reject_editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor reject_sheet = reject_editor.worksheet("Data");

            bool failed = false;
            try {
                reject_action(reject_sheet);
            } catch (const fastxlsx::FastXlsxError& error) {
                failed = true;
                check_contains(error.what(), "StyleId",
                    prefix + " style rejection should expose the unsupported StyleId boundary");
            }
            check(failed,
                prefix + " should reject caller-supplied non-default StyleId values");
            check(reject_editor.last_edit_error().has_value() &&
                    reject_editor.last_edit_error()->find(prefix) != std::string::npos,
                prefix + " style rejection should retain the public edit diagnostic");
            check_workbook_editor_public_no_pending_state(
                reject_editor, prefix + " style rejection");
            check(!reject_sheet.has_pending_changes(),
                prefix + " style rejection should keep the materialized sheet clean");
            check(reject_sheet.get_cell("A1").text_value() == "placeholder-a1",
                prefix + " style rejection should preserve source A1");
            check(reject_sheet.get_cell("B1").number_value() == 1.0,
                prefix + " style rejection should preserve source B1");
            check(reject_sheet.get_cell("A2").text_value() == "placeholder-a2",
                prefix + " style rejection should preserve source A2");
            check_workbook_editor_no_replacement_diagnostics(
                reject_editor,
                prefix + " style rejection should not queue replacement diagnostics");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_save =
                workbook_editor_public_catalog_snapshot(reject_editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
                workbook_editor_public_save_state_snapshot(reject_editor);
            reject_editor.save_as(reject_output);
            check_workbook_editor_public_save_state_preserved(
                reject_editor, save_state_before_save,
                prefix + " style rejection save");
            check_workbook_editor_public_catalog_preserved(
                reject_editor, catalog_before_save,
                prefix + " style rejection save");
            check_workbook_editor_public_no_pending_state(
                reject_editor, prefix + " style rejection save");
            check(!reject_sheet.has_pending_changes(),
                prefix + " style rejection save should keep the materialized sheet clean");
            check_workbook_editor_no_replacement_diagnostics(
                reject_editor,
                prefix + " style rejection save should not queue replacement diagnostics");
            const auto reject_output_entries =
                fastxlsx::test::read_zip_entries(reject_output);
            check(reject_output_entries == source_entries,
                prefix + " style rejection save should copy source entries");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " style rejection save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(
                reject_output, prefix + " style rejection save");

            const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
                workbook_editor_public_catalog_snapshot(reject_editor);
            const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
                workbook_editor_public_save_state_snapshot(reject_editor);
            reject_editor.save_as(reject_noop_output);
            check_workbook_editor_public_save_state_preserved(
                reject_editor, save_state_before_noop,
                prefix + " style rejection noop save");
            check_workbook_editor_public_catalog_preserved(
                reject_editor, catalog_before_noop,
                prefix + " style rejection noop save");
            check_workbook_editor_public_no_pending_state(
                reject_editor, prefix + " style rejection noop save");
            check(!reject_sheet.has_pending_changes(),
                prefix + " style rejection noop save should keep the materialized sheet clean");
            check_workbook_editor_no_replacement_diagnostics(
                reject_editor,
                prefix + " style rejection noop save should not queue replacement diagnostics");
            const auto reject_noop_entries =
                fastxlsx::test::read_zip_entries(reject_noop_output);
            check(reject_noop_entries == source_entries,
                prefix + " style rejection noop save should still copy source entries");
            check(reject_noop_entries == reject_output_entries,
                prefix + " style rejection noop output should match the first output");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                prefix + " style rejection noop save should leave the source package unchanged");
            check_reopened_default_data_sheet_output(
                reject_noop_output, prefix + " style rejection noop save");
        };

    check_batch_style_rejection_noop("set_cells()", set_cells_style_reject_output,
        set_cells_style_reject_noop_output,
        [non_default_style](fastxlsx::WorksheetEditor& reject_sheet) {
            reject_sheet.set_cells({
                {fastxlsx::WorksheetCellReference {1, 1},
                    fastxlsx::CellValue::text("batch-styled-rejected")
                        .with_style(non_default_style)},
            });
        });

    check_batch_style_rejection_noop("set_cell_values()",
        set_cell_values_style_reject_output, set_cell_values_style_reject_noop_output,
        [non_default_style](fastxlsx::WorksheetEditor& reject_sheet) {
            reject_sheet.set_cell_values({
                {fastxlsx::WorksheetCellReference {1, 2},
                    fastxlsx::CellValue::number(77.0).with_style(non_default_style)},
            });
        });

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cells({
        {fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::CellValue::text("init-list-full-replace")},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::text("init-list-first")},
        {fastxlsx::WorksheetCellReference {3, 3},
            fastxlsx::CellValue::formula("A1+B1")},
    });
    check(sheet.get_cell("A1").text_value() == "init-list-full-replace",
        "initializer-list set_cells should replace existing sparse cells");
    const fastxlsx::CellValue duplicate_full_update = sheet.get_cell("C3");
    check(duplicate_full_update.kind() == fastxlsx::CellValueKind::Formula
            && duplicate_full_update.text_value() == "A1+B1",
        "initializer-list set_cells should keep duplicate-coordinate later-wins semantics");

    sheet.set_cell_values({
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::number(42.0)},
        {fastxlsx::WorksheetCellReference {4, 4},
            fastxlsx::CellValue::boolean(true)},
    });
    check(sheet.get_cell("B1").number_value() == 42.0,
        "initializer-list set_cell_values should update existing sparse cells");
    check(sheet.get_cell("D4").boolean_value(),
        "initializer-list set_cell_values should insert missing sparse cells");

    sheet.clear_cell_values({
        fastxlsx::WorksheetCellReference {1, 2},
        fastxlsx::WorksheetCellReference {6, 6},
    });
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "initializer-list clear_cell_values should clear represented cells");
    check(!sheet.try_cell("F6").has_value(),
        "initializer-list clear_cell_values should not synthesize missing cells");

    sheet.erase_cells({
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {3, 3},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {8, 8},
    });
    check(!sheet.try_cell("A1").has_value(),
        "initializer-list erase_cells should remove represented cells");
    check(!sheet.try_cell("C3").has_value(),
        "initializer-list erase_cells should remove represented duplicate target");
    check(!sheet.try_cell("H8").has_value(),
        "initializer-list erase_cells should not synthesize missing cells");
    check(!editor.last_edit_error().has_value(),
        "successful initializer-list batch overloads should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "initializer-list batch save should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="B1"/>)",
        "initializer-list clear_cell_values should persist explicit blank cells");
    check_contains(worksheet_xml, R"(<c r="D4" t="b"><v>1</v></c>)",
        "initializer-list set_cell_values should persist inserted boolean cells");
    check_not_contains(worksheet_xml, "init-list-full-replace",
        "initializer-list erase_cells should omit erased full replacement text");
    check_not_contains(worksheet_xml, "A1+B1",
        "initializer-list erase_cells should omit erased formula text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "initializer-list batch overloads should preserve untouched worksheets");
    const auto inspect_initializer_list_batch_output =
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "initializer-list reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                "initializer-list reopened output should expose final sparse bounds");
            check(!reopened_sheet.try_cell("A1").has_value(),
                "initializer-list reopened output should keep erased A1 absent");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Blank,
                "initializer-list reopened output should read explicit B1 blank");
            const fastxlsx::CellValue reopened_a2 = reopened_sheet.get_cell("A2");
            check(reopened_a2.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_a2.text_value() == "placeholder-a2",
                "initializer-list reopened output should keep source-backed A2");
            check(!reopened_sheet.try_cell("C3").has_value(),
                "initializer-list reopened output should keep erased C3 absent");
            const fastxlsx::CellValue reopened_d4 = reopened_sheet.get_cell("D4");
            check(reopened_d4.kind() == fastxlsx::CellValueKind::Boolean &&
                    reopened_d4.boolean_value(),
                "initializer-list reopened output should read inserted D4 boolean");
            check(!reopened_sheet.try_cell("F6").has_value(),
                "initializer-list reopened output should not synthesize clear-only F6");
            check(!reopened_sheet.try_cell("H8").has_value(),
                "initializer-list reopened output should not synthesize erase-only H8");
        };
    check_reopened_clean_sheet_output(output, "Data", "initializer-list batch overloads",
        inspect_initializer_list_batch_output);
    const std::size_t pending_count_after_save = editor.pending_change_count();
    const auto check_initializer_list_batch_saved_snapshot =
        [&](std::string_view scenario) {
            const std::string prefix(scenario);

            check(sheet.cell_count() == 3,
                prefix + " should keep the saved sparse count");
            const std::vector<fastxlsx::WorksheetCellSnapshot> cells =
                sheet.sparse_cells();
            check(cells.size() == 3,
                prefix + " should expose all saved sparse records");
            if (cells.size() == 3) {
                check(cells[0].reference.row == 1 &&
                        cells[0].reference.column == 2 &&
                        cells[0].value.kind() == fastxlsx::CellValueKind::Blank,
                    prefix + " should keep explicit B1 blank first");
                check(cells[1].reference.row == 2 &&
                        cells[1].reference.column == 1 &&
                        cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
                        cells[1].value.text_value() == "placeholder-a2",
                    prefix + " should keep source-backed A2 second");
                check(cells[2].reference.row == 4 &&
                        cells[2].reference.column == 4 &&
                        cells[2].value.kind() == fastxlsx::CellValueKind::Boolean &&
                        cells[2].value.boolean_value(),
                    prefix + " should keep inserted D4 boolean last");
            }

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
                sheet.row_cells(1);
            check(row_one.size() == 1 &&
                    row_one[0].reference.row == 1 &&
                    row_one[0].reference.column == 2 &&
                    row_one[0].value.kind() == fastxlsx::CellValueKind::Blank,
                prefix + " should expose row-one explicit blank");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
                sheet.row_cells(2);
            check(row_two.size() == 1 &&
                    row_two[0].reference.row == 2 &&
                    row_two[0].reference.column == 1 &&
                    row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    row_two[0].value.text_value() == "placeholder-a2",
                prefix + " should expose row-two source-backed cell");
            check(sheet.row_cells(3).empty(),
                prefix + " should keep erased C3 absent from row snapshots");

            const std::vector<fastxlsx::WorksheetCellSnapshot> row_four =
                sheet.row_cells(4);
            check(row_four.size() == 1 &&
                    row_four[0].reference.row == 4 &&
                    row_four[0].reference.column == 4 &&
                    row_four[0].value.kind() == fastxlsx::CellValueKind::Boolean &&
                    row_four[0].value.boolean_value(),
                prefix + " should expose row-four inserted boolean");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_one =
                sheet.column_cells(1);
            check(column_one.size() == 1 &&
                    column_one[0].reference.row == 2 &&
                    column_one[0].reference.column == 1 &&
                    column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    column_one[0].value.text_value() == "placeholder-a2",
                prefix + " should expose column-one source-backed cell");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_two =
                sheet.column_cells(2);
            check(column_two.size() == 1 &&
                    column_two[0].reference.row == 1 &&
                    column_two[0].reference.column == 2 &&
                    column_two[0].value.kind() == fastxlsx::CellValueKind::Blank,
                prefix + " should expose column-two explicit blank");
            check(sheet.column_cells(3).empty(),
                prefix + " should keep erased C3 absent from column snapshots");

            const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
                sheet.column_cells(4);
            check(column_four.size() == 1 &&
                    column_four[0].reference.row == 4 &&
                    column_four[0].reference.column == 4 &&
                    column_four[0].value.kind() == fastxlsx::CellValueKind::Boolean &&
                    column_four[0].value.boolean_value(),
                prefix + " should expose column-four inserted boolean");

            check_cell_range_equals(sheet.used_range(), 1, 1, 4, 4,
                prefix + " should keep saved sparse bounds");
            check(!sheet.try_cell("A1").has_value() &&
                    !sheet.try_cell("C3").has_value(),
                prefix + " should keep erased cells absent");
            check(!sheet.has_pending_changes(),
                prefix + " should keep the materialized handle clean");
            check(editor.pending_change_count() == pending_count_after_save,
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
    check_initializer_list_batch_saved_snapshot(
        "initializer-list batch saved handle");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "initializer-list batch no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "initializer-list batch no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "initializer-list batch no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "initializer-list batch no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "initializer-list batch no-op save should keep diagnostics clear");
    check_initializer_list_batch_saved_snapshot(
        "initializer-list batch no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "initializer-list batch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "initializer-list batch no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "initializer-list batch no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "initializer-list batch no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(noop_output, "Data", "initializer-list batch no-op save",
        inspect_initializer_list_batch_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "initializer-list batch second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "initializer-list batch second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "initializer-list batch second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "initializer-list batch second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "initializer-list batch second no-op save should keep diagnostics clear");
    check_initializer_list_batch_saved_snapshot(
        "initializer-list batch second no-op saved handle");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "initializer-list batch second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "initializer-list batch second no-op save");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "initializer-list batch second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "initializer-list batch second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "initializer-list batch second no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        second_noop_output, "Data", "initializer-list batch second no-op save",
        inspect_initializer_list_batch_output);

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("Data");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "initializer-list batch post-noop reacquire should return a clean saved session");
    check(reacquired.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank &&
            sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Blank,
        "initializer-list batch post-noop reacquire should share the saved blank state");
    check(reacquired.get_cell("D4").boolean_value() &&
            sheet.get_cell("D4").boolean_value(),
        "initializer-list batch post-noop reacquire should share inserted boolean state");

    reacquired.set_cell_values({
        {fastxlsx::WorksheetCellReference {1, 2},
            fastxlsx::CellValue::text("post-noop-init-list-b1")},
        {fastxlsx::WorksheetCellReference {5, 5},
            fastxlsx::CellValue::formula("B1")},
    });
    reacquired.clear_cell_values({
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {9, 9},
    });
    reacquired.erase_cells({
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {2, 1},
    });
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "initializer-list batch post-noop edit should dirty both shared handles");
    check(reacquired.cell_count() == 3 && sheet.cell_count() == 3,
        "initializer-list batch post-noop edit should preserve final sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 2, 5, 5,
        "initializer-list batch post-noop edit should expose shifted sparse bounds");
    check(reacquired.get_cell("B1").text_value() == "post-noop-init-list-b1" &&
            sheet.get_cell("B1").text_value() == "post-noop-init-list-b1",
        "initializer-list batch post-noop edit should update B1 through both handles");
    check(reacquired.get_cell("D4").kind() == fastxlsx::CellValueKind::Blank,
        "initializer-list batch post-noop edit should clear D4 as an explicit blank");
    const fastxlsx::CellValue post_noop_formula = reacquired.get_cell("E5");
    check(post_noop_formula.kind() == fastxlsx::CellValueKind::Formula &&
            post_noop_formula.text_value() == "B1",
        "initializer-list batch post-noop edit should insert E5 as a formula");
    check(!reacquired.try_cell("A2").has_value() && !sheet.try_cell("A2").has_value(),
        "initializer-list batch post-noop edit should erase A2 through both handles");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "initializer-list batch post-noop save should clean both shared handles");
    check(editor.pending_change_count() == 2,
        "initializer-list batch post-noop save should record the second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "initializer-list batch post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "initializer-list batch post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "initializer-list batch post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "initializer-list batch post-noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "initializer-list batch post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "initializer-list batch post-noop save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "initializer-list batch post-noop save should leave the second no-op output unchanged");
    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, R"(<dimension ref="B1:E5"/>)",
        "initializer-list batch post-noop save should persist the refreshed sparse bounds");
    check_contains(post_noop_xml,
        R"(<c r="B1" t="inlineStr"><is><t>post-noop-init-list-b1</t></is></c>)",
        "initializer-list batch post-noop save should persist the B1 value edit");
    check_contains(post_noop_xml, R"(<c r="D4"/>)",
        "initializer-list batch post-noop save should persist D4 as an explicit blank");
    check_contains(post_noop_xml, R"(<c r="E5"><f>B1</f></c>)",
        "initializer-list batch post-noop save should persist the inserted E5 formula");
    check_not_contains(post_noop_xml, R"(r="A2")",
        "initializer-list batch post-noop save should omit erased A2");
    check_reopened_clean_sheet_output(
        post_noop_output, "Data", "initializer-list batch post-noop save",
        [](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 3,
                "initializer-list batch post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 2, 5, 5,
                "initializer-list batch post-noop reopened output should expose final bounds");
            const fastxlsx::CellValue reopened_b1 = reopened_sheet.get_cell("B1");
            check(reopened_b1.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_b1.text_value() == "post-noop-init-list-b1",
                "initializer-list batch post-noop reopened output should read B1 text");
            const fastxlsx::CellValue reopened_d4 = reopened_sheet.get_cell("D4");
            check(reopened_d4.kind() == fastxlsx::CellValueKind::Blank,
                "initializer-list batch post-noop reopened output should keep D4 blank");
            const fastxlsx::CellValue reopened_e5 = reopened_sheet.get_cell("E5");
            check(reopened_e5.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_e5.text_value() == "B1",
                "initializer-list batch post-noop reopened output should read E5 formula");
            check(!reopened_sheet.try_cell("A2").has_value(),
                "initializer-list batch post-noop reopened output should keep A2 erased");
        });
}

void test_public_worksheet_editor_equal_batch_mutations_are_clean_noops()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-equal-batch-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-equal-batch-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::size_t baseline_cell_count = sheet.cell_count();
    const std::size_t baseline_memory = sheet.estimated_memory_usage();

    const auto check_clean_state = [&](std::string_view scenario) {
        const std::string prefix(scenario);
        check(!sheet.has_pending_changes()
                && !editor.has_unsaved_changes()
                && !editor.has_pending_changes()
                && editor.pending_change_count() == 0
                && editor.pending_materialized_worksheet_names().empty()
                && editor.pending_materialized_cell_count() == 0
                && editor.estimated_pending_materialized_memory_usage() == 0
                && editor.pending_worksheet_edits().empty()
                && !editor.last_edit_error().has_value()
                && sheet.cell_count() == baseline_cell_count
                && sheet.estimated_memory_usage() == baseline_memory,
            prefix + " should preserve clean editor and sparse session state");
        check(sheet.get_cell("A1").text_value() == "placeholder-a1"
                && sheet.get_cell("B1").number_value() == 1.0
                && sheet.get_cell("A2").text_value() == "placeholder-a2",
            prefix + " should preserve source-backed cell values");
    };

    sheet.set_cells({
        {{1, 1}, fastxlsx::CellValue::text("temporary-a1")},
        {{1, 1}, fastxlsx::CellValue::text("placeholder-a1")},
        {{1, 2}, fastxlsx::CellValue::number(1.0)},
    });
    check_clean_state("final-state-equal set_cells");

    sheet.set_cell_values({
        {{2, 1}, fastxlsx::CellValue::text("temporary-a2")},
        {{2, 1}, fastxlsx::CellValue::text("placeholder-a2")},
        {{1, 2}, fastxlsx::CellValue::number(1.0)},
    });
    check_clean_state("final-state-equal set_cell_values");

    sheet.set_row(1, {
        fastxlsx::CellValue::text("placeholder-a1"),
        fastxlsx::CellValue::number(1.0),
    });
    check_clean_state("final-state-equal set_row");

    sheet.set_column(1, {
        fastxlsx::CellValue::text("placeholder-a1"),
        fastxlsx::CellValue::text("placeholder-a2"),
    });
    check_clean_state("final-state-equal set_column");

    sheet.set_row_values(1, {
        fastxlsx::CellValue::text("placeholder-a1"),
        fastxlsx::CellValue::number(1.0),
    });
    check_clean_state("final-state-equal set_row_values");

    sheet.set_column_values(1, {
        fastxlsx::CellValue::text("placeholder-a1"),
        fastxlsx::CellValue::text("placeholder-a2"),
    });
    check_clean_state("final-state-equal set_column_values");

    editor.save_as(output);
    check(fastxlsx::test::read_zip_entries(output) == source_entries,
        "save after final-state-equal batch mutations should preserve source entries");
    check_reopened_default_data_sheet_output(
        output, "final-state-equal batch mutation output");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_initializer_list_batch_overloads();
        test_public_worksheet_editor_equal_batch_mutations_are_clean_noops();
        std::cout << "WorkbookEditor public-state batch edit tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public-state batch edit test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
