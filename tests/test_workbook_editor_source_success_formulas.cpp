#include "test_workbook_editor_source_success_common.hpp"

struct ReopenedFormulaOutputCell {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    fastxlsx::CellValue value;
};

bool formula_output_values_equal(
    const fastxlsx::CellValue& actual,
    const fastxlsx::CellValue& expected)
{
    if (actual.kind() != expected.kind()) {
        return false;
    }

    switch (expected.kind()) {
    case fastxlsx::CellValueKind::Blank:
        return true;
    case fastxlsx::CellValueKind::Number:
        return actual.number_value() == expected.number_value();
    case fastxlsx::CellValueKind::Boolean:
        return actual.boolean_value() == expected.boolean_value();
    case fastxlsx::CellValueKind::Text:
    case fastxlsx::CellValueKind::Formula:
    case fastxlsx::CellValueKind::Error:
        return actual.text_value() == expected.text_value();
    }

    return false;
}

bool formula_output_snapshot_matches(
    const fastxlsx::WorksheetCellSnapshot& actual,
    const ReopenedFormulaOutputCell& expected)
{
    return actual.reference.row == expected.row &&
        actual.reference.column == expected.column &&
        formula_output_values_equal(actual.value, expected.value);
}

void check_formula_row_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario,
    std::string_view view_label)
{
    std::vector<std::uint32_t> checked_rows;
    const std::string prefix(scenario);
    const std::string view(view_label);

    for (const ReopenedFormulaOutputCell& expected : expected_cells) {
        bool already_checked = false;
        for (const std::uint32_t checked_row : checked_rows) {
            if (checked_row == expected.row) {
                already_checked = true;
                break;
            }
        }
        if (already_checked) {
            continue;
        }
        checked_rows.push_back(expected.row);

        std::size_t expected_count = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.row == expected.row) {
                ++expected_count;
            }
        }

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_cells =
            reopened_sheet.row_cells(expected.row);
        check(row_cells.size() == expected_count,
            prefix + " " + view + " row_cells should expose the expected row count");
        if (row_cells.size() != expected_count) {
            continue;
        }

        std::size_t index = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.row != expected.row) {
                continue;
            }
            check(formula_output_snapshot_matches(row_cells[index], candidate),
                prefix + " " + view + " row_cells should preserve row-major values");
            ++index;
        }
    }
}

void check_formula_column_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario,
    std::string_view view_label)
{
    std::vector<std::uint32_t> checked_columns;
    const std::string prefix(scenario);
    const std::string view(view_label);

    for (const ReopenedFormulaOutputCell& expected : expected_cells) {
        bool already_checked = false;
        for (const std::uint32_t checked_column : checked_columns) {
            if (checked_column == expected.column) {
                already_checked = true;
                break;
            }
        }
        if (already_checked) {
            continue;
        }
        checked_columns.push_back(expected.column);

        std::size_t expected_count = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.column == expected.column) {
                ++expected_count;
            }
        }

        const std::vector<fastxlsx::WorksheetCellSnapshot> column_cells =
            reopened_sheet.column_cells(expected.column);
        check(column_cells.size() == expected_count,
            prefix + " " + view + " column_cells should expose the expected column count");
        if (column_cells.size() != expected_count) {
            continue;
        }

        std::size_t index = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.column != expected.column) {
                continue;
            }
            check(formula_output_snapshot_matches(column_cells[index], candidate),
                prefix + " " + view + " column_cells should preserve row-major values");
            ++index;
        }
    }
}

void check_reopened_formula_dirty_output(
    const std::filesystem::path& output,
    const fastxlsx::CellRange& expected_range,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " fresh reopen should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " fresh reopen should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " fresh reopen should not expose dirty materialized memory");
    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen should materialize a clean worksheet");
    check(reopened_sheet.cell_count() == expected_cells.size(),
        prefix + " fresh reopen should preserve the expected sparse cell count");

    const std::optional<fastxlsx::CellRange> actual_range = reopened_sheet.used_range();
    check(actual_range.has_value() &&
            actual_range->first_row == expected_range.first_row &&
            actual_range->first_column == expected_range.first_column &&
            actual_range->last_row == expected_range.last_row &&
            actual_range->last_column == expected_range.last_column,
        prefix + " fresh reopen should expose the expected used range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> actual_cells =
        reopened_sheet.sparse_cells();
    check(actual_cells.size() == expected_cells.size(),
        prefix + " fresh reopen sparse_cells should expose the expected cell count");
    if (actual_cells.size() == expected_cells.size()) {
        for (std::size_t index = 0; index < expected_cells.size(); ++index) {
            const ReopenedFormulaOutputCell& expected = expected_cells[index];
            check(actual_cells[index].reference.row == expected.row &&
                    actual_cells[index].reference.column == expected.column &&
                    formula_output_values_equal(actual_cells[index].value, expected.value),
                prefix + " fresh reopen sparse_cells should preserve row-major values");
        }
    }

    check_formula_row_snapshots(reopened_sheet, expected_cells, scenario, "fresh reopen");
    check_formula_column_snapshots(reopened_sheet, expected_cells, scenario, "fresh reopen");

    for (const ReopenedFormulaOutputCell& expected : expected_cells) {
        const fastxlsx::CellValue actual =
            reopened_sheet.get_cell(expected.row, expected.column);
        check(formula_output_values_equal(actual, expected.value),
            prefix + " fresh reopen should read each expected cell directly");
    }

    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen reads should leave the worksheet clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen reads");
}

void check_live_formula_shifted_cells(
    fastxlsx::WorksheetEditor& sheet,
    const fastxlsx::CellRange& expected_range,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == expected_cells.size(),
        prefix + " live view should expose the expected sparse cell count");

    const std::optional<fastxlsx::CellRange> actual_range = sheet.used_range();
    check(actual_range.has_value() &&
            actual_range->first_row == expected_range.first_row &&
            actual_range->first_column == expected_range.first_column &&
            actual_range->last_row == expected_range.last_row &&
            actual_range->last_column == expected_range.last_column,
        prefix + " live view should expose the expected used range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> actual_cells =
        sheet.sparse_cells();
    check(actual_cells.size() == expected_cells.size(),
        prefix + " live sparse_cells should expose the expected cell count");
    if (actual_cells.size() == expected_cells.size()) {
        for (std::size_t index = 0; index < expected_cells.size(); ++index) {
            check(formula_output_snapshot_matches(actual_cells[index], expected_cells[index]),
                prefix + " live sparse_cells should preserve row-major values");
        }
    }

    check_formula_row_snapshots(sheet, expected_cells, scenario, "live");
    check_formula_column_snapshots(sheet, expected_cells, scenario, "live");

    for (const ReopenedFormulaOutputCell& expected : expected_cells) {
        const fastxlsx::CellValue actual =
            sheet.get_cell(expected.row, expected.column);
        check(formula_output_values_equal(actual, expected.value),
            prefix + " live view should read each expected cell directly");
    }
}

void test_public_worksheet_editor_materializes_source_formulas()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-formula-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-formula-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-formula-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-formula-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::number(3.0),
            fastxlsx::CellView::formula("SUM(A1:B1)&\"<ok>\"")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& source_worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f><v>999</v></c>)");
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 2.0,
        "WorksheetEditor should materialize source formula sibling number A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number
            && b1->number_value() == 3.0,
        "WorksheetEditor should materialize source formula sibling number B1");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "SUM(A1:B1)&\"<ok>\"",
        "WorksheetEditor should materialize source formula text and ignore cached values");
    check(!sheet.has_pending_changes(),
        "source formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source formula read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "source formula read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source formula no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source formula no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source formula no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == entries,
        "source formula no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == entries,
        "source formula no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::number(2.0)},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {1, 3, fastxlsx::CellValue::formula("SUM(A1:B1)&\"<ok>\"")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 3},
        noop_cells,
        "source formula no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("formula-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="C1"><f>SUM(A1:B1)&amp;"&lt;ok&gt;"</f></c>)",
        "flushed WorksheetEditor source formula should preserve formula text");
    check(worksheet_xml.find("<v>999</v>") == std::string::npos,
        "flushed WorksheetEditor source formula should not preserve stale cached values");
    check_contains(worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>formula-new-inline</t></is></c>)",
        "flushed WorksheetEditor source formula sheet should include later text edits");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(2.0)},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {1, 3, fastxlsx::CellValue::formula("SUM(A1:B1)&\"<ok>\"")},
        {2, 4, fastxlsx::CellValue::text("formula-new-inline")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source formula dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source formula post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source formula post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == entries,
        "source formula post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source formula post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source formula post-dirty no-op output");

    sheet.set_cell("E3", fastxlsx::CellValue::formula("C1&\"<dirty>\"&A1"));
    check(sheet.has_pending_changes(),
        "source formula post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source formula post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source formula post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="E3"><f>C1&amp;"&lt;dirty&gt;"&amp;A1</f></c>)",
        "source formula post-noop reuse save should include the later escaped formula edit");
    check_not_contains(post_noop_reuse_xml, "<v>999</v>",
        "source formula post-noop reuse save should keep stale cached values omitted");
    check(fastxlsx::test::read_zip_entries(source) == entries,
        "source formula post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source formula post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "source formula post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source formula post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(2.0)},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {1, 3, fastxlsx::CellValue::formula("SUM(A1:B1)&\"<ok>\"")},
        {2, 4, fastxlsx::CellValue::text("formula-new-inline")},
        {3, 5, fastxlsx::CellValue::formula("C1&\"<dirty>\"&A1")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source formula post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source formula post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == entries,
        "source formula post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source formula post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_source_error_cells()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-source-error-cells-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-error-cells-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-error-cells-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-error-cells-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-error-cells-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-error-cells-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="e"><v>#VALUE!</v></c>)"
        R"(<c r="B1" t="e"><v>#DIV/0!</v></c>)"
        R"(<c r="C1" t="e"><v>#N/A</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Error
            && a1->text_value() == "#VALUE!",
        "WorksheetEditor should materialize source #VALUE! error cells");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Error
            && b1->text_value() == "#DIV/0!",
        "WorksheetEditor should materialize source #DIV/0! error cells");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Error
            && c1->text_value() == "#N/A",
        "WorksheetEditor should materialize source #N/A error cells");
    check(!sheet.has_pending_changes(),
        "source error cell read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source error cell read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "source error cell read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source error cell no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source error cell no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source error cell no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "source error cell no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source error cell no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::error("#VALUE!")},
        {1, 2, fastxlsx::CellValue::error("#DIV/0!")},
        {1, 3, fastxlsx::CellValue::error("#N/A")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 3},
        noop_cells,
        "source error cell no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("after-source-error"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1" t="e"><v>#VALUE!</v></c>)",
        "dirty projection should write materialized #VALUE! as t=e");
    check_contains(output_worksheet_xml, R"(<c r="B1" t="e"><v>#DIV/0!</v></c>)",
        "dirty projection should write materialized #DIV/0! as t=e");
    check_contains(output_worksheet_xml, R"(<c r="C1" t="e"><v>#N/A</v></c>)",
        "dirty projection should write materialized #N/A as t=e");
    check_contains(output_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>after-source-error</t></is></c>)",
        "dirty projection should include edits beside source error cells");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty source error projection should preserve untouched sheets");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::error("#VALUE!")},
        {1, 2, fastxlsx::CellValue::error("#DIV/0!")},
        {1, 3, fastxlsx::CellValue::error("#N/A")},
        {2, 4, fastxlsx::CellValue::text("after-source-error")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source error cell dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source error cell post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source error cell post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source error cell post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source error cell post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source error cell post-dirty no-op output");

    sheet.set_cell("E3", fastxlsx::CellValue::error("#NULL!"));
    check(sheet.has_pending_changes(),
        "source error cell post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source error cell post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source error cell post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<c r="E3" t="e"><v>#NULL!</v></c>)",
        "source error cell post-noop reuse save should include the later error cell");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source error cell post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source error cell post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "source error cell post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source error cell post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::error("#VALUE!")},
        {1, 2, fastxlsx::CellValue::error("#DIV/0!")},
        {1, 3, fastxlsx::CellValue::error("#N/A")},
        {2, 4, fastxlsx::CellValue::text("after-source-error")},
        {3, 5, fastxlsx::CellValue::error("#NULL!")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source error cell post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source error cell post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source error cell post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source error cell post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source error cell post-noop reuse no-op output");
}

void test_public_worksheet_editor_structural_shift_source_error_cells()
{
    const auto write_error_shift_source =
        [](std::string_view name, std::string_view sheet_data) {
            const std::filesystem::path source = write_two_sheet_source(name);
            const std::string worksheet_xml =
                std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
                + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
                + std::string(sheet_data)
                + R"(</worksheet>)";
            rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
            return source;
        };

    const auto run_error_shift_case =
        [&](std::string_view artifact_suffix,
            std::string_view scenario,
            const std::filesystem::path& source,
            std::size_t source_cell_count,
            auto mutate,
            const fastxlsx::CellRange& expected_range,
            std::span<const ReopenedFormulaOutputCell> expected_cells,
            std::initializer_list<std::string_view> expected_xml_fragments,
            std::initializer_list<std::string_view> omitted_tokens,
            const fastxlsx::CellRange& reopened_range,
            const ReopenedFormulaOutputCell& reopened_edit,
            std::string_view reopened_edit_xml_fragment) {
            const std::string artifact_suffix_text(artifact_suffix);
            const std::string scenario_text(scenario);
            const std::filesystem::path output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-source-error-cell-"
                + artifact_suffix_text + "-output.xlsx");
            const std::filesystem::path noop_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-source-error-cell-"
                + artifact_suffix_text + "-noop-output.xlsx");
            const std::filesystem::path reopened_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-source-error-cell-"
                + artifact_suffix_text + "-reopened-output.xlsx");
            const std::filesystem::path reopened_noop_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-source-error-cell-"
                + artifact_suffix_text + "-reopened-noop-output.xlsx");
            const auto source_entries = fastxlsx::test::read_zip_entries(source);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());
            check(sheet.cell_count() == source_cell_count,
                scenario_text + " setup should materialize source error cells");
            check(!sheet.has_pending_changes(),
                scenario_text + " setup should start clean");
            check(!editor.has_pending_changes(),
                scenario_text + " setup should not dirty WorkbookEditor");

            mutate(sheet);

            check(sheet.has_pending_changes(),
                scenario_text + " should dirty Data");
            check(editor.has_pending_changes(),
                scenario_text + " should dirty WorkbookEditor");
            check(sheet.cell_count() == expected_cells.size(),
                scenario_text + " should expose the shifted error cell count");
            check_live_formula_shifted_cells(
                sheet, expected_range, expected_cells, scenario);
            check(sheet.has_pending_changes(),
                scenario_text + " live shifted reads should keep Data dirty");
            check(editor.has_pending_changes(),
                scenario_text + " live shifted reads should keep WorkbookEditor dirty");

            editor.save_as(output);
            check(!sheet.has_pending_changes(),
                scenario_text + " save should keep Data clean");
            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            const std::string& output_worksheet_xml =
                output_entries.at("xl/worksheets/sheet1.xml");
            for (std::string_view expected_xml_fragment : expected_xml_fragments) {
                check_contains(output_worksheet_xml, expected_xml_fragment,
                    scenario_text + " save should write shifted error cells as t=e");
            }
            for (std::string_view omitted_token : omitted_tokens) {
                check_not_contains(output_worksheet_xml, omitted_token,
                    scenario_text + " save should omit deleted source error cells");
            }
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " save should not mutate the source package");
            check(output_entries.at("xl/worksheets/sheet2.xml")
                    == source_entries.at("xl/worksheets/sheet2.xml"),
                scenario_text + " save should preserve untouched worksheets");
            check_reopened_formula_dirty_output(
                output, expected_range, expected_cells, scenario);

            editor.save_as(noop_output);
            check(!sheet.has_pending_changes(),
                scenario_text + " no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " no-op save should not mutate the source package");
            check_reopened_formula_dirty_output(
                noop_output, expected_range, expected_cells, scenario_text + " no-op output");

            fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
            fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh reopen should start clean");
            reopened_sheet.set_cell(
                reopened_edit.row, reopened_edit.column, reopened_edit.value);
            reopened_editor.save_as(reopened_output);
            const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
            const std::string& reopened_worksheet_xml =
                reopened_entries.at("xl/worksheets/sheet1.xml");
            for (std::string_view expected_xml_fragment : expected_xml_fragments) {
                check_contains(reopened_worksheet_xml, expected_xml_fragment,
                    scenario_text + " fresh-reopen save should keep shifted error cells");
            }
            check_contains(reopened_worksheet_xml, reopened_edit_xml_fragment,
                scenario_text + " fresh-reopen save should include the later error cell");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh-reopen save should not mutate its saved input");

            std::vector<ReopenedFormulaOutputCell> reopened_expected(
                expected_cells.begin(), expected_cells.end());
            reopened_expected.push_back(reopened_edit);
            check_reopened_formula_dirty_output(
                reopened_output,
                reopened_range,
                std::span<const ReopenedFormulaOutputCell>(
                    reopened_expected.data(), reopened_expected.size()),
                scenario_text + " fresh-reopen output");

            reopened_editor.save_as(reopened_noop_output);
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh-reopen no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(reopened_noop_output)
                    == reopened_entries,
                scenario_text + " fresh-reopen no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh-reopen no-op save should not mutate its saved input");
            check_reopened_formula_dirty_output(
                reopened_noop_output,
                reopened_range,
                std::span<const ReopenedFormulaOutputCell>(
                    reopened_expected.data(), reopened_expected.size()),
                scenario_text + " fresh-reopen no-op output");
        };

    constexpr std::string_view insert_source_sheet_data =
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="e"><v>#VALUE!</v></c>)"
        R"(<c r="C1" t="e"><v>#N/A</v></c>)"
        R"(</row><row r="2">)"
        R"(<c r="B2" t="e"><v>#DIV/0!</v></c>)"
        R"(</row></sheetData>)";
    const ReopenedFormulaOutputCell insert_rows_expected[] = {
        {2, 1, fastxlsx::CellValue::error("#VALUE!")},
        {2, 3, fastxlsx::CellValue::error("#N/A")},
        {3, 2, fastxlsx::CellValue::error("#DIV/0!")},
    };
    run_error_shift_case("insert-rows",
        "structural insert_rows source error cell shift",
        write_error_shift_source(
            "fastxlsx-workbook-editor-public-structural-insert-rows-source-error-cell-source.xlsx",
            insert_source_sheet_data),
        3,
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(1, 1); },
        fastxlsx::CellRange {2, 1, 3, 3},
        insert_rows_expected,
        {
            R"(<c r="A2" t="e"><v>#VALUE!</v></c>)",
            R"(<c r="C2" t="e"><v>#N/A</v></c>)",
            R"(<c r="B3" t="e"><v>#DIV/0!</v></c>)",
        },
        {},
        fastxlsx::CellRange {2, 1, 4, 4},
        ReopenedFormulaOutputCell {4, 4, fastxlsx::CellValue::error("#NULL!")},
        R"(<c r="D4" t="e"><v>#NULL!</v></c>)");

    const ReopenedFormulaOutputCell insert_columns_expected[] = {
        {1, 2, fastxlsx::CellValue::error("#VALUE!")},
        {1, 4, fastxlsx::CellValue::error("#N/A")},
        {2, 3, fastxlsx::CellValue::error("#DIV/0!")},
    };
    run_error_shift_case("insert-columns",
        "structural insert_columns source error cell shift",
        write_error_shift_source(
            "fastxlsx-workbook-editor-public-structural-insert-columns-source-error-cell-source.xlsx",
            insert_source_sheet_data),
        3,
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(1, 1); },
        fastxlsx::CellRange {1, 2, 2, 4},
        insert_columns_expected,
        {
            R"(<c r="B1" t="e"><v>#VALUE!</v></c>)",
            R"(<c r="D1" t="e"><v>#N/A</v></c>)",
            R"(<c r="C2" t="e"><v>#DIV/0!</v></c>)",
        },
        {},
        fastxlsx::CellRange {1, 2, 3, 5},
        ReopenedFormulaOutputCell {3, 5, fastxlsx::CellValue::error("#NULL!")},
        R"(<c r="E3" t="e"><v>#NULL!</v></c>)");

    constexpr std::string_view delete_rows_source_sheet_data =
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="e"><v>#NULL!</v></c>)"
        R"(</row><row r="2">)"
        R"(<c r="A2" t="e"><v>#VALUE!</v></c>)"
        R"(</row><row r="3">)"
        R"(<c r="C3" t="e"><v>#N/A</v></c>)"
        R"(</row></sheetData>)";
    const ReopenedFormulaOutputCell delete_rows_expected[] = {
        {1, 1, fastxlsx::CellValue::error("#VALUE!")},
        {2, 3, fastxlsx::CellValue::error("#N/A")},
    };
    run_error_shift_case("delete-rows",
        "structural delete_rows source error cell shift",
        write_error_shift_source(
            "fastxlsx-workbook-editor-public-structural-delete-rows-source-error-cell-source.xlsx",
            delete_rows_source_sheet_data),
        3,
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(1, 1); },
        fastxlsx::CellRange {1, 1, 2, 3},
        delete_rows_expected,
        {
            R"(<c r="A1" t="e"><v>#VALUE!</v></c>)",
            R"(<c r="C2" t="e"><v>#N/A</v></c>)",
        },
        {"#NULL!"},
        fastxlsx::CellRange {1, 1, 3, 4},
        ReopenedFormulaOutputCell {3, 4, fastxlsx::CellValue::error("#DIV/0!")},
        R"(<c r="D3" t="e"><v>#DIV/0!</v></c>)");

    constexpr std::string_view delete_columns_source_sheet_data =
        R"(<sheetData><row r="1">)"
        R"(<c r="A1" t="e"><v>#NULL!</v></c>)"
        R"(<c r="B1" t="e"><v>#VALUE!</v></c>)"
        R"(</row><row r="2">)"
        R"(<c r="D2" t="e"><v>#N/A</v></c>)"
        R"(</row></sheetData>)";
    const ReopenedFormulaOutputCell delete_columns_expected[] = {
        {1, 1, fastxlsx::CellValue::error("#VALUE!")},
        {2, 3, fastxlsx::CellValue::error("#N/A")},
    };
    run_error_shift_case("delete-columns",
        "structural delete_columns source error cell shift",
        write_error_shift_source(
            "fastxlsx-workbook-editor-public-structural-delete-columns-source-error-cell-source.xlsx",
            delete_columns_source_sheet_data),
        3,
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(1, 1); },
        fastxlsx::CellRange {1, 1, 2, 3},
        delete_columns_expected,
        {
            R"(<c r="A1" t="e"><v>#VALUE!</v></c>)",
            R"(<c r="C2" t="e"><v>#N/A</v></c>)",
        },
        {"#NULL!"},
        fastxlsx::CellRange {1, 1, 3, 4},
        ReopenedFormulaOutputCell {3, 4, fastxlsx::CellValue::error("#DIV/0!")},
        R"(<c r="D3" t="e"><v>#DIV/0!</v></c>)");
}

void test_public_worksheet_editor_ignores_formula_cached_result_types()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-formula-cached-result-types-source.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-formula-cached-result-types-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-formula-cached-result-types-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-formula-cached-result-types-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-formula-cached-result-types-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-formula-cached-result-types-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f>A2+1</f><v>999</v></c>)"
        R"(<c r="B1" t="str"><f>TEXT(A1,"@")</f><v>stale-string</v></c>)"
        R"(<c r="C1" t="b"><f>A1&gt;0</f><v>1</v></c>)"
        R"(<c r="D1" t="e"><f>NA()</f><v>#N/A</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Formula
            && a1->text_value() == "A2+1",
        "WorksheetEditor should ignore numeric cached results when source formula text exists");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Formula
            && b1->text_value() == "TEXT(A1,\"@\")",
        "WorksheetEditor should ignore t=str cached results when source formula text exists");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "A1>0",
        "WorksheetEditor should ignore boolean cached results when source formula text exists");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Formula
            && d1->text_value() == "NA()",
        "WorksheetEditor should ignore error cached results when source formula text exists");
    check(!sheet.has_pending_changes(),
        "formula cached-result materialization should start clean");
    check(!editor.has_pending_changes(),
        "formula cached-result materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "formula cached-result materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "cached-result formula no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "cached-result formula no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "cached-result formula no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "cached-result formula no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "cached-result formula no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("A2+1")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(A1,\"@\")")},
        {1, 3, fastxlsx::CellValue::formula("A1>0")},
        {1, 4, fastxlsx::CellValue::formula("NA()")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 4},
        noop_cells,
        "cached-result formula no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("cached-result-types-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1"><f>A2+1</f></c>)",
        "dirty projection should write numeric-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="B1"><f>TEXT(A1,"@")</f></c>)",
        "dirty projection should write t=str-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1&gt;0</f></c>)",
        "dirty projection should write boolean-cached formulas without stale values");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>NA()</f></c>)",
        "dirty projection should write error-cached formulas without stale values");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "dirty projection should drop stale numeric cached formula values");
    check_not_contains(output_worksheet_xml, "stale-string",
        "dirty projection should drop stale string cached formula values");
    check_not_contains(output_worksheet_xml, "<v>1</v>",
        "dirty projection should drop stale boolean cached formula values");
    check_not_contains(output_worksheet_xml, "<v>#N/A</v>",
        "dirty projection should drop stale error cached formula values");
    check_contains(output_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>cached-result-types-edit</t></is></c>)",
        "dirty projection should include later edits beside cached-result formulas");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "formula cached-result rewrite should not mutate untouched source sheet bytes");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("A2+1")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(A1,\"@\")")},
        {1, 3, fastxlsx::CellValue::formula("A1>0")},
        {1, 4, fastxlsx::CellValue::formula("NA()")},
        {2, 4, fastxlsx::CellValue::text("cached-result-types-edit")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "cached-result formula dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "cached-result formula post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "cached-result formula post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "cached-result formula post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "cached-result formula post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "cached-result formula post-dirty no-op output");

    sheet.set_cell("E3", fastxlsx::CellValue::formula("A1&\"<cached>\"&D1"));
    check(sheet.has_pending_changes(),
        "cached-result formula post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "cached-result formula post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "cached-result formula post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="E3"><f>A1&amp;"&lt;cached&gt;"&amp;D1</f></c>)",
        "cached-result formula post-noop reuse save should include the later escaped formula edit");
    check_not_contains(post_noop_reuse_xml, "<v>999</v>",
        "cached-result formula post-noop reuse save should keep numeric cached values omitted");
    check_not_contains(post_noop_reuse_xml, "stale-string",
        "cached-result formula post-noop reuse save should keep string cached values omitted");
    check_not_contains(post_noop_reuse_xml, "<v>1</v>",
        "cached-result formula post-noop reuse save should keep boolean cached values omitted");
    check_not_contains(post_noop_reuse_xml, "<v>#N/A</v>",
        "cached-result formula post-noop reuse save should keep error cached values omitted");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "cached-result formula post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "cached-result formula post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "cached-result formula post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "cached-result formula post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("A2+1")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(A1,\"@\")")},
        {1, 3, fastxlsx::CellValue::formula("A1>0")},
        {1, 4, fastxlsx::CellValue::formula("NA()")},
        {2, 4, fastxlsx::CellValue::text("cached-result-types-edit")},
        {3, 5, fastxlsx::CellValue::formula("A1&\"<cached>\"&D1")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "cached-result formula post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "cached-result formula post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "cached-result formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "cached-result formula post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "cached-result formula post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_unresolved_shared_formula_cached_scalars()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-source.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-noop-output.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-unresolved-shared-formula-cached-scalars-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f t="shared" si="70"/><v>12.5</v></c>)"
        R"(<c r="B1" t="str"><f t="shared" si="71"/><v>cached-text</v></c>)"
        R"(<c r="C1" t="b"><f t="shared" si="72"/><v>1</v></c>)"
        R"(<c r="D1" t="e"><f t="shared" si="73"/><v>#VALUE!</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 12.5,
        "unresolved shared formula cached scalar should materialize numeric fallback");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "cached-text",
        "unresolved shared formula cached scalar should materialize t=str fallback");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Boolean
            && c1->boolean_value(),
        "unresolved shared formula cached scalar should materialize boolean fallback");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Error
            && d1->text_value() == "#VALUE!",
        "unresolved shared formula cached scalar should materialize error fallback");
    check(!sheet.has_pending_changes(),
        "unresolved shared formula cached scalar materialization should start clean");
    check(!editor.has_pending_changes(),
        "unresolved shared formula cached scalar materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "unresolved shared formula cached scalar materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "unresolved shared formula cached scalar no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "unresolved shared formula cached scalar no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "unresolved shared formula cached scalar no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "unresolved shared formula cached scalar no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "unresolved shared formula cached scalar no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::number(12.5)},
        {1, 2, fastxlsx::CellValue::text("cached-text")},
        {1, 3, fastxlsx::CellValue::boolean(true)},
        {1, 4, fastxlsx::CellValue::error("#VALUE!")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 4},
        noop_cells,
        "unresolved shared formula cached scalar no-op output");

    sheet.set_cell("F2", fastxlsx::CellValue::formula("A1+B1"));
    check(sheet.has_pending_changes(),
        "unresolved shared formula cached scalar later formula edit should dirty Data");
    check(editor.has_pending_changes(),
        "unresolved shared formula cached scalar later formula edit should dirty WorkbookEditor");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1"><v>12.5</v></c>)",
        "dirty projection should write unresolved shared formula numeric fallback as scalar");
    check_contains(output_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>cached-text</t></is></c>)",
        "dirty projection should write unresolved shared formula string fallback as inline text");
    check_contains(output_worksheet_xml, R"(<c r="C1" t="b"><v>1</v></c>)",
        "dirty projection should write unresolved shared formula boolean fallback as scalar");
    check_contains(output_worksheet_xml, R"(<c r="D1" t="e"><v>#VALUE!</v></c>)",
        "dirty projection should write unresolved shared formula error fallback as scalar");
    check_contains(output_worksheet_xml, R"(<c r="F2"><f>A1+B1</f></c>)",
        "dirty projection should include the later formula edit");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "dirty projection should drop unresolved shared formula metadata");
    check_not_contains(output_worksheet_xml, R"(si=")",
        "dirty projection should drop unresolved shared formula indexes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "unresolved shared formula cached scalar dirty save should not mutate the source package");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "unresolved shared formula cached scalar dirty save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(12.5)},
        {1, 2, fastxlsx::CellValue::text("cached-text")},
        {1, 3, fastxlsx::CellValue::boolean(true)},
        {1, 4, fastxlsx::CellValue::error("#VALUE!")},
        {2, 6, fastxlsx::CellValue::formula("A1+B1")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 6},
        expected_cells,
        "unresolved shared formula cached scalar dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "unresolved shared formula cached scalar dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "unresolved shared formula cached scalar dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "unresolved shared formula cached scalar dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "unresolved shared formula cached scalar dirty no-op save should not mutate prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 6},
        expected_cells,
        "unresolved shared formula cached scalar dirty no-op output");

    sheet.set_cell("G3", fastxlsx::CellValue::error("#N/A"));
    check(sheet.has_pending_changes(),
        "unresolved shared formula cached scalar post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "unresolved shared formula cached scalar post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<c r="A1"><v>12.5</v></c>)",
        "unresolved shared formula cached scalar post-noop reuse should keep numeric fallback");
    check_contains(post_noop_reuse_xml,
        R"(<c r="B1" t="inlineStr"><is><t>cached-text</t></is></c>)",
        "unresolved shared formula cached scalar post-noop reuse should keep string fallback");
    check_contains(post_noop_reuse_xml, R"(<c r="C1" t="b"><v>1</v></c>)",
        "unresolved shared formula cached scalar post-noop reuse should keep boolean fallback");
    check_contains(post_noop_reuse_xml, R"(<c r="D1" t="e"><v>#VALUE!</v></c>)",
        "unresolved shared formula cached scalar post-noop reuse should keep error fallback");
    check_contains(post_noop_reuse_xml, R"(<c r="F2"><f>A1+B1</f></c>)",
        "unresolved shared formula cached scalar post-noop reuse should keep prior formula edit");
    check_contains(post_noop_reuse_xml, R"(<c r="G3" t="e"><v>#N/A</v></c>)",
        "unresolved shared formula cached scalar post-noop reuse should include the later error edit");
    check_not_contains(post_noop_reuse_xml, R"(t="shared")",
        "unresolved shared formula cached scalar post-noop reuse should keep shared metadata dropped");
    check_not_contains(post_noop_reuse_xml, R"(si=")",
        "unresolved shared formula cached scalar post-noop reuse should keep shared indexes dropped");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "unresolved shared formula cached scalar post-noop reuse should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "unresolved shared formula cached scalar post-noop reuse should not mutate prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "unresolved shared formula cached scalar post-noop reuse should not mutate prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "unresolved shared formula cached scalar post-noop reuse should not mutate prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(12.5)},
        {1, 2, fastxlsx::CellValue::text("cached-text")},
        {1, 3, fastxlsx::CellValue::boolean(true)},
        {1, 4, fastxlsx::CellValue::error("#VALUE!")},
        {2, 6, fastxlsx::CellValue::formula("A1+B1")},
        {3, 7, fastxlsx::CellValue::error("#N/A")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 7},
        post_noop_reuse_cells,
        "unresolved shared formula cached scalar post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "unresolved shared formula cached scalar post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "unresolved shared formula cached scalar post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "unresolved shared formula cached scalar post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 7},
        post_noop_reuse_cells,
        "unresolved shared formula cached scalar post-noop reuse no-op output");
}

void test_public_worksheet_editor_direct_formula_source_mutations_drop_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-direct-formula-source-mutation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-direct-formula-source-mutation-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-direct-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-public-direct-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-direct-formula-source-mutation-reopened-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f>A2+1</f><v>999</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:C1" si="70" ca="1">A1+1</f><v>111</v></c>)"
        R"(<c r="C1"><f t="shared" si="70" aca="1"/><v>222</v></c>)"
        R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>333</v></c>)"
        R"(<c r="E1"><f t="array" ref="D1:E1"/><v>444</v></c>)"
        R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+2</f><v>555</v></c>)"
        R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>666</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> ordinary_formula = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> shared_follower = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> array_formula = sheet.try_cell("D1");
    const std::optional<fastxlsx::CellValue> array_cached = sheet.try_cell("E1");
    const std::optional<fastxlsx::CellValue> datatable_formula = sheet.try_cell("F1");
    const std::optional<fastxlsx::CellValue> datatable_cached = sheet.try_cell("G1");
    check(ordinary_formula.has_value()
            && ordinary_formula->kind() == fastxlsx::CellValueKind::Formula
            && ordinary_formula->text_value() == "A2+1",
        "direct formula source mutation setup should materialize ordinary formulas");
    check(shared_follower.has_value()
            && shared_follower->kind() == fastxlsx::CellValueKind::Formula
            && shared_follower->text_value() == "B1+1",
        "direct formula source mutation setup should translate shared followers");
    check(array_formula.has_value() && array_formula->kind() == fastxlsx::CellValueKind::Formula
            && array_formula->text_value() == "SUM(A1:B1)",
        "direct formula source mutation setup should flatten array formula text");
    check(array_cached.has_value() && array_cached->kind() == fastxlsx::CellValueKind::Number
            && array_cached->number_value() == 444.0,
        "direct formula source mutation setup should read array metadata-only cached values");
    check(datatable_formula.has_value()
            && datatable_formula->kind() == fastxlsx::CellValueKind::Formula
            && datatable_formula->text_value() == "A1+2",
        "direct formula source mutation setup should flatten dataTable formula text");
    check(datatable_cached.has_value()
            && datatable_cached->kind() == fastxlsx::CellValueKind::Number
            && datatable_cached->number_value() == 666.0,
        "direct formula source mutation setup should read dataTable metadata-only cached values");
    check(!sheet.has_pending_changes(),
        "direct formula source mutation setup should start clean");
    check(!editor.has_pending_changes(),
        "direct formula source mutation setup should not dirty WorkbookEditor");

    sheet.set_cell_value("A1", fastxlsx::CellValue::text("ordinary-overwrite"));
    sheet.set_cell("B1", fastxlsx::CellValue::number(12.5));
    sheet.clear_cell_value("C1");
    sheet.erase_cell("D1");
    sheet.set_cell("E1", fastxlsx::CellValue::formula("A1+B1"));
    sheet.erase_cell("F1");
    sheet.clear_cell_value("G1");

    check(sheet.has_pending_changes(),
        "direct formula source mutations should dirty Data");
    check(editor.has_pending_changes(),
        "direct formula source mutations should dirty WorkbookEditor");
    check(sheet.cell_count() == 5,
        "direct formula source mutations should remove erased source formula records");
    check(!sheet.try_cell("D1").has_value(),
        "direct formula source mutations should erase the array formula cell");
    check(!sheet.try_cell("F1").has_value(),
        "direct formula source mutations should erase the dataTable formula cell");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "direct formula source mutations should keep cleared shared follower as blank");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Blank,
        "direct formula source mutations should keep cleared dataTable fallback as blank");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "direct formula source mutation save should keep Data clean");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>ordinary-overwrite</t></is></c>)",
        "direct formula source mutation save should overwrite ordinary formula text");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>12.5</v></c>)",
        "direct formula source mutation save should overwrite shared formula base");
    check_contains(output_worksheet_xml, R"(<c r="C1"/>)",
        "direct formula source mutation save should persist cleared shared follower blank");
    check_not_contains(output_worksheet_xml, "<c r=\"D1\"",
        "direct formula source mutation save should omit erased array formula cell");
    check_contains(output_worksheet_xml, R"(<c r="E1"><f>A1+B1</f></c>)",
        "direct formula source mutation save should overwrite array cached fallback");
    check_not_contains(output_worksheet_xml, "<c r=\"F1\"",
        "direct formula source mutation save should omit erased dataTable formula cell");
    check_contains(output_worksheet_xml, R"(<c r="G1"/>)",
        "direct formula source mutation save should persist cleared dataTable fallback blank");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "direct formula source mutation save should drop shared formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="array")",
        "direct formula source mutation save should drop array formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="dataTable")",
        "direct formula source mutation save should drop dataTable formula metadata");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "direct formula source mutation save should drop calc metadata attributes");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "direct formula source mutation save should drop dataTable attributes");
    check_not_contains(output_worksheet_xml, "<f>A2+1</f>",
        "direct formula source mutation save should not revive the ordinary formula");
    check_not_contains(output_worksheet_xml, "SUM(A1:B1)",
        "direct formula source mutation save should not revive the array formula");
    check_not_contains(output_worksheet_xml, "<f>A1+2</f>",
        "direct formula source mutation save should not revive the dataTable formula");
    const int stale_cached_values[] = {999, 111, 222, 333, 444, 555, 666};
    for (const int stale_cached_value : stale_cached_values) {
        check_not_contains(output_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "direct formula source mutation save should drop stale cached values");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "direct formula source mutation save should not mutate the source package");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "direct formula source mutation save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("ordinary-overwrite")},
        {1, 2, fastxlsx::CellValue::number(12.5)},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 5, fastxlsx::CellValue::formula("A1+B1")},
        {1, 7, fastxlsx::CellValue::blank()},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 1, 7},
        expected_cells,
        "direct formula source mutation output");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "direct formula source mutation no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "direct formula source mutation no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "direct formula source mutation no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 7},
        expected_cells,
        "direct formula source mutation no-op output");

    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!reopened_sheet.has_pending_changes(),
        "direct formula source mutation fresh reopen should start clean");
    check(!reopened_editor.has_pending_changes(),
        "direct formula source mutation fresh reopen should keep WorkbookEditor clean");
    check(reopened_sheet.cell_count() == 5,
        "direct formula source mutation fresh reopen should preserve sparse count");
    check(!reopened_sheet.try_cell("D1").has_value(),
        "direct formula source mutation fresh reopen should keep erased array formula absent");
    check(!reopened_sheet.try_cell("F1").has_value(),
        "direct formula source mutation fresh reopen should keep erased dataTable formula absent");
    check(reopened_sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "direct formula source mutation fresh reopen should keep shared follower blank");
    check(reopened_sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Blank,
        "direct formula source mutation fresh reopen should keep dataTable fallback blank");

    reopened_sheet.set_cell("H2", fastxlsx::CellValue::formula("E1+B1"));
    check(reopened_sheet.has_pending_changes(),
        "direct formula source mutation fresh-reopen edit should dirty Data");
    check(reopened_editor.has_pending_changes(),
        "direct formula source mutation fresh-reopen edit should dirty WorkbookEditor");
    reopened_editor.save_as(reopened_output);
    check(!reopened_sheet.has_pending_changes(),
        "direct formula source mutation fresh-reopen save should keep Data clean");

    const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
    const std::string& reopened_worksheet_xml =
        reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_worksheet_xml, R"(<c r="H2"><f>E1+B1</f></c>)",
        "direct formula source mutation fresh-reopen save should include the later formula edit");
    check_not_contains(reopened_worksheet_xml, R"(t="shared")",
        "direct formula source mutation fresh-reopen save should keep shared metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="array")",
        "direct formula source mutation fresh-reopen save should keep array metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="dataTable")",
        "direct formula source mutation fresh-reopen save should keep dataTable metadata dropped");
    for (const int stale_cached_value : stale_cached_values) {
        check_not_contains(reopened_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "direct formula source mutation fresh-reopen save should keep stale cached values dropped");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "direct formula source mutation fresh-reopen save should not mutate the original source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "direct formula source mutation fresh-reopen save should not mutate its saved input");

    const ReopenedFormulaOutputCell reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("ordinary-overwrite")},
        {1, 2, fastxlsx::CellValue::number(12.5)},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 5, fastxlsx::CellValue::formula("A1+B1")},
        {1, 7, fastxlsx::CellValue::blank()},
        {2, 8, fastxlsx::CellValue::formula("E1+B1")},
    };
    check_reopened_formula_dirty_output(
        reopened_output,
        fastxlsx::CellRange {1, 1, 2, 8},
        reopened_expected_cells,
        "direct formula source mutation fresh-reopen output");

    reopened_editor.save_as(reopened_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "direct formula source mutation fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(reopened_noop_output) == reopened_entries,
        "direct formula source mutation fresh-reopen no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "direct formula source mutation fresh-reopen no-op save should not mutate its saved input");
    check_reopened_formula_dirty_output(
        reopened_noop_output,
        fastxlsx::CellRange {1, 1, 2, 8},
        reopened_expected_cells,
        "direct formula source mutation fresh-reopen no-op output");
}

void test_public_worksheet_editor_batch_formula_source_mutations_drop_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-batch-formula-source-mutation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-batch-formula-source-mutation-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-batch-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-public-batch-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-batch-formula-source-mutation-reopened-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f>A2+10</f><v>1001</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:C1" si="71" ca="1">A1+10</f><v>1002</v></c>)"
        R"(<c r="C1"><f t="shared" si="71" aca="1"/><v>1003</v></c>)"
        R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>1004</v></c>)"
        R"(<c r="E1"><f t="array" ref="D1:E1"/><v>1005</v></c>)"
        R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+20</f><v>1006</v></c>)"
        R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>1007</v></c>)"
        R"(<c r="H1"><f>G1+1</f><v>1008</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    check(sheet.cell_count() == 8,
        "batch formula source mutation setup should materialize all source records");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("C1").text_value() == "B1+10",
        "batch formula source mutation setup should translate shared followers");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("D1").text_value() == "SUM(A1:B1)",
        "batch formula source mutation setup should flatten array formula text");
    check(sheet.get_cell("E1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("E1").number_value() == 1005.0,
        "batch formula source mutation setup should materialize array cached fallback");
    check(sheet.get_cell("F1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("F1").text_value() == "A1+20",
        "batch formula source mutation setup should flatten dataTable formula text");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("G1").number_value() == 1007.0,
        "batch formula source mutation setup should materialize dataTable cached fallback");
    check(!sheet.has_pending_changes(),
        "batch formula source mutation setup should start clean");
    check(!editor.has_pending_changes(),
        "batch formula source mutation setup should not dirty WorkbookEditor");

    sheet.set_cell_values({
        {{1, 1}, fastxlsx::CellValue::text("batch-overwrite-a1")},
        {{1, 2}, fastxlsx::CellValue::number(25.0)},
        {{1, 8}, fastxlsx::CellValue::formula("A1+B1")},
    });
    sheet.clear_cell_values("C1:D1");
    sheet.clear_cell_values({fastxlsx::WorksheetCellReference {1, 7}});
    sheet.erase_cells("E1:F1");

    check(sheet.has_pending_changes(),
        "batch formula source mutations should dirty Data");
    check(editor.has_pending_changes(),
        "batch formula source mutations should dirty WorkbookEditor");
    check(sheet.cell_count() == 6,
        "batch formula source mutations should remove erased source formula records");
    check(!sheet.try_cell("E1").has_value(),
        "batch formula source mutations should erase array cached fallback");
    check(!sheet.try_cell("F1").has_value(),
        "batch formula source mutations should erase dataTable formula cell");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Blank,
        "batch formula source mutations should clear shared follower as blank");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Blank,
        "batch formula source mutations should clear array formula as blank");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Blank,
        "batch formula source mutations should clear dataTable fallback as blank");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "batch formula source mutation save should keep Data clean");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>batch-overwrite-a1</t></is></c>)",
        "batch formula source mutation save should persist batch text overwrite");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>25</v></c>)",
        "batch formula source mutation save should persist batch numeric overwrite");
    check_contains(output_worksheet_xml, R"(<c r="C1"/>)",
        "batch formula source mutation save should persist A1-range cleared shared follower");
    check_contains(output_worksheet_xml, R"(<c r="D1"/>)",
        "batch formula source mutation save should persist A1-range cleared array formula");
    check_not_contains(output_worksheet_xml, "<c r=\"E1\"",
        "batch formula source mutation save should omit erased array fallback");
    check_not_contains(output_worksheet_xml, "<c r=\"F1\"",
        "batch formula source mutation save should omit erased dataTable formula");
    check_contains(output_worksheet_xml, R"(<c r="G1"/>)",
        "batch formula source mutation save should persist coordinate-batch cleared dataTable fallback");
    check_contains(output_worksheet_xml, R"(<c r="H1"><f>A1+B1</f></c>)",
        "batch formula source mutation save should persist batch formula overwrite");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "batch formula source mutation save should drop shared formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="array")",
        "batch formula source mutation save should drop array formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="dataTable")",
        "batch formula source mutation save should drop dataTable formula metadata");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "batch formula source mutation save should drop calc metadata attributes");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "batch formula source mutation save should drop dataTable attributes");
    check_not_contains(output_worksheet_xml, "<f>A2+10</f>",
        "batch formula source mutation save should not revive ordinary source formula");
    check_not_contains(output_worksheet_xml, "SUM(A1:B1)",
        "batch formula source mutation save should not revive array formula text");
    check_not_contains(output_worksheet_xml, "<f>A1+20</f>",
        "batch formula source mutation save should not revive dataTable formula text");
    check_not_contains(output_worksheet_xml, "<f>G1+1</f>",
        "batch formula source mutation save should not revive overwritten formula text");
    const int stale_cached_values[] = {1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008};
    for (const int stale_cached_value : stale_cached_values) {
        check_not_contains(output_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "batch formula source mutation save should drop stale cached values");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "batch formula source mutation save should not mutate the source package");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "batch formula source mutation save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("batch-overwrite-a1")},
        {1, 2, fastxlsx::CellValue::number(25.0)},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 4, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::formula("A1+B1")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 1, 8},
        expected_cells,
        "batch formula source mutation output");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "batch formula source mutation no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "batch formula source mutation no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "batch formula source mutation no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 8},
        expected_cells,
        "batch formula source mutation no-op output");

    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!reopened_sheet.has_pending_changes(),
        "batch formula source mutation fresh reopen should start clean");
    check(reopened_sheet.cell_count() == 6,
        "batch formula source mutation fresh reopen should preserve sparse count");
    check(!reopened_sheet.try_cell("E1").has_value(),
        "batch formula source mutation fresh reopen should keep erased array fallback absent");
    check(!reopened_sheet.try_cell("F1").has_value(),
        "batch formula source mutation fresh reopen should keep erased dataTable formula absent");

    reopened_sheet.set_cell_values({
        {{2, 1}, fastxlsx::CellValue::text("batch-reopen-text")},
        {{2, 8}, fastxlsx::CellValue::formula("H1+B1")},
    });
    reopened_editor.save_as(reopened_output);
    check(!reopened_sheet.has_pending_changes(),
        "batch formula source mutation fresh-reopen save should keep Data clean");
    const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
    const std::string& reopened_worksheet_xml =
        reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>batch-reopen-text</t></is></c>)",
        "batch formula source mutation fresh-reopen save should include later batch text");
    check_contains(reopened_worksheet_xml, R"(<c r="H2"><f>H1+B1</f></c>)",
        "batch formula source mutation fresh-reopen save should include later batch formula");
    check_not_contains(reopened_worksheet_xml, R"(t="shared")",
        "batch formula source mutation fresh-reopen save should keep shared metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="array")",
        "batch formula source mutation fresh-reopen save should keep array metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="dataTable")",
        "batch formula source mutation fresh-reopen save should keep dataTable metadata dropped");
    for (const int stale_cached_value : stale_cached_values) {
        check_not_contains(reopened_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "batch formula source mutation fresh-reopen save should keep stale cached values dropped");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "batch formula source mutation fresh-reopen save should not mutate original source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "batch formula source mutation fresh-reopen save should not mutate its saved input");

    const ReopenedFormulaOutputCell reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("batch-overwrite-a1")},
        {1, 2, fastxlsx::CellValue::number(25.0)},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 4, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::formula("A1+B1")},
        {2, 1, fastxlsx::CellValue::text("batch-reopen-text")},
        {2, 8, fastxlsx::CellValue::formula("H1+B1")},
    };
    check_reopened_formula_dirty_output(
        reopened_output,
        fastxlsx::CellRange {1, 1, 2, 8},
        reopened_expected_cells,
        "batch formula source mutation fresh-reopen output");

    reopened_editor.save_as(reopened_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "batch formula source mutation fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(reopened_noop_output) == reopened_entries,
        "batch formula source mutation fresh-reopen no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "batch formula source mutation fresh-reopen no-op save should not mutate its saved input");
    check_reopened_formula_dirty_output(
        reopened_noop_output,
        fastxlsx::CellRange {1, 1, 2, 8},
        reopened_expected_cells,
        "batch formula source mutation fresh-reopen no-op output");
}

void test_public_worksheet_editor_row_column_formula_source_mutations_drop_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-row-column-formula-source-mutation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-row-column-formula-source-mutation-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-row-column-formula-source-mutation-reopened-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><f>A2+21</f><v>2101</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:C2" si="82" ca="1">A1+30</f><v>2102</v></c>)"
        R"(<c r="C1"><f t="shared" si="82" aca="1"/><v>2103</v></c>)"
        R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>2104</v></c>)"
        R"(<c r="E1"><f t="array" ref="D1:E1"/><v>2105</v></c>)"
        R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+40</f><v>2106</v></c>)"
        R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>2107</v></c>)"
        R"(<c r="H1"><f>G1+1</f><v>2108</v></c>)"
        R"(</row>)"
        R"(<row r="2">)"
        R"(<c r="A2"><f>A1+1</f><v>2201</v></c>)"
        R"(<c r="B2"><f t="shared" si="82"/><v>2202</v></c>)"
        R"(<c r="C2"><f>C1+1</f><v>2203</v></c>)"
        R"(<c r="D2"><f>D1+1</f><v>2204</v></c>)"
        R"(<c r="E2"><f>E1+1</f><v>2205</v></c>)"
        R"(<c r="F2"><f>F1+1</f><v>2206</v></c>)"
        R"(<c r="G2"><f>G1+1</f><v>2207</v></c>)"
        R"(<c r="H2"><f>H1+1</f><v>2208</v></c>)"
        R"(</row>)"
        R"(<row r="3">)"
        R"(<c r="A3"><f>A2+1</f><v>2301</v></c>)"
        R"(<c r="B3"><f>B2+1</f><v>2302</v></c>)"
        R"(<c r="C3"><f>C2+1</f><v>2303</v></c>)"
        R"(<c r="D3"><f>D2+1</f><v>2304</v></c>)"
        R"(<c r="E3"><f>E2+1</f><v>2305</v></c>)"
        R"(<c r="F3"><f>F2+1</f><v>2306</v></c>)"
        R"(<c r="G3"><f>G2+1</f><v>2307</v></c>)"
        R"(<c r="H3"><f>H2+1</f><v>2308</v></c>)"
        R"(</row>)"
        R"(</sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    check(sheet.cell_count() == 24,
        "row/column formula source mutation setup should materialize all source records");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("C1").text_value() == "B1+30",
        "row/column formula source mutation setup should translate shared row followers");
    check(sheet.get_cell("B2").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("B2").text_value() == "A2+30",
        "row/column formula source mutation setup should translate shared column followers");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("D1").text_value() == "SUM(A1:B1)",
        "row/column formula source mutation setup should flatten array formula text");
    check(sheet.get_cell("E1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("E1").number_value() == 2105.0,
        "row/column formula source mutation setup should materialize array cached fallback");
    check(sheet.get_cell("F1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("F1").text_value() == "A1+40",
        "row/column formula source mutation setup should flatten dataTable formula text");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("G1").number_value() == 2107.0,
        "row/column formula source mutation setup should materialize dataTable cached fallback");
    check(!sheet.has_pending_changes(),
        "row/column formula source mutation setup should start clean");
    check(!editor.has_pending_changes(),
        "row/column formula source mutation setup should not dirty WorkbookEditor");

    sheet.set_row_values(1, {
        fastxlsx::CellValue::text("row-overwrite-a1"),
        fastxlsx::CellValue::number(31.0),
        fastxlsx::CellValue::formula("A2+B2"),
    });
    sheet.set_column_values(4, {
        fastxlsx::CellValue::formula("A1+B1"),
        fastxlsx::CellValue::text("column-overwrite-d2"),
        fastxlsx::CellValue::number(43.0),
    });
    sheet.clear_rows(2, 2);
    sheet.clear_columns(5, 7);
    sheet.erase_rows(3, 3);
    sheet.erase_columns(6, 6);

    check(sheet.has_pending_changes(),
        "row/column formula source mutations should dirty Data");
    check(editor.has_pending_changes(),
        "row/column formula source mutations should dirty WorkbookEditor");
    check(sheet.cell_count() == 14,
        "row/column formula source mutations should keep only represented row/column records");
    check(sheet.get_cell("E1").kind() == fastxlsx::CellValueKind::Blank,
        "row/column formula source mutations should clear array cached fallback by column");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Blank,
        "row/column formula source mutations should clear dataTable cached fallback by column");
    check(!sheet.try_cell("F1").has_value(),
        "row/column formula source mutations should erase dataTable formula column");
    check(!sheet.try_cell("A3").has_value(),
        "row/column formula source mutations should erase the source formula row");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "row/column formula source mutation save should keep Data clean");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>row-overwrite-a1</t></is></c>)",
        "row/column formula source mutation save should persist row text overwrite");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>31</v></c>)",
        "row/column formula source mutation save should persist row numeric overwrite");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A2+B2</f></c>)",
        "row/column formula source mutation save should persist row formula overwrite");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>A1+B1</f></c>)",
        "row/column formula source mutation save should persist column formula overwrite");
    check_contains(output_worksheet_xml, R"(<c r="E1"/>)",
        "row/column formula source mutation save should persist cleared array fallback blank");
    check_not_contains(output_worksheet_xml, "<c r=\"F1\"",
        "row/column formula source mutation save should omit erased dataTable formula cell");
    check_contains(output_worksheet_xml, R"(<c r="G1"/>)",
        "row/column formula source mutation save should persist cleared dataTable fallback blank");
    check_contains(output_worksheet_xml, R"(<c r="H1"><f>G1+1</f></c>)",
        "row/column formula source mutation save should flatten untouched ordinary formula text");
    check_contains(output_worksheet_xml, R"(<c r="A2"/>)",
        "row/column formula source mutation save should persist cleared row A2 blank");
    check_contains(output_worksheet_xml, R"(<c r="D2"/>)",
        "row/column formula source mutation save should clear the column-overwritten D2 cell");
    check_not_contains(output_worksheet_xml, "<c r=\"F2\"",
        "row/column formula source mutation save should omit erased F2 blank");
    check_not_contains(output_worksheet_xml, "<c r=\"A3\"",
        "row/column formula source mutation save should omit erased source formula row");
    check_not_contains(output_worksheet_xml, "<c r=\"D3\"",
        "row/column formula source mutation save should omit erased column-overwritten row");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "row/column formula source mutation save should drop shared formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="array")",
        "row/column formula source mutation save should drop array formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="dataTable")",
        "row/column formula source mutation save should drop dataTable formula metadata");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "row/column formula source mutation save should drop calc metadata attributes");
    check_not_contains(output_worksheet_xml, R"(aca="1")",
        "row/column formula source mutation save should drop shared follower metadata attributes");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "row/column formula source mutation save should drop dataTable attributes");
    check_not_contains(output_worksheet_xml, R"(r1="A1")",
        "row/column formula source mutation save should drop dataTable input metadata");
    check_not_contains(output_worksheet_xml, "<f>A2+21</f>",
        "row/column formula source mutation save should not revive overwritten ordinary formula text");
    check_not_contains(output_worksheet_xml, "SUM(A1:B1)",
        "row/column formula source mutation save should not revive array formula text");
    check_not_contains(output_worksheet_xml, "<f>A1+40</f>",
        "row/column formula source mutation save should not revive dataTable formula text");
    check_not_contains(output_worksheet_xml, "<f>H2+1</f>",
        "row/column formula source mutation save should not revive erased row formula text");
    for (int stale_cached_value = 2101; stale_cached_value <= 2308; ++stale_cached_value) {
        check_not_contains(output_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "row/column formula source mutation save should drop stale cached values");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column formula source mutation save should not mutate the source package");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "row/column formula source mutation save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("row-overwrite-a1")},
        {1, 2, fastxlsx::CellValue::number(31.0)},
        {1, 3, fastxlsx::CellValue::formula("A2+B2")},
        {1, 4, fastxlsx::CellValue::formula("A1+B1")},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::formula("G1+1")},
        {2, 1, fastxlsx::CellValue::blank()},
        {2, 2, fastxlsx::CellValue::blank()},
        {2, 3, fastxlsx::CellValue::blank()},
        {2, 4, fastxlsx::CellValue::blank()},
        {2, 5, fastxlsx::CellValue::blank()},
        {2, 7, fastxlsx::CellValue::blank()},
        {2, 8, fastxlsx::CellValue::blank()},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 8},
        expected_cells,
        "row/column formula source mutation output");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "row/column formula source mutation no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "row/column formula source mutation no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column formula source mutation no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 2, 8},
        expected_cells,
        "row/column formula source mutation no-op output");

    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!reopened_sheet.has_pending_changes(),
        "row/column formula source mutation fresh reopen should start clean");
    check(reopened_sheet.cell_count() == 14,
        "row/column formula source mutation fresh reopen should preserve sparse count");
    check(!reopened_sheet.try_cell("F1").has_value(),
        "row/column formula source mutation fresh reopen should keep erased F1 absent");
    check(!reopened_sheet.try_cell("A3").has_value(),
        "row/column formula source mutation fresh reopen should keep erased row absent");

    reopened_sheet.set_cell("H3", fastxlsx::CellValue::formula("H1+D1"));
    reopened_editor.save_as(reopened_output);
    check(!reopened_sheet.has_pending_changes(),
        "row/column formula source mutation fresh-reopen save should keep Data clean");
    const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
    const std::string& reopened_worksheet_xml =
        reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_worksheet_xml, R"(<c r="H3"><f>H1+D1</f></c>)",
        "row/column formula source mutation fresh-reopen save should include later formula edit");
    check_not_contains(reopened_worksheet_xml, R"(t="shared")",
        "row/column formula source mutation fresh-reopen save should keep shared metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="array")",
        "row/column formula source mutation fresh-reopen save should keep array metadata dropped");
    check_not_contains(reopened_worksheet_xml, R"(t="dataTable")",
        "row/column formula source mutation fresh-reopen save should keep dataTable metadata dropped");
    for (int stale_cached_value = 2101; stale_cached_value <= 2308; ++stale_cached_value) {
        check_not_contains(reopened_worksheet_xml,
            "<v>" + std::to_string(stale_cached_value) + "</v>",
            "row/column formula source mutation fresh-reopen save should keep stale cached values dropped");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "row/column formula source mutation fresh-reopen save should not mutate original source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "row/column formula source mutation fresh-reopen save should not mutate its saved input");

    const ReopenedFormulaOutputCell reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("row-overwrite-a1")},
        {1, 2, fastxlsx::CellValue::number(31.0)},
        {1, 3, fastxlsx::CellValue::formula("A2+B2")},
        {1, 4, fastxlsx::CellValue::formula("A1+B1")},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::formula("G1+1")},
        {2, 1, fastxlsx::CellValue::blank()},
        {2, 2, fastxlsx::CellValue::blank()},
        {2, 3, fastxlsx::CellValue::blank()},
        {2, 4, fastxlsx::CellValue::blank()},
        {2, 5, fastxlsx::CellValue::blank()},
        {2, 7, fastxlsx::CellValue::blank()},
        {2, 8, fastxlsx::CellValue::blank()},
        {3, 8, fastxlsx::CellValue::formula("H1+D1")},
    };
    check_reopened_formula_dirty_output(
        reopened_output,
        fastxlsx::CellRange {1, 1, 3, 8},
        reopened_expected_cells,
        "row/column formula source mutation fresh-reopen output");

    reopened_editor.save_as(reopened_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "row/column formula source mutation fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(reopened_noop_output) == reopened_entries,
        "row/column formula source mutation fresh-reopen no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "row/column formula source mutation fresh-reopen no-op save should not mutate its saved input");
    check_reopened_formula_dirty_output(
        reopened_noop_output,
        fastxlsx::CellRange {1, 1, 3, 8},
        reopened_expected_cells,
        "row/column formula source mutation fresh-reopen no-op output");
}

void test_public_worksheet_editor_whole_store_formula_source_mutations_drop_metadata()
{
    const auto write_formula_source = [](std::string_view name) {
        const std::filesystem::path source = write_two_sheet_source(name);
        const std::string worksheet_xml =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<sheetData><row r="1">)"
            R"(<c r="A1"><f>A2+50</f><v>3001</v></c>)"
            R"(<c r="B1"><f t="shared" ref="B1:C1" si="93" ca="1">A1+50</f><v>3002</v></c>)"
            R"(<c r="C1"><f t="shared" si="93" aca="1"/><v>3003</v></c>)"
            R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>3004</v></c>)"
            R"(<c r="E1"><f t="array" ref="D1:E1"/><v>3005</v></c>)"
            R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+60</f><v>3006</v></c>)"
            R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>3007</v></c>)"
            R"(<c r="H1"><f>G1+1</f><v>3008</v></c>)"
            R"(</row></sheetData></worksheet>)";
        rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
        return source;
    };

    const auto check_no_formula_metadata = [](const std::string& worksheet_xml,
                                              std::string_view scenario) {
        const std::string prefix(scenario);
        check_not_contains(worksheet_xml, R"(t="shared")",
            prefix + " should drop shared formula metadata");
        check_not_contains(worksheet_xml, R"(t="array")",
            prefix + " should drop array formula metadata");
        check_not_contains(worksheet_xml, R"(t="dataTable")",
            prefix + " should drop dataTable formula metadata");
        check_not_contains(worksheet_xml, R"(ca="1")",
            prefix + " should drop calc metadata attributes");
        check_not_contains(worksheet_xml, R"(aca="1")",
            prefix + " should drop shared follower metadata attributes");
        check_not_contains(worksheet_xml, R"(dt2D="1")",
            prefix + " should drop dataTable attributes");
        check_not_contains(worksheet_xml, R"(r1="A1")",
            prefix + " should drop dataTable input metadata");
        check_not_contains(worksheet_xml, "<f>A2+50</f>",
            prefix + " should not revive ordinary source formula text");
        check_not_contains(worksheet_xml, "SUM(A1:B1)",
            prefix + " should not revive array formula text");
        check_not_contains(worksheet_xml, "<f>A1+60</f>",
            prefix + " should not revive dataTable formula text");
        for (int stale_cached_value = 3001; stale_cached_value <= 3008; ++stale_cached_value) {
            check_not_contains(worksheet_xml,
                "<v>" + std::to_string(stale_cached_value) + "</v>",
                prefix + " should drop stale cached values");
        }
    };

    const auto check_reopened_empty_formula_output =
        [](const std::filesystem::path& output, std::string_view scenario) {
            const std::string prefix(scenario);
            fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
            fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());

            check_workbook_editor_public_clean_state(reopened_editor,
                prefix + " fresh reopen");
            check(!reopened_sheet.has_pending_changes(),
                prefix + " fresh reopen should keep the materialized worksheet clean");
            check(reopened_sheet.cell_count() == 0,
                prefix + " fresh reopen should keep the sparse store empty");
            check(!reopened_sheet.used_range().has_value(),
                prefix + " fresh reopen should expose no used range");
            check(reopened_sheet.sparse_cells().empty(),
                prefix + " fresh reopen sparse_cells should be empty");
            check(reopened_sheet.row_cells(1).empty(),
                prefix + " fresh reopen row_cells should be empty");
            check(reopened_sheet.column_cells(1).empty(),
                prefix + " fresh reopen column_cells should be empty");
            check(!reopened_sheet.try_cell("A1").has_value(),
                prefix + " fresh reopen should keep erased A1 absent");
            check(!reopened_sheet.has_pending_changes(),
                prefix + " fresh reopen reads should leave the worksheet clean");
            check_workbook_editor_public_clean_state(reopened_editor,
                prefix + " fresh reopen reads");
        };

    const std::filesystem::path clear_source = write_formula_source(
        "fastxlsx-workbook-editor-public-whole-clear-formula-source-mutation-source.xlsx");
    const std::filesystem::path clear_output = artifact(
        "fastxlsx-workbook-editor-public-whole-clear-formula-source-mutation-output.xlsx");
    const std::filesystem::path clear_noop_output = artifact(
        "fastxlsx-workbook-editor-public-whole-clear-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path clear_reopened_output = artifact(
        "fastxlsx-workbook-editor-public-whole-clear-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path clear_reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-whole-clear-formula-source-mutation-reopened-noop-output.xlsx");
    const auto clear_source_entries = fastxlsx::test::read_zip_entries(clear_source);

    fastxlsx::WorkbookEditor clear_editor = fastxlsx::WorkbookEditor::open(clear_source);
    fastxlsx::WorksheetEditor clear_sheet = clear_editor.worksheet("Data", lossy_source_materialization_options());
    check(clear_sheet.cell_count() == 8,
        "whole-store formula clear setup should materialize all source records");
    check(clear_sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Formula
            && clear_sheet.get_cell("C1").text_value() == "B1+50",
        "whole-store formula clear setup should translate shared followers");
    check(clear_sheet.get_cell("E1").kind() == fastxlsx::CellValueKind::Number
            && clear_sheet.get_cell("E1").number_value() == 3005.0,
        "whole-store formula clear setup should materialize array cached fallback");
    check(clear_sheet.get_cell("F1").kind() == fastxlsx::CellValueKind::Formula
            && clear_sheet.get_cell("F1").text_value() == "A1+60",
        "whole-store formula clear setup should flatten dataTable formula text");
    check(!clear_sheet.has_pending_changes(),
        "whole-store formula clear setup should start clean");

    clear_sheet.clear_cell_values();
    check(clear_sheet.has_pending_changes(),
        "whole-store formula clear should dirty Data");
    check(clear_editor.has_pending_changes(),
        "whole-store formula clear should dirty WorkbookEditor");
    check(clear_sheet.cell_count() == 8,
        "whole-store formula clear should keep every source record represented");
    check(clear_sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Blank,
        "whole-store formula clear should blank ordinary formula records");
    check(clear_sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Blank,
        "whole-store formula clear should blank array formula records");
    check(clear_sheet.get_cell("F1").kind() == fastxlsx::CellValueKind::Blank,
        "whole-store formula clear should blank dataTable formula records");

    clear_editor.save_as(clear_output);
    check(!clear_sheet.has_pending_changes(),
        "whole-store formula clear save should keep Data clean");
    const auto clear_output_entries = fastxlsx::test::read_zip_entries(clear_output);
    const std::string& clear_worksheet_xml =
        clear_output_entries.at("xl/worksheets/sheet1.xml");
    for (const char column : std::string("ABCDEFGH")) {
        check_contains(clear_worksheet_xml,
            std::string("<c r=\"") + column + "1\"/>",
            "whole-store formula clear save should persist every source formula coordinate as blank");
    }
    check_no_formula_metadata(clear_worksheet_xml, "whole-store formula clear save");
    check(fastxlsx::test::read_zip_entries(clear_source) == clear_source_entries,
        "whole-store formula clear save should not mutate the source package");
    check(clear_output_entries.at("xl/worksheets/sheet2.xml")
            == clear_source_entries.at("xl/worksheets/sheet2.xml"),
        "whole-store formula clear save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell clear_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::blank()},
        {1, 2, fastxlsx::CellValue::blank()},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 4, fastxlsx::CellValue::blank()},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 6, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::blank()},
    };
    check_reopened_formula_dirty_output(
        clear_output,
        fastxlsx::CellRange {1, 1, 1, 8},
        clear_expected_cells,
        "whole-store formula clear output");

    clear_editor.save_as(clear_noop_output);
    check(!clear_sheet.has_pending_changes(),
        "whole-store formula clear no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(clear_noop_output) == clear_output_entries,
        "whole-store formula clear no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(clear_source) == clear_source_entries,
        "whole-store formula clear no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        clear_noop_output,
        fastxlsx::CellRange {1, 1, 1, 8},
        clear_expected_cells,
        "whole-store formula clear no-op output");

    fastxlsx::WorkbookEditor clear_reopened_editor =
        fastxlsx::WorkbookEditor::open(clear_noop_output);
    fastxlsx::WorksheetEditor clear_reopened_sheet =
        clear_reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!clear_reopened_sheet.has_pending_changes(),
        "whole-store formula clear fresh reopen should start clean");
    clear_reopened_sheet.set_cell("I2", fastxlsx::CellValue::formula("A1+H1"));
    clear_reopened_editor.save_as(clear_reopened_output);
    const auto clear_reopened_entries =
        fastxlsx::test::read_zip_entries(clear_reopened_output);
    const std::string& clear_reopened_xml =
        clear_reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(clear_reopened_xml, R"(<c r="I2"><f>A1+H1</f></c>)",
        "whole-store formula clear fresh-reopen save should include later formula edit");
    check_no_formula_metadata(
        clear_reopened_xml, "whole-store formula clear fresh-reopen save");
    check(fastxlsx::test::read_zip_entries(clear_noop_output) == clear_output_entries,
        "whole-store formula clear fresh-reopen save should not mutate its saved input");
    const ReopenedFormulaOutputCell clear_reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::blank()},
        {1, 2, fastxlsx::CellValue::blank()},
        {1, 3, fastxlsx::CellValue::blank()},
        {1, 4, fastxlsx::CellValue::blank()},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 6, fastxlsx::CellValue::blank()},
        {1, 7, fastxlsx::CellValue::blank()},
        {1, 8, fastxlsx::CellValue::blank()},
        {2, 9, fastxlsx::CellValue::formula("A1+H1")},
    };
    check_reopened_formula_dirty_output(
        clear_reopened_output,
        fastxlsx::CellRange {1, 1, 2, 9},
        clear_reopened_expected_cells,
        "whole-store formula clear fresh-reopen output");

    clear_reopened_editor.save_as(clear_reopened_noop_output);
    check(!clear_reopened_sheet.has_pending_changes(),
        "whole-store formula clear fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(clear_reopened_noop_output)
            == clear_reopened_entries,
        "whole-store formula clear fresh-reopen no-op save should keep output byte-stable");
    check_reopened_formula_dirty_output(
        clear_reopened_noop_output,
        fastxlsx::CellRange {1, 1, 2, 9},
        clear_reopened_expected_cells,
        "whole-store formula clear fresh-reopen no-op output");

    const std::filesystem::path erase_source = write_formula_source(
        "fastxlsx-workbook-editor-public-whole-erase-formula-source-mutation-source.xlsx");
    const std::filesystem::path erase_output = artifact(
        "fastxlsx-workbook-editor-public-whole-erase-formula-source-mutation-output.xlsx");
    const std::filesystem::path erase_noop_output = artifact(
        "fastxlsx-workbook-editor-public-whole-erase-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path erase_reopened_output = artifact(
        "fastxlsx-workbook-editor-public-whole-erase-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path erase_reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-whole-erase-formula-source-mutation-reopened-noop-output.xlsx");
    const auto erase_source_entries = fastxlsx::test::read_zip_entries(erase_source);

    fastxlsx::WorkbookEditor erase_editor = fastxlsx::WorkbookEditor::open(erase_source);
    fastxlsx::WorksheetEditor erase_sheet = erase_editor.worksheet("Data", lossy_source_materialization_options());
    check(erase_sheet.cell_count() == 8,
        "whole-store formula erase setup should materialize all source records");
    erase_sheet.erase_cells();
    check(erase_sheet.has_pending_changes(),
        "whole-store formula erase should dirty Data");
    check(erase_editor.has_pending_changes(),
        "whole-store formula erase should dirty WorkbookEditor");
    check(erase_sheet.cell_count() == 0,
        "whole-store formula erase should remove every source formula record");
    check(!erase_sheet.used_range().has_value(),
        "whole-store formula erase should clear the represented used range");
    check(!erase_sheet.try_cell("A1").has_value(),
        "whole-store formula erase should remove ordinary formula records");
    check(!erase_sheet.try_cell("F1").has_value(),
        "whole-store formula erase should remove dataTable formula records");

    erase_editor.save_as(erase_output);
    check(!erase_sheet.has_pending_changes(),
        "whole-store formula erase save should keep Data clean");
    const auto erase_output_entries = fastxlsx::test::read_zip_entries(erase_output);
    const std::string& erase_worksheet_xml =
        erase_output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(erase_worksheet_xml, "<c r=\"",
        "whole-store formula erase save should omit every source formula coordinate");
    check_contains(erase_worksheet_xml, "<sheetData",
        "whole-store formula erase save should keep a worksheet sheetData element");
    check_no_formula_metadata(erase_worksheet_xml, "whole-store formula erase save");
    check(fastxlsx::test::read_zip_entries(erase_source) == erase_source_entries,
        "whole-store formula erase save should not mutate the source package");
    check(erase_output_entries.at("xl/worksheets/sheet2.xml")
            == erase_source_entries.at("xl/worksheets/sheet2.xml"),
        "whole-store formula erase save should preserve untouched worksheets");
    check_reopened_empty_formula_output(
        erase_output, "whole-store formula erase output");

    erase_editor.save_as(erase_noop_output);
    check(!erase_sheet.has_pending_changes(),
        "whole-store formula erase no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_output_entries,
        "whole-store formula erase no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(erase_source) == erase_source_entries,
        "whole-store formula erase no-op save should not mutate the source package");
    check_reopened_empty_formula_output(
        erase_noop_output, "whole-store formula erase no-op output");

    fastxlsx::WorkbookEditor erase_reopened_editor =
        fastxlsx::WorkbookEditor::open(erase_noop_output);
    fastxlsx::WorksheetEditor erase_reopened_sheet =
        erase_reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!erase_reopened_sheet.has_pending_changes(),
        "whole-store formula erase fresh reopen should start clean");
    erase_reopened_sheet.set_cell("A1", fastxlsx::CellValue::formula("B1+1"));
    erase_reopened_editor.save_as(erase_reopened_output);
    const auto erase_reopened_entries =
        fastxlsx::test::read_zip_entries(erase_reopened_output);
    const std::string& erase_reopened_xml =
        erase_reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(erase_reopened_xml, R"(<c r="A1"><f>B1+1</f></c>)",
        "whole-store formula erase fresh-reopen save should include later formula edit");
    check_no_formula_metadata(
        erase_reopened_xml, "whole-store formula erase fresh-reopen save");
    check(fastxlsx::test::read_zip_entries(erase_noop_output) == erase_output_entries,
        "whole-store formula erase fresh-reopen save should not mutate its saved input");
    const ReopenedFormulaOutputCell erase_reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("B1+1")},
    };
    check_reopened_formula_dirty_output(
        erase_reopened_output,
        fastxlsx::CellRange {1, 1, 1, 1},
        erase_reopened_expected_cells,
        "whole-store formula erase fresh-reopen output");

    erase_reopened_editor.save_as(erase_reopened_noop_output);
    check(!erase_reopened_sheet.has_pending_changes(),
        "whole-store formula erase fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(erase_reopened_noop_output)
            == erase_reopened_entries,
        "whole-store formula erase fresh-reopen no-op save should keep output byte-stable");
    check_reopened_formula_dirty_output(
        erase_reopened_noop_output,
        fastxlsx::CellRange {1, 1, 1, 1},
        erase_reopened_expected_cells,
        "whole-store formula erase fresh-reopen no-op output");
}

void test_public_worksheet_editor_full_replacement_formula_source_mutations_drop_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-full-replacement-formula-source-mutation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-full-replacement-formula-source-mutation-output.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-full-replacement-formula-source-mutation-noop-output.xlsx");
    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-public-full-replacement-formula-source-mutation-reopened-output.xlsx");
    const std::filesystem::path reopened_noop_output = artifact(
        "fastxlsx-workbook-editor-public-full-replacement-formula-source-mutation-reopened-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f>A2+70</f><v>4001</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:C1" si="95" ca="1">A1+70</f><v>4002</v></c>)"
        R"(<c r="C1"><f t="shared" si="95" aca="1"/><v>4003</v></c>)"
        R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>4004</v></c>)"
        R"(<c r="E1"><f t="array" ref="D1:E1"/><v>4005</v></c>)"
        R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+80</f><v>4006</v></c>)"
        R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>4007</v></c>)"
        R"(<c r="H1"><f>G1+1</f><v>4008</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    const auto check_no_formula_metadata = [](const std::string& worksheet_xml,
                                              std::string_view scenario) {
        const std::string prefix(scenario);
        check_not_contains(worksheet_xml, R"(t="shared")",
            prefix + " should drop shared formula metadata");
        check_not_contains(worksheet_xml, R"(t="array")",
            prefix + " should drop array formula metadata");
        check_not_contains(worksheet_xml, R"(t="dataTable")",
            prefix + " should drop dataTable formula metadata");
        check_not_contains(worksheet_xml, R"(ca="1")",
            prefix + " should drop calc metadata attributes");
        check_not_contains(worksheet_xml, R"(aca="1")",
            prefix + " should drop shared follower metadata attributes");
        check_not_contains(worksheet_xml, R"(dt2D="1")",
            prefix + " should drop dataTable attributes");
        check_not_contains(worksheet_xml, R"(r1="A1")",
            prefix + " should drop dataTable input metadata");
        check_not_contains(worksheet_xml, "<f>A2+70</f>",
            prefix + " should not revive ordinary source formula text");
        check_not_contains(worksheet_xml, "A1+70",
            prefix + " should not revive shared source formula text");
        check_not_contains(worksheet_xml, "SUM(A1:B1)",
            prefix + " should not revive array formula text");
        check_not_contains(worksheet_xml, "<f>A1+80</f>",
            prefix + " should not revive dataTable formula text");
        check_not_contains(worksheet_xml, "<f>G1+1</f>",
            prefix + " should not revive overwritten source formula text");
        for (int stale_cached_value = 4001; stale_cached_value <= 4008; ++stale_cached_value) {
            check_not_contains(worksheet_xml,
                "<v>" + std::to_string(stale_cached_value) + "</v>",
                prefix + " should drop stale cached values");
        }
    };

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    check(sheet.cell_count() == 8,
        "full-replacement formula source mutation setup should materialize all source records");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("C1").text_value() == "B1+70",
        "full-replacement formula source mutation setup should translate shared followers");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("D1").text_value() == "SUM(A1:B1)",
        "full-replacement formula source mutation setup should flatten array formula text");
    check(sheet.get_cell("E1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("E1").number_value() == 4005.0,
        "full-replacement formula source mutation setup should materialize array cached fallback");
    check(sheet.get_cell("F1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("F1").text_value() == "A1+80",
        "full-replacement formula source mutation setup should flatten dataTable formula text");
    check(sheet.get_cell("G1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("G1").number_value() == 4007.0,
        "full-replacement formula source mutation setup should materialize dataTable cached fallback");
    check(!sheet.has_pending_changes(),
        "full-replacement formula source mutation setup should start clean");
    check(!editor.has_pending_changes(),
        "full-replacement formula source mutation setup should not dirty WorkbookEditor");

    sheet.set_row(1, {fastxlsx::CellValue::text("row-a1"),
        fastxlsx::CellValue::number(51.0),
        fastxlsx::CellValue::formula("A1+B1"),
        fastxlsx::CellValue::formula("A1+C1")});
    sheet.set_column(4, {fastxlsx::CellValue::formula("A1+C1"),
        fastxlsx::CellValue::text("column-d2"),
        fastxlsx::CellValue::number(73.0)});
    sheet.set_cells({
        {{1, 1}, fastxlsx::CellValue::text("batch-a1")},
        {{3, 4}, fastxlsx::CellValue::formula("D1+B1")},
    });
    sheet.append_row({
        fastxlsx::CellValue::text("append-a"),
        fastxlsx::CellValue::formula("A1+D1"),
    });

    check(sheet.has_pending_changes(),
        "full-replacement formula source mutations should dirty Data");
    check(editor.has_pending_changes(),
        "full-replacement formula source mutations should dirty WorkbookEditor");
    check(sheet.cell_count() == 8,
        "full-replacement formula source mutations should keep the final sparse count");
    check(!sheet.try_cell("E1").has_value(),
        "full-replacement formula source mutations should remove array fallback");
    check(!sheet.try_cell("F1").has_value(),
        "full-replacement formula source mutations should remove dataTable formula cell");
    check(!sheet.try_cell("G1").has_value(),
        "full-replacement formula source mutations should remove dataTable cached fallback");
    check(!sheet.try_cell("H1").has_value(),
        "full-replacement formula source mutations should remove tail source formula");
    check(sheet.get_cell("A1").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A1").text_value() == "batch-a1",
        "full-replacement formula source mutations should expose set_cells text overwrite");
    check(sheet.get_cell("B1").kind() == fastxlsx::CellValueKind::Number
            && sheet.get_cell("B1").number_value() == 51.0,
        "full-replacement formula source mutations should preserve set_row number overwrite");
    check(sheet.get_cell("C1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("C1").text_value() == "A1+B1",
        "full-replacement formula source mutations should preserve set_row formula overwrite");
    check(sheet.get_cell("D1").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("D1").text_value() == "A1+C1",
        "full-replacement formula source mutations should preserve set_column formula overwrite");
    check(sheet.get_cell("D2").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("D2").text_value() == "column-d2",
        "full-replacement formula source mutations should expose set_column inserted text");
    check(sheet.get_cell("D3").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("D3").text_value() == "D1+B1",
        "full-replacement formula source mutations should expose set_cells later formula overwrite");
    check(sheet.get_cell("A4").kind() == fastxlsx::CellValueKind::Text
            && sheet.get_cell("A4").text_value() == "append-a",
        "full-replacement formula source mutations should expose appended text");
    check(sheet.get_cell("B4").kind() == fastxlsx::CellValueKind::Formula
            && sheet.get_cell("B4").text_value() == "A1+D1",
        "full-replacement formula source mutations should expose appended formula");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "full-replacement formula source mutation save should keep Data clean");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>batch-a1</t></is></c>)",
        "full-replacement formula source mutation save should persist set_cells text overwrite");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>51</v></c>)",
        "full-replacement formula source mutation save should persist set_row numeric overwrite");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
        "full-replacement formula source mutation save should persist set_row formula overwrite");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>A1+C1</f></c>)",
        "full-replacement formula source mutation save should persist set_column formula overwrite");
    check_contains(output_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>column-d2</t></is></c>)",
        "full-replacement formula source mutation save should persist set_column inserted text");
    check_contains(output_worksheet_xml, R"(<c r="D3"><f>D1+B1</f></c>)",
        "full-replacement formula source mutation save should persist set_cells formula overwrite");
    check_contains(output_worksheet_xml,
        R"(<c r="A4" t="inlineStr"><is><t>append-a</t></is></c>)",
        "full-replacement formula source mutation save should persist appended text");
    check_contains(output_worksheet_xml, R"(<c r="B4"><f>A1+D1</f></c>)",
        "full-replacement formula source mutation save should persist appended formula");
    check_not_contains(output_worksheet_xml, "<c r=\"E1\"",
        "full-replacement formula source mutation save should omit removed array fallback");
    check_not_contains(output_worksheet_xml, "<c r=\"F1\"",
        "full-replacement formula source mutation save should omit removed dataTable formula");
    check_not_contains(output_worksheet_xml, "<c r=\"G1\"",
        "full-replacement formula source mutation save should omit removed dataTable fallback");
    check_not_contains(output_worksheet_xml, "<c r=\"H1\"",
        "full-replacement formula source mutation save should omit removed tail formula");
    check_no_formula_metadata(
        output_worksheet_xml, "full-replacement formula source mutation save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-replacement formula source mutation save should not mutate the source package");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "full-replacement formula source mutation save should preserve untouched worksheets");

    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("batch-a1")},
        {1, 2, fastxlsx::CellValue::number(51.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+B1")},
        {1, 4, fastxlsx::CellValue::formula("A1+C1")},
        {2, 4, fastxlsx::CellValue::text("column-d2")},
        {3, 4, fastxlsx::CellValue::formula("D1+B1")},
        {4, 1, fastxlsx::CellValue::text("append-a")},
        {4, 2, fastxlsx::CellValue::formula("A1+D1")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 4, 4},
        expected_cells,
        "full-replacement formula source mutation output");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-replacement formula source mutation no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "full-replacement formula source mutation no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-replacement formula source mutation no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        expected_cells,
        "full-replacement formula source mutation no-op output");

    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
    check(!reopened_sheet.has_pending_changes(),
        "full-replacement formula source mutation fresh reopen should start clean");
    reopened_sheet.set_cell("E5", fastxlsx::CellValue::formula("A4+B4"));
    reopened_editor.save_as(reopened_output);
    const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
    const std::string& reopened_worksheet_xml =
        reopened_entries.at("xl/worksheets/sheet1.xml");
    check_contains(reopened_worksheet_xml, R"(<c r="E5"><f>A4+B4</f></c>)",
        "full-replacement formula source mutation fresh-reopen save should include later formula edit");
    check_no_formula_metadata(
        reopened_worksheet_xml, "full-replacement formula source mutation fresh-reopen save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-replacement formula source mutation fresh-reopen save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "full-replacement formula source mutation fresh-reopen save should not mutate its saved input");

    const ReopenedFormulaOutputCell reopened_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("batch-a1")},
        {1, 2, fastxlsx::CellValue::number(51.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+B1")},
        {1, 4, fastxlsx::CellValue::formula("A1+C1")},
        {2, 4, fastxlsx::CellValue::text("column-d2")},
        {3, 4, fastxlsx::CellValue::formula("D1+B1")},
        {4, 1, fastxlsx::CellValue::text("append-a")},
        {4, 2, fastxlsx::CellValue::formula("A1+D1")},
        {5, 5, fastxlsx::CellValue::formula("A4+B4")},
    };
    check_reopened_formula_dirty_output(
        reopened_output,
        fastxlsx::CellRange {1, 1, 5, 5},
        reopened_expected_cells,
        "full-replacement formula source mutation fresh-reopen output");

    reopened_editor.save_as(reopened_noop_output);
    check(!reopened_sheet.has_pending_changes(),
        "full-replacement formula source mutation fresh-reopen no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(reopened_noop_output) == reopened_entries,
        "full-replacement formula source mutation fresh-reopen no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "full-replacement formula source mutation fresh-reopen no-op save should not mutate its saved input");
    check_reopened_formula_dirty_output(
        reopened_noop_output,
        fastxlsx::CellRange {1, 1, 5, 5},
        reopened_expected_cells,
        "full-replacement formula source mutation fresh-reopen no-op output");
}

void test_public_worksheet_editor_structural_shift_formula_source_mutations_drop_metadata()
{
    const auto write_insert_shift_source = [](std::string_view name, int stale_base) {
        const std::filesystem::path source = write_two_sheet_source(name);
        const auto stale_value = [stale_base](int offset) {
            return std::to_string(stale_base + offset);
        };
        const std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + R"(<sheetData><row r="1">)"
            + R"(<c r="A1"><f>A2+90</f><v>)" + stale_value(1) + R"(</v></c>)"
            + R"(<c r="B1"><f t="shared" ref="B1:C1" si="97" ca="1">A1+90</f><v>)"
            + stale_value(2) + R"(</v></c>)"
            + R"(<c r="C1"><f t="shared" si="97" aca="1"/><v>)" + stale_value(3)
            + R"(</v></c>)"
            + R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(A1:B1)</f><v>)"
            + stale_value(4) + R"(</v></c>)"
            + R"(<c r="E1"><f t="array" ref="D1:E1"/><v>)" + stale_value(5)
            + R"(</v></c>)"
            + R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">A1+100</f><v>)"
            + stale_value(6) + R"(</v></c>)"
            + R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>)"
            + stale_value(7) + R"(</v></c>)"
            + R"(<c r="H1"><f>SUM(G1:H1)+A:A+1:1+"G1"+Table1[G1]</f><v>)"
            + stale_value(8) + R"(</v></c>)"
            + R"(</row></sheetData></worksheet>)";
        rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
        return source;
    };

    const auto write_delete_rows_source = [](std::string_view name, int stale_base) {
        const std::filesystem::path source = write_two_sheet_source(name);
        const auto stale_value = [stale_base](int offset) {
            return std::to_string(stale_base + offset);
        };
        const std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + R"(<sheetData><row r="2">)"
            + R"(<c r="A2"><f>A4+90</f><v>)" + stale_value(1) + R"(</v></c>)"
            + R"(<c r="B2"><f t="shared" ref="B2:C2" si="98" ca="1">A4+90</f><v>)"
            + stale_value(2) + R"(</v></c>)"
            + R"(<c r="C2"><f t="shared" si="98" aca="1"/><v>)" + stale_value(3)
            + R"(</v></c>)"
            + R"(<c r="D2"><f t="array" ref="D2:E2" ca="1">SUM(A4:B4)</f><v>)"
            + stale_value(4) + R"(</v></c>)"
            + R"(<c r="E2"><f t="array" ref="D2:E2"/><v>)" + stale_value(5)
            + R"(</v></c>)"
            + R"(<c r="F2"><f t="dataTable" ref="F2:G2" dt2D="1" r1="A2">A4+100</f><v>)"
            + stale_value(6) + R"(</v></c>)"
            + R"(<c r="G2"><f t="dataTable" ref="F2:G2" ca="1"/><v>)"
            + stale_value(7) + R"(</v></c>)"
            + R"(<c r="H2"><f>SUM(G4:H4)+A:A+4:4+"G4"+Table1[G4]</f><v>)"
            + stale_value(8) + R"(</v></c>)"
            + R"(</row></sheetData></worksheet>)";
        rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
        return source;
    };

    const auto write_delete_columns_source = [](std::string_view name, int stale_base) {
        const std::filesystem::path source = write_two_sheet_source(name);
        const auto stale_value = [stale_base](int offset) {
            return std::to_string(stale_base + offset);
        };
        const std::string worksheet_xml =
            std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + R"(<sheetData><row r="1">)"
            + R"(<c r="A1"><f>D1+90</f><v>)" + stale_value(1) + R"(</v></c>)"
            + R"(<c r="B1"><f t="shared" ref="B1:C1" si="99" ca="1">D1+90</f><v>)"
            + stale_value(2) + R"(</v></c>)"
            + R"(<c r="C1"><f t="shared" si="99" aca="1"/><v>)" + stale_value(3)
            + R"(</v></c>)"
            + R"(<c r="D1"><f t="array" ref="D1:E1" ca="1">SUM(F1:G1)</f><v>)"
            + stale_value(4) + R"(</v></c>)"
            + R"(<c r="E1"><f t="array" ref="D1:E1"/><v>)" + stale_value(5)
            + R"(</v></c>)"
            + R"(<c r="F1"><f t="dataTable" ref="F1:G1" dt2D="1" r1="A1">G1+100</f><v>)"
            + stale_value(6) + R"(</v></c>)"
            + R"(<c r="G1"><f t="dataTable" ref="F1:G1" ca="1"/><v>)"
            + stale_value(7) + R"(</v></c>)"
            + R"(<c r="H1"><f>SUM(G1:H1)+D:E+1:1+"G1"+Table1[G1]</f><v>)"
            + stale_value(8) + R"(</v></c>)"
            + R"(</row></sheetData></worksheet>)";
        rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
        return source;
    };

    const auto check_no_formula_metadata =
        [](const std::string& worksheet_xml,
           std::initializer_list<int> omitted_cached_values,
           std::string_view scenario) {
            const std::string prefix(scenario);
            check_not_contains(worksheet_xml, R"(t="shared")",
                prefix + " should drop shared formula metadata");
            check_not_contains(worksheet_xml, R"(t="array")",
                prefix + " should drop array formula metadata");
            check_not_contains(worksheet_xml, R"(t="dataTable")",
                prefix + " should drop dataTable formula metadata");
            check_not_contains(worksheet_xml, R"(ca="1")",
                prefix + " should drop calc metadata attributes");
            check_not_contains(worksheet_xml, R"(aca="1")",
                prefix + " should drop shared follower metadata attributes");
            check_not_contains(worksheet_xml, R"(dt2D="1")",
                prefix + " should drop dataTable attributes");
            check_not_contains(worksheet_xml, R"(r1=")",
                prefix + " should drop dataTable input metadata");
            for (const int omitted_cached_value : omitted_cached_values) {
                check_not_contains(worksheet_xml,
                    "<v>" + std::to_string(omitted_cached_value) + "</v>",
                    prefix + " should drop stale cached values from shifted formula cells");
            }
        };

    const auto run_shift_case =
        [&](std::string_view artifact_suffix,
            std::string_view scenario,
            const std::filesystem::path& source,
            std::initializer_list<int> omitted_cached_values,
            auto mutate,
            const fastxlsx::CellRange& expected_range,
            std::span<const ReopenedFormulaOutputCell> expected_cells,
            const fastxlsx::CellRange& reopened_range,
            const ReopenedFormulaOutputCell& reopened_edit) {
            const std::string artifact_suffix_text(artifact_suffix);
            const std::string scenario_text(scenario);
            const std::filesystem::path output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-formula-source-mutation-"
                + artifact_suffix_text + "-output.xlsx");
            const std::filesystem::path noop_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-formula-source-mutation-"
                + artifact_suffix_text + "-noop-output.xlsx");
            const std::filesystem::path reopened_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-formula-source-mutation-"
                + artifact_suffix_text + "-reopened-output.xlsx");
            const std::filesystem::path reopened_noop_output = artifact(
                "fastxlsx-workbook-editor-public-structural-shift-formula-source-mutation-"
                + artifact_suffix_text + "-reopened-noop-output.xlsx");
            const auto source_entries = fastxlsx::test::read_zip_entries(source);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());
            check(sheet.cell_count() == 8,
                scenario_text + " setup should materialize all source formula records");
            check(!sheet.has_pending_changes(),
                scenario_text + " setup should start clean");
            check(!editor.has_pending_changes(),
                scenario_text + " setup should not dirty WorkbookEditor");

            mutate(sheet);

            check(sheet.has_pending_changes(),
                scenario_text + " should dirty Data");
            check(editor.has_pending_changes(),
                scenario_text + " should dirty WorkbookEditor");
            check(sheet.cell_count() == expected_cells.size(),
                scenario_text + " should expose the expected shifted sparse count");
            check_live_formula_shifted_cells(
                sheet, expected_range, expected_cells, scenario);
            check(sheet.has_pending_changes(),
                scenario_text + " live shifted reads should keep Data dirty");
            check(editor.has_pending_changes(),
                scenario_text + " live shifted reads should keep WorkbookEditor dirty");

            editor.save_as(output);
            check(!sheet.has_pending_changes(),
                scenario_text + " save should keep Data clean");
            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            const std::string& output_worksheet_xml =
                output_entries.at("xl/worksheets/sheet1.xml");
            check_no_formula_metadata(
                output_worksheet_xml, omitted_cached_values, scenario);
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " save should not mutate the source package");
            check(output_entries.at("xl/worksheets/sheet2.xml")
                    == source_entries.at("xl/worksheets/sheet2.xml"),
                scenario_text + " save should preserve untouched worksheets");
            check_reopened_formula_dirty_output(
                output, expected_range, expected_cells, scenario);

            editor.save_as(noop_output);
            check(!sheet.has_pending_changes(),
                scenario_text + " no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " no-op save should not mutate the source package");
            check_reopened_formula_dirty_output(
                noop_output, expected_range, expected_cells, scenario_text + " no-op output");

            fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(noop_output);
            fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data", lossy_source_materialization_options());
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh reopen should start clean");
            reopened_sheet.set_cell(
                reopened_edit.row, reopened_edit.column, reopened_edit.value);
            reopened_editor.save_as(reopened_output);
            const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
            const std::string& reopened_worksheet_xml =
                reopened_entries.at("xl/worksheets/sheet1.xml");
            check_no_formula_metadata(reopened_worksheet_xml,
                omitted_cached_values,
                scenario_text + " fresh-reopen save");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh-reopen save should not mutate its saved input");

            std::vector<ReopenedFormulaOutputCell> reopened_expected(
                expected_cells.begin(), expected_cells.end());
            reopened_expected.push_back(reopened_edit);
            check_reopened_formula_dirty_output(
                reopened_output,
                reopened_range,
                std::span<const ReopenedFormulaOutputCell>(
                    reopened_expected.data(), reopened_expected.size()),
                scenario_text + " fresh-reopen output");

            reopened_editor.save_as(reopened_noop_output);
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh-reopen no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(reopened_noop_output)
                    == reopened_entries,
                scenario_text + " fresh-reopen no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh-reopen no-op save should not mutate its saved input");
            check_reopened_formula_dirty_output(
                reopened_noop_output,
                reopened_range,
                std::span<const ReopenedFormulaOutputCell>(
                    reopened_expected.data(), reopened_expected.size()),
                scenario_text + " fresh-reopen no-op output");
        };

    const ReopenedFormulaOutputCell insert_rows_expected[] = {
        {2, 1, fastxlsx::CellValue::formula("A3+90")},
        {2, 2, fastxlsx::CellValue::formula("A2+90")},
        {2, 3, fastxlsx::CellValue::formula("B2+90")},
        {2, 4, fastxlsx::CellValue::formula("SUM(A2:B2)")},
        {2, 5, fastxlsx::CellValue::number(5005.0)},
        {2, 6, fastxlsx::CellValue::formula("A2+100")},
        {2, 7, fastxlsx::CellValue::number(5007.0)},
        {2, 8, fastxlsx::CellValue::formula(
            R"(SUM(G2:H2)+A:A+2:2+"G1"+Table1[G1])")},
    };
    run_shift_case("insert-rows",
        "structural insert_rows formula source mutation",
        write_insert_shift_source(
            "fastxlsx-workbook-editor-public-structural-insert-rows-formula-source-mutation-source.xlsx",
            5000),
        {5001, 5002, 5003, 5004, 5006, 5008},
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(1, 1); },
        fastxlsx::CellRange {2, 1, 2, 8},
        insert_rows_expected,
        fastxlsx::CellRange {2, 1, 3, 9},
        ReopenedFormulaOutputCell {3, 9, fastxlsx::CellValue::formula("A2+H2")});

    const ReopenedFormulaOutputCell insert_columns_expected[] = {
        {1, 2, fastxlsx::CellValue::formula("B2+90")},
        {1, 3, fastxlsx::CellValue::formula("B1+90")},
        {1, 4, fastxlsx::CellValue::formula("C1+90")},
        {1, 5, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 6, fastxlsx::CellValue::number(5105.0)},
        {1, 7, fastxlsx::CellValue::formula("B1+100")},
        {1, 8, fastxlsx::CellValue::number(5107.0)},
        {1, 9, fastxlsx::CellValue::formula(
            R"(SUM(H1:I1)+B:B+1:1+"G1"+Table1[G1])")},
    };
    run_shift_case("insert-columns",
        "structural insert_columns formula source mutation",
        write_insert_shift_source(
            "fastxlsx-workbook-editor-public-structural-insert-columns-formula-source-mutation-source.xlsx",
            5100),
        {5101, 5102, 5103, 5104, 5106, 5108},
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(1, 1); },
        fastxlsx::CellRange {1, 2, 1, 9},
        insert_columns_expected,
        fastxlsx::CellRange {1, 2, 2, 10},
        ReopenedFormulaOutputCell {2, 10, fastxlsx::CellValue::formula("B1+I1")});

    const ReopenedFormulaOutputCell delete_rows_expected[] = {
        {1, 1, fastxlsx::CellValue::formula("A3+90")},
        {1, 2, fastxlsx::CellValue::formula("A3+90")},
        {1, 3, fastxlsx::CellValue::formula("B3+90")},
        {1, 4, fastxlsx::CellValue::formula("SUM(A3:B3)")},
        {1, 5, fastxlsx::CellValue::number(5205.0)},
        {1, 6, fastxlsx::CellValue::formula("A3+100")},
        {1, 7, fastxlsx::CellValue::number(5207.0)},
        {1, 8, fastxlsx::CellValue::formula(
            R"(SUM(G3:H3)+A:A+3:3+"G4"+Table1[G4])")},
    };
    run_shift_case("delete-rows",
        "structural delete_rows formula source mutation",
        write_delete_rows_source(
            "fastxlsx-workbook-editor-public-structural-delete-rows-formula-source-mutation-source.xlsx",
            5200),
        {5201, 5202, 5203, 5204, 5206, 5208},
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(1, 1); },
        fastxlsx::CellRange {1, 1, 1, 8},
        delete_rows_expected,
        fastxlsx::CellRange {1, 1, 2, 9},
        ReopenedFormulaOutputCell {2, 9, fastxlsx::CellValue::formula("A1+H1")});

    const ReopenedFormulaOutputCell delete_columns_expected[] = {
        {1, 1, fastxlsx::CellValue::formula("C1+90")},
        {1, 2, fastxlsx::CellValue::formula("D1+90")},
        {1, 3, fastxlsx::CellValue::formula("SUM(E1:F1)")},
        {1, 4, fastxlsx::CellValue::number(5305.0)},
        {1, 5, fastxlsx::CellValue::formula("F1+100")},
        {1, 6, fastxlsx::CellValue::number(5307.0)},
        {1, 7, fastxlsx::CellValue::formula(
            R"(SUM(F1:G1)+C:D+1:1+"G1"+Table1[G1])")},
    };
    run_shift_case("delete-columns",
        "structural delete_columns formula source mutation",
        write_delete_columns_source(
            "fastxlsx-workbook-editor-public-structural-delete-columns-formula-source-mutation-source.xlsx",
            5300),
        {5301, 5302, 5303, 5304, 5306, 5308},
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(1, 1); },
        fastxlsx::CellRange {1, 1, 1, 7},
        delete_columns_expected,
        fastxlsx::CellRange {1, 1, 2, 8},
        ReopenedFormulaOutputCell {2, 8, fastxlsx::CellValue::formula("A1+G1")});
}

void test_public_worksheet_editor_materializes_source_shared_formulas()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-source-shared-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><f t="shared" ref="A1:B2" si="5" ca="1">A1+B$1+$A1+$A$1+SUM(A1:B1)&amp;"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1]</f><v>123</v></c>)"
        R"(</row>)"
        R"(<row r="2">)"
        R"(<c r="B2"><f t="shared" si="5" aca="1" bx="1"/><v>999</v></c>)"
        R"(</row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> base = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> follower = sheet.try_cell("B2");
    check(base.has_value() && base->kind() == fastxlsx::CellValueKind::Formula
            && base->text_value()
                == R"(A1+B$1+$A1+$A$1+SUM(A1:B1)&"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1])",
        "WorksheetEditor should materialize shared formula definitions as formula text");
    check(follower.has_value() && follower->kind() == fastxlsx::CellValueKind::Formula
            && follower->text_value()
                == R"(B2+C$1+$A2+$A$1+SUM(B2:C2)&"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1])",
        "WorksheetEditor should materialize shared formula followers with translated references");
    check(!sheet.has_pending_changes(),
        "source shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source shared formula read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "source shared formula read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source shared formula no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source shared formula no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source shared formula no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "source shared formula no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source shared formula no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            R"(A1+B$1+$A1+$A$1+SUM(A1:B1)&"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1])")},
        {2, 2, fastxlsx::CellValue::formula(
            R"(B2+C$1+$A2+$A$1+SUM(B2:C2)&"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1])")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        noop_cells,
        "shared formula no-op output");

    sheet.set_cell("C3", fastxlsx::CellValue::text("shared-formula-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="A1"><f>A1+B$1+$A1+$A$1+SUM(A1:B1)&amp;"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1]</f></c>)",
        "flushed WorksheetEditor shared formula base should write plain formula text");
    check_contains(output_worksheet_xml,
        R"(<c r="B2"><f>B2+C$1+$A2+$A$1+SUM(B2:C2)&amp;"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1]</f></c>)",
        "flushed WorksheetEditor shared formula follower should write translated formula text");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "flushed WorksheetEditor shared formula follower should not preserve stale cached values");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "flushed WorksheetEditor shared formulas should not preserve calc metadata attributes");
    check_not_contains(output_worksheet_xml, R"(aca="1")",
        "flushed WorksheetEditor shared formula followers should not preserve metadata attributes");
    check_contains(output_worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>shared-formula-new-inline</t></is></c>)",
        "flushed WorksheetEditor shared formula sheet should include later text edits");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "source shared formula rewrite should not mutate untouched source sheet bytes");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            R"(A1+B$1+$A1+$A$1+SUM(A1:B1)&"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1])")},
        {2, 2, fastxlsx::CellValue::formula(
            R"(B2+C$1+$A2+$A$1+SUM(B2:C2)&"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1])")},
        {3, 3, fastxlsx::CellValue::text("shared-formula-new-inline")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "shared formula dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source shared formula post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source shared formula post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source shared formula post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source shared formula post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "shared formula post-dirty no-op output");

    sheet.set_cell("D4", fastxlsx::CellValue::formula("A1&\"<shared>\"&B2"));
    check(sheet.has_pending_changes(),
        "source shared formula post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source shared formula post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source shared formula post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="D4"><f>A1&amp;"&lt;shared&gt;"&amp;B2</f></c>)",
        "source shared formula post-noop reuse save should include the later escaped formula edit");
    check_not_contains(post_noop_reuse_xml, R"(t="shared")",
        "source shared formula post-noop reuse save should keep shared metadata flattened");
    check_not_contains(post_noop_reuse_xml, "<v>999</v>",
        "source shared formula post-noop reuse save should keep stale cached values omitted");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source shared formula post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source shared formula post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "source shared formula post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source shared formula post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            R"(A1+B$1+$A1+$A$1+SUM(A1:B1)&"A1"+'Other Sheet'!A1+[Book.xlsx]Sheet1!A1+Table1[A1])")},
        {2, 2, fastxlsx::CellValue::formula(
            R"(B2+C$1+$A2+$A$1+SUM(B2:C2)&"A1"+'Other Sheet'!B2+[Book.xlsx]Sheet1!B2+Table1[A1])")},
        {3, 3, fastxlsx::CellValue::text("shared-formula-new-inline")},
        {4, 4, fastxlsx::CellValue::formula("A1&\"<shared>\"&B2")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "source shared formula post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source shared formula post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source shared formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source shared formula post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "source shared formula post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_source_order_shared_formula_matrix()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-source.xlsx");
    const std::filesystem::path noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-shared-formula-matrix-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-source-shared-formula-matrix-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1">)"
        R"(<c r="A1"><f t="shared" ref="A1:C2" si="1">A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)</f><v>1</v></c>)"
        R"(<c r="B1"><f t="shared" ref="B1:D1" si="2">C1+D$1+$C1+$C$1</f><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" si="1"/><v>777</v></c>)"
        R"(<c r="D1"><f t="shared" si="2"/><v>888</v></c>)"
        R"(</row>)"
        R"(<row r="2"><c r="A2"><f t="shared" si="1"/><v>999</v></c></row>)"
        R"(<row r="3"><c r="A3"><f t="shared" ref="A3:B3" si="1">Z3+1</f><v>3</v></c><c r="B3"><f t="shared" si="1"/><v>4</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const auto expect_formula = [&](std::string_view reference, std::string_view expected,
                                    std::string_view message) {
        const std::optional<fastxlsx::CellValue> value = sheet.try_cell(reference);
        check(value.has_value() && value->kind() == fastxlsx::CellValueKind::Formula
                && value->text_value() == expected,
            message);
    };
    expect_formula("C1",
        "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)",
        "WorksheetEditor should translate multiple shared formula followers without formula-name false positives");
    expect_formula("D1", "E1+F$1+$C1+$C$1",
        "WorksheetEditor should keep interleaved shared formula indexes independent");
    expect_formula("A2",
        "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)",
        "WorksheetEditor should translate row-offset shared formula followers");
    expect_formula("B3", "AA3+1",
        "WorksheetEditor should use the latest source-order shared formula definition for later followers");
    check(!sheet.has_pending_changes(),
        "source-order shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "source-order shared formula read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "source-order shared formula read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source-order shared formula matrix no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source-order shared formula matrix no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source-order shared formula matrix no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "source-order shared formula matrix no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source-order shared formula matrix no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            "A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)")},
        {1, 2, fastxlsx::CellValue::formula("C1+D$1+$C1+$C$1")},
        {1, 3, fastxlsx::CellValue::formula(
            "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)")},
        {1, 4, fastxlsx::CellValue::formula("E1+F$1+$C1+$C$1")},
        {2, 1, fastxlsx::CellValue::formula(
            "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)")},
        {3, 1, fastxlsx::CellValue::formula("Z3+1")},
        {3, 2, fastxlsx::CellValue::formula("AA3+1")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 3, 4},
        noop_cells,
        "source-order shared formula matrix no-op output");

    sheet.set_cell("E4", fastxlsx::CellValue::text("shared-formula-matrix-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml,
        R"(<c r="C1"><f>C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)</f></c>)",
        "flushed WorksheetEditor should write translated source-order shared formula C1 as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>E1+F$1+$C1+$C$1</f></c>)",
        "flushed WorksheetEditor should write interleaved shared formula D1 as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="B3"><f>AA3+1</f></c>)",
        "flushed WorksheetEditor should write latest-definition shared formula B3 as plain formula text");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "flushed WorksheetEditor should not preserve shared formula metadata");
    check_not_contains(output_worksheet_xml, "<v>999</v>",
        "flushed WorksheetEditor should drop stale shared formula cached values");
    check_contains(output_worksheet_xml,
        R"(<c r="E4" t="inlineStr"><is><t>shared-formula-matrix-edit</t></is></c>)",
        "flushed WorksheetEditor shared formula matrix should include later text edits");
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "source-order shared formula matrix should not mutate untouched source sheet bytes");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            "A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)")},
        {1, 2, fastxlsx::CellValue::formula("C1+D$1+$C1+$C$1")},
        {1, 3, fastxlsx::CellValue::formula(
            "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)")},
        {1, 4, fastxlsx::CellValue::formula("E1+F$1+$C1+$C$1")},
        {2, 1, fastxlsx::CellValue::formula(
            "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)")},
        {3, 1, fastxlsx::CellValue::formula("Z3+1")},
        {3, 2, fastxlsx::CellValue::formula("AA3+1")},
        {4, 5, fastxlsx::CellValue::text("shared-formula-matrix-edit")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 4, 5},
        expected_cells,
        "source-order shared formula matrix dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source-order shared formula matrix post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source-order shared formula matrix post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source-order shared formula matrix post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source-order shared formula matrix post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 4, 5},
        expected_cells,
        "source-order shared formula matrix post-dirty no-op output");

    sheet.set_cell("F5", fastxlsx::CellValue::formula("C1&D1+B3"));
    check(sheet.has_pending_changes(),
        "source-order shared formula matrix post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source-order shared formula matrix post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source-order shared formula matrix post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="F5"><f>C1&amp;D1+B3</f></c>)",
        "source-order shared formula matrix post-noop reuse save should include the later formula edit");
    check_not_contains(post_noop_reuse_xml, R"(t="shared")",
        "source-order shared formula matrix post-noop reuse save should keep shared metadata flattened");
    check_not_contains(post_noop_reuse_xml, "<v>999</v>",
        "source-order shared formula matrix post-noop reuse save should keep stale cached values omitted");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source-order shared formula matrix post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source-order shared formula matrix post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "source-order shared formula matrix post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source-order shared formula matrix post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::formula(
            "A1+Sheet1!A1+'O''Brien'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)+SUM(Sheet1!A:A)+SUM('Other Sheet'!1:1)+SUM($A:B)+SUM($1:2)")},
        {1, 2, fastxlsx::CellValue::formula("C1+D$1+$C1+$C$1")},
        {1, 3, fastxlsx::CellValue::formula(
            "C1+Sheet1!C1+'O''Brien'!C1+SUM(C1:D1)+LOG10(C1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(C:C)+SUM(1:1)+SUM(Sheet1!C:C)+SUM('Other Sheet'!1:1)+SUM($A:D)+SUM($1:2)")},
        {1, 4, fastxlsx::CellValue::formula("E1+F$1+$C1+$C$1")},
        {2, 1, fastxlsx::CellValue::formula(
            "A2+Sheet1!A2+'O''Brien'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)+SUM(Sheet1!A:A)+SUM('Other Sheet'!2:2)+SUM($A:B)+SUM($1:3)")},
        {3, 1, fastxlsx::CellValue::formula("Z3+1")},
        {3, 2, fastxlsx::CellValue::formula("AA3+1")},
        {4, 5, fastxlsx::CellValue::text("shared-formula-matrix-edit")},
        {5, 6, fastxlsx::CellValue::formula("C1&D1+B3")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 5, 6},
        post_noop_reuse_cells,
        "source-order shared formula matrix post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source-order shared formula matrix post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source-order shared formula matrix post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source-order shared formula matrix post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 5, 6},
        post_noop_reuse_cells,
        "source-order shared formula matrix post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_office_like_shared_formula_shape()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-office-like-shared-formula-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-office-like-shared-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:H6"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" ref="C1:D3" si="40">A1+B1</f><v>9901</v></c>)"
        R"(<c r="D1"><f t="shared" si="40"/><v>9902</v></c>)"
        R"(<c r="E1"><f>A1*2</f><v>9903</v></c></row>)"
        R"(<row r="2"><c r="A2"><v>10</v></c><c r="B2"><v>20</v></c>)"
        R"(<c r="C2"><f t="shared" si="40"/><v>9904</v></c>)"
        R"(<c r="D2"><f t="shared" si="40"/><v>9905</v></c>)"
        R"(<c r="E2" t="inlineStr"><is><t>between-shared-groups</t></is></c>)"
        R"(<c r="F2"><f t="shared" ref="F2:G3" si="41">SUM($A2:B2)+C$1</f><v>9906</v></c>)"
        R"(<c r="G2"><f t="shared" si="41"/><v>9907</v></c></row>)"
        R"(<row r="3"><c r="A3"><v>100</v></c><c r="B3"><v>200</v></c>)"
        R"(<c r="C3"><f t="shared" si="40"/><v>9908</v></c>)"
        R"(<c r="D3"><f t="shared" si="40"/><v>9909</v></c>)"
        R"(<c r="F3"><f t="shared" si="41"/><v>9910</v></c>)"
        R"(<c r="G3"><f t="shared" si="41"/><v>9911</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const auto expect_formula = [&](std::string_view reference, std::string_view expected,
                                    std::string_view message) {
        const std::optional<fastxlsx::CellValue> value = sheet.try_cell(reference);
        check(value.has_value() && value->kind() == fastxlsx::CellValueKind::Formula
                && value->text_value() == expected,
            message);
    };
    expect_formula("C1", "A1+B1",
        "WorksheetEditor should materialize the first 2D shared formula definition");
    expect_formula("D1", "B1+C1",
        "WorksheetEditor should translate a same-row 2D shared formula follower");
    expect_formula("C2", "A2+B2",
        "WorksheetEditor should translate a same-column 2D shared formula follower");
    expect_formula("D3", "B3+C3",
        "WorksheetEditor should translate a diagonal 2D shared formula follower");
    expect_formula("E1", "A1*2",
        "WorksheetEditor should preserve ordinary formulas interleaved with shared formulas");
    expect_formula("F2", "SUM($A2:B2)+C$1",
        "WorksheetEditor should materialize the second shared formula definition");
    expect_formula("G2", "SUM($A2:C2)+D$1",
        "WorksheetEditor should translate column-offset followers in the second shared formula group");
    expect_formula("G3", "SUM($A3:C3)+D$1",
        "WorksheetEditor should translate row and column offsets in the second shared formula group");
    check(!sheet.has_pending_changes(),
        "office-like shared formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "office-like shared formula read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "office-like shared formula read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "office-like shared formula no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "office-like shared formula no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "office-like shared formula no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "office-like shared formula no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "office-like shared formula no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::number(1.0)},
        {1, 2, fastxlsx::CellValue::number(2.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+B1")},
        {1, 4, fastxlsx::CellValue::formula("B1+C1")},
        {1, 5, fastxlsx::CellValue::formula("A1*2")},
        {2, 1, fastxlsx::CellValue::number(10.0)},
        {2, 2, fastxlsx::CellValue::number(20.0)},
        {2, 3, fastxlsx::CellValue::formula("A2+B2")},
        {2, 4, fastxlsx::CellValue::formula("B2+C2")},
        {2, 5, fastxlsx::CellValue::text("between-shared-groups")},
        {2, 6, fastxlsx::CellValue::formula("SUM($A2:B2)+C$1")},
        {2, 7, fastxlsx::CellValue::formula("SUM($A2:C2)+D$1")},
        {3, 1, fastxlsx::CellValue::number(100.0)},
        {3, 2, fastxlsx::CellValue::number(200.0)},
        {3, 3, fastxlsx::CellValue::formula("A3+B3")},
        {3, 4, fastxlsx::CellValue::formula("B3+C3")},
        {3, 6, fastxlsx::CellValue::formula("SUM($A3:B3)+C$1")},
        {3, 7, fastxlsx::CellValue::formula("SUM($A3:C3)+D$1")},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 3, 7},
        noop_cells,
        "office-like shared formula no-op output");

    sheet.set_cell("H6", fastxlsx::CellValue::text("office-like-shared-formula-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1+B1</f></c>)",
        "flushed WorksheetEditor should write 2D shared formula definition as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D1"><f>B1+C1</f></c>)",
        "flushed WorksheetEditor should write same-row 2D follower as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="D3"><f>B3+C3</f></c>)",
        "flushed WorksheetEditor should write diagonal 2D follower as plain formula text");
    check_contains(output_worksheet_xml, R"(<c r="G3"><f>SUM($A3:C3)+D$1</f></c>)",
        "flushed WorksheetEditor should write the second shared formula group follower");
    check_contains(output_worksheet_xml,
        R"(<c r="H6" t="inlineStr"><is><t>office-like-shared-formula-edit</t></is></c>)",
        "flushed WorksheetEditor office-like shared formula sheet should include later text edits");
    check_not_contains(output_worksheet_xml, R"(t="shared")",
        "flushed WorksheetEditor office-like shared formula sheet should not preserve shared metadata");
    for (int stale_value = 9901; stale_value <= 9911; ++stale_value) {
        check_not_contains(output_worksheet_xml, "<v>" + std::to_string(stale_value) + "</v>",
            "flushed WorksheetEditor office-like shared formula sheet should drop stale cached values");
    }
    check(fastxlsx::test::read_zip_entries(source).at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "office-like shared formula rewrite should not mutate untouched source sheet bytes");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(1.0)},
        {1, 2, fastxlsx::CellValue::number(2.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+B1")},
        {1, 4, fastxlsx::CellValue::formula("B1+C1")},
        {1, 5, fastxlsx::CellValue::formula("A1*2")},
        {2, 1, fastxlsx::CellValue::number(10.0)},
        {2, 2, fastxlsx::CellValue::number(20.0)},
        {2, 3, fastxlsx::CellValue::formula("A2+B2")},
        {2, 4, fastxlsx::CellValue::formula("B2+C2")},
        {2, 5, fastxlsx::CellValue::text("between-shared-groups")},
        {2, 6, fastxlsx::CellValue::formula("SUM($A2:B2)+C$1")},
        {2, 7, fastxlsx::CellValue::formula("SUM($A2:C2)+D$1")},
        {3, 1, fastxlsx::CellValue::number(100.0)},
        {3, 2, fastxlsx::CellValue::number(200.0)},
        {3, 3, fastxlsx::CellValue::formula("A3+B3")},
        {3, 4, fastxlsx::CellValue::formula("B3+C3")},
        {3, 6, fastxlsx::CellValue::formula("SUM($A3:B3)+C$1")},
        {3, 7, fastxlsx::CellValue::formula("SUM($A3:C3)+D$1")},
        {6, 8, fastxlsx::CellValue::text("office-like-shared-formula-edit")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 6, 8},
        expected_cells,
        "office-like shared formula dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "office-like shared formula post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "office-like shared formula post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "office-like shared formula post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "office-like shared formula post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 6, 8},
        expected_cells,
        "office-like shared formula post-dirty no-op output");

    sheet.set_cell("I7", fastxlsx::CellValue::formula("C1+G3+E1"));
    check(sheet.has_pending_changes(),
        "office-like shared formula post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "office-like shared formula post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "office-like shared formula post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="I7"><f>C1+G3+E1</f></c>)",
        "office-like shared formula post-noop reuse save should include the later formula edit");
    check_not_contains(post_noop_reuse_xml, R"(t="shared")",
        "office-like shared formula post-noop reuse save should keep shared metadata flattened");
    for (int stale_value = 9901; stale_value <= 9911; ++stale_value) {
        check_not_contains(post_noop_reuse_xml, "<v>" + std::to_string(stale_value) + "</v>",
            "office-like shared formula post-noop reuse save should keep stale cached values omitted");
    }
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "office-like shared formula post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "office-like shared formula post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "office-like shared formula post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "office-like shared formula post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(1.0)},
        {1, 2, fastxlsx::CellValue::number(2.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+B1")},
        {1, 4, fastxlsx::CellValue::formula("B1+C1")},
        {1, 5, fastxlsx::CellValue::formula("A1*2")},
        {2, 1, fastxlsx::CellValue::number(10.0)},
        {2, 2, fastxlsx::CellValue::number(20.0)},
        {2, 3, fastxlsx::CellValue::formula("A2+B2")},
        {2, 4, fastxlsx::CellValue::formula("B2+C2")},
        {2, 5, fastxlsx::CellValue::text("between-shared-groups")},
        {2, 6, fastxlsx::CellValue::formula("SUM($A2:B2)+C$1")},
        {2, 7, fastxlsx::CellValue::formula("SUM($A2:C2)+D$1")},
        {3, 1, fastxlsx::CellValue::number(100.0)},
        {3, 2, fastxlsx::CellValue::number(200.0)},
        {3, 3, fastxlsx::CellValue::formula("A3+B3")},
        {3, 4, fastxlsx::CellValue::formula("B3+C3")},
        {3, 6, fastxlsx::CellValue::formula("SUM($A3:B3)+C$1")},
        {3, 7, fastxlsx::CellValue::formula("SUM($A3:C3)+D$1")},
        {6, 8, fastxlsx::CellValue::text("office-like-shared-formula-edit")},
        {7, 9, fastxlsx::CellValue::formula("C1+G3+E1")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 7, 9},
        post_noop_reuse_cells,
        "office-like shared formula post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "office-like shared formula post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "office-like shared formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "office-like shared formula post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 7, 9},
        post_noop_reuse_cells,
        "office-like shared formula post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_array_and_datatable_formula_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-array-datatable-formula-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-array-datatable-formula-post-noop-reuse-noop-output.xlsx");

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData><row r="1">)"
        R"(<c r="A1"><f t="array" ref="A1:B1" aca="1" ca="1" bx="1">SUM(B1:C1)</f><v>123</v></c>)"
        R"(<c r="B1"><f t="array" ref="A1:B1"/><v>456</v></c>)"
        R"(<c r="C1"><f t="dataTable" ref="C1:D1" dt2D="1" dtr="1" del1="0" del2="0" r1="A1" r2="B1">A1+1</f><v>789</v></c>)"
        R"(<c r="D1"><f t="dataTable" ref="C1:D1" ca="1"/><v>321</v></c>)"
        R"(</row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", lossy_source_materialization_options());

    const std::optional<fastxlsx::CellValue> array_formula = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> array_cached = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> datatable_formula = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> datatable_cached = sheet.try_cell("D1");
    check(array_formula.has_value() && array_formula->kind() == fastxlsx::CellValueKind::Formula
            && array_formula->text_value() == "SUM(B1:C1)",
        "WorksheetEditor should flatten source array formula text to plain formula");
    check(array_cached.has_value() && array_cached->kind() == fastxlsx::CellValueKind::Number
            && array_cached->number_value() == 456.0,
        "WorksheetEditor should use cached scalar fallback for metadata-only array formulas");
    check(datatable_formula.has_value()
            && datatable_formula->kind() == fastxlsx::CellValueKind::Formula
            && datatable_formula->text_value() == "A1+1",
        "WorksheetEditor should flatten source dataTable formula text to plain formula");
    check(datatable_cached.has_value() && datatable_cached->kind() == fastxlsx::CellValueKind::Number
            && datatable_cached->number_value() == 321.0,
        "WorksheetEditor should use cached scalar fallback for metadata-only dataTable formulas");
    check(!sheet.has_pending_changes(),
        "array/dataTable formula read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "array/dataTable formula read-only materialization should not dirty the workbook editor");
    check(editor.pending_change_count() == 0,
        "array/dataTable formula read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "array/dataTable formula no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "array/dataTable formula no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "array/dataTable formula no-op save should not queue Patch edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "array/dataTable formula no-op save should copy source package bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "array/dataTable formula no-op save should not mutate the source package");
    const ReopenedFormulaOutputCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 2, fastxlsx::CellValue::number(456.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+1")},
        {1, 4, fastxlsx::CellValue::number(321.0)},
    };
    check_reopened_formula_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 4},
        noop_cells,
        "array/dataTable formula no-op output");

    sheet.set_cell("F2", fastxlsx::CellValue::text("array-datatable-edit"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(output_worksheet_xml, R"(<c r="A1"><f>SUM(B1:C1)</f></c>)",
        "dirty projection should write array formula text as plain formula");
    check_contains(output_worksheet_xml, R"(<c r="B1"><v>456</v></c>)",
        "dirty projection should retain array metadata-only cached scalar fallback");
    check_contains(output_worksheet_xml, R"(<c r="C1"><f>A1+1</f></c>)",
        "dirty projection should write dataTable formula text as plain formula");
    check_contains(output_worksheet_xml, R"(<c r="D1"><v>321</v></c>)",
        "dirty projection should retain dataTable metadata-only cached scalar fallback");
    check_contains(output_worksheet_xml,
        R"(<c r="F2" t="inlineStr"><is><t>array-datatable-edit</t></is></c>)",
        "dirty projection should include later edits after array/dataTable materialization");
    check_not_contains(output_worksheet_xml, R"(t="array")",
        "dirty projection should not preserve array formula metadata");
    check_not_contains(output_worksheet_xml, R"(t="dataTable")",
        "dirty projection should not preserve dataTable formula metadata");
    check_not_contains(output_worksheet_xml, R"(ca="1")",
        "dirty projection should not preserve known formula calculation metadata");
    check_not_contains(output_worksheet_xml, R"(dt2D="1")",
        "dirty projection should not preserve dataTable formula attributes");
    check_not_contains(output_worksheet_xml, "<v>123</v>",
        "dirty projection should drop stale array formula cached values");
    check_not_contains(output_worksheet_xml, "<v>789</v>",
        "dirty projection should drop stale dataTable formula cached values");
    const ReopenedFormulaOutputCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 2, fastxlsx::CellValue::number(456.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+1")},
        {1, 4, fastxlsx::CellValue::number(321.0)},
        {2, 6, fastxlsx::CellValue::text("array-datatable-edit")},
    };
    check_reopened_formula_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 6},
        expected_cells,
        "array/dataTable formula dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "array/dataTable formula post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "array/dataTable formula post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "array/dataTable formula post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "array/dataTable formula post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 6},
        expected_cells,
        "array/dataTable formula post-dirty no-op output");

    sheet.set_cell("G3", fastxlsx::CellValue::formula("A1+C1+F2"));
    check(sheet.has_pending_changes(),
        "array/dataTable formula post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "array/dataTable formula post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "array/dataTable formula post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string& post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml,
        R"(<c r="G3"><f>A1+C1+F2</f></c>)",
        "array/dataTable formula post-noop reuse save should include the later formula edit");
    check_not_contains(post_noop_reuse_xml, R"(t="array")",
        "array/dataTable formula post-noop reuse save should keep array metadata flattened");
    check_not_contains(post_noop_reuse_xml, R"(t="dataTable")",
        "array/dataTable formula post-noop reuse save should keep dataTable metadata flattened");
    check_not_contains(post_noop_reuse_xml, R"(ca="1")",
        "array/dataTable formula post-noop reuse save should keep formula calc metadata omitted");
    check_not_contains(post_noop_reuse_xml, R"(dt2D="1")",
        "array/dataTable formula post-noop reuse save should keep dataTable attributes omitted");
    check_not_contains(post_noop_reuse_xml, "<v>123</v>",
        "array/dataTable formula post-noop reuse save should keep stale array cached value omitted");
    check_not_contains(post_noop_reuse_xml, "<v>789</v>",
        "array/dataTable formula post-noop reuse save should keep stale dataTable cached value omitted");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "array/dataTable formula post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "array/dataTable formula post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "array/dataTable formula post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "array/dataTable formula post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedFormulaOutputCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 2, fastxlsx::CellValue::number(456.0)},
        {1, 3, fastxlsx::CellValue::formula("A1+1")},
        {1, 4, fastxlsx::CellValue::number(321.0)},
        {2, 6, fastxlsx::CellValue::text("array-datatable-edit")},
        {3, 7, fastxlsx::CellValue::formula("A1+C1+F2")},
    };
    check_reopened_formula_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 7},
        post_noop_reuse_cells,
        "array/dataTable formula post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "array/dataTable formula post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "array/dataTable formula post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "array/dataTable formula post-noop reuse no-op save should not mutate the source package");
    check_reopened_formula_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 7},
        post_noop_reuse_cells,
        "array/dataTable formula post-noop reuse no-op output");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_formulas();
        test_public_worksheet_editor_materializes_source_error_cells();
        test_public_worksheet_editor_structural_shift_source_error_cells();
        test_public_worksheet_editor_ignores_formula_cached_result_types();
        test_public_worksheet_editor_materializes_unresolved_shared_formula_cached_scalars();
        test_public_worksheet_editor_direct_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_batch_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_row_column_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_whole_store_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_full_replacement_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_structural_shift_formula_source_mutations_drop_metadata();
        test_public_worksheet_editor_materializes_source_shared_formulas();
        test_public_worksheet_editor_materializes_source_order_shared_formula_matrix();
        test_public_worksheet_editor_materializes_office_like_shared_formula_shape();
        test_public_worksheet_editor_materializes_array_and_datatable_formula_metadata();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success formulas check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success formulas tests passed\n");
    return 0;
}
