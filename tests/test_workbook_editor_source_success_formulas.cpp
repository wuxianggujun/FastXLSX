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

void check_reopened_formula_row_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario)
{
    std::vector<std::uint32_t> checked_rows;
    const std::string prefix(scenario);

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
            prefix + " fresh reopen row_cells should expose the expected row count");
        if (row_cells.size() != expected_count) {
            continue;
        }

        std::size_t index = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.row != expected.row) {
                continue;
            }
            check(formula_output_snapshot_matches(row_cells[index], candidate),
                prefix + " fresh reopen row_cells should preserve row-major values");
            ++index;
        }
    }
}

void check_reopened_formula_column_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedFormulaOutputCell> expected_cells,
    std::string_view scenario)
{
    std::vector<std::uint32_t> checked_columns;
    const std::string prefix(scenario);

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
            prefix + " fresh reopen column_cells should expose the expected column count");
        if (column_cells.size() != expected_count) {
            continue;
        }

        std::size_t index = 0;
        for (const ReopenedFormulaOutputCell& candidate : expected_cells) {
            if (candidate.column != expected.column) {
                continue;
            }
            check(formula_output_snapshot_matches(column_cells[index], candidate),
                prefix + " fresh reopen column_cells should preserve row-major values");
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
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data");

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

    check_reopened_formula_row_snapshots(reopened_sheet, expected_cells, scenario);
    check_reopened_formula_column_snapshots(reopened_sheet, expected_cells, scenario);

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
}

void test_public_worksheet_editor_materializes_office_like_shared_formula_shape()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-office-like-shared-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-office-like-shared-formula-dirty-noop-output.xlsx");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 6, 8},
        expected_cells,
        "office-like shared formula post-dirty no-op output");
}

void test_public_worksheet_editor_materializes_array_and_datatable_formula_metadata()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-public-array-datatable-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-array-datatable-formula-dirty-noop-output.xlsx");

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
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

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
    check_reopened_formula_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 6},
        expected_cells,
        "array/dataTable formula post-dirty no-op output");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_formulas();
        test_public_worksheet_editor_materializes_source_error_cells();
        test_public_worksheet_editor_ignores_formula_cached_result_types();
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
