#include "test_workbook_editor_source_success_common.hpp"

struct ReopenedSourceSuccessCell {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    fastxlsx::CellValue value;
};

bool source_success_values_equal(
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

bool source_success_snapshot_matches(
    const fastxlsx::WorksheetCellSnapshot& actual,
    const ReopenedSourceSuccessCell& expected)
{
    return actual.reference.row == expected.row &&
        actual.reference.column == expected.column &&
        source_success_values_equal(actual.value, expected.value);
}

void check_reopened_source_success_row_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedSourceSuccessCell> expected_cells,
    std::string_view scenario)
{
    std::vector<std::uint32_t> checked_rows;
    const std::string prefix(scenario);

    for (const ReopenedSourceSuccessCell& expected : expected_cells) {
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
        for (const ReopenedSourceSuccessCell& candidate : expected_cells) {
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
        for (const ReopenedSourceSuccessCell& candidate : expected_cells) {
            if (candidate.row != expected.row) {
                continue;
            }
            check(source_success_snapshot_matches(row_cells[index], candidate),
                prefix + " fresh reopen row_cells should preserve row-major values");
            ++index;
        }
    }
}

void check_reopened_source_success_column_snapshots(
    fastxlsx::WorksheetEditor& reopened_sheet,
    std::span<const ReopenedSourceSuccessCell> expected_cells,
    std::string_view scenario)
{
    std::vector<std::uint32_t> checked_columns;
    const std::string prefix(scenario);

    for (const ReopenedSourceSuccessCell& expected : expected_cells) {
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
        for (const ReopenedSourceSuccessCell& candidate : expected_cells) {
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
        for (const ReopenedSourceSuccessCell& candidate : expected_cells) {
            if (candidate.column != expected.column) {
                continue;
            }
            check(source_success_snapshot_matches(column_cells[index], candidate),
                prefix + " fresh reopen column_cells should preserve row-major values");
            ++index;
        }
    }
}

void check_reopened_source_success_dirty_output(
    const std::filesystem::path& output,
    const fastxlsx::CellRange& expected_range,
    std::span<const ReopenedSourceSuccessCell> expected_cells,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened_editor.worksheet("Data");

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
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
            check(source_success_snapshot_matches(actual_cells[index], expected_cells[index]),
                prefix + " fresh reopen sparse_cells should preserve row-major values");
        }
    }

    check_reopened_source_success_row_snapshots(
        reopened_sheet, expected_cells, scenario);
    check_reopened_source_success_column_snapshots(
        reopened_sheet, expected_cells, scenario);

    for (const ReopenedSourceSuccessCell& expected : expected_cells) {
        const fastxlsx::CellValue actual =
            reopened_sheet.get_cell(expected.row, expected.column);
        check(source_success_values_equal(actual, expected.value),
            prefix + " fresh reopen should read each expected cell directly");
    }

    check(!reopened_sheet.has_pending_changes(),
        prefix + " fresh reopen reads should leave the worksheet clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen reads");
}

void test_public_worksheet_editor_materializes_source_supported_values()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-supported-values-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1"/>)"
          R"(<c r="B1" t="b"><v>1</v></c>)"
          R"(<c r="C1" t="b"><v>0</v></c>)"
          R"(<c r="D1" t="inlineStr"><is><t></t></is></c>)"
          R"(<c r="E1" t="inlineStr"><is/></c>)"
          R"(<c r="F1"><f t="array" ref="F1">SUM(B1:C1)</f><v>1</v></c>)"
          R"(<c r="G1"><f t="shared" si="0"/><v>7</v></c>)"
          R"(<c r="H1" ph="1"><v>8</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    const std::optional<fastxlsx::CellValue> e1 = sheet.try_cell("E1");
    const std::optional<fastxlsx::CellValue> f1 = sheet.try_cell("F1");
    const std::optional<fastxlsx::CellValue> g1 = sheet.try_cell("G1");
    const std::optional<fastxlsx::CellValue> h1 = sheet.try_cell("H1");
    check(sheet.cell_count() == 8,
        "WorksheetEditor should count source blank and boolean cells as sparse records");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Blank,
        "WorksheetEditor should materialize self-closing source cells as explicit blank");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Boolean
            && b1->boolean_value(),
        "WorksheetEditor should materialize source boolean true");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Boolean
            && !c1->boolean_value(),
        "WorksheetEditor should materialize source boolean false");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Text
            && d1->text_value().empty(),
        "WorksheetEditor should materialize empty source inline text as empty text");
    check(e1.has_value() && e1->kind() == fastxlsx::CellValueKind::Blank,
        "WorksheetEditor should materialize inline string cells without text as blank");
    check(f1.has_value() && f1->kind() == fastxlsx::CellValueKind::Formula
            && f1->text_value() == "SUM(B1:C1)",
        "WorksheetEditor should flatten source formula metadata when formula text is present");
    check(g1.has_value() && g1->kind() == fastxlsx::CellValueKind::Number
            && g1->number_value() == 7.0,
        "WorksheetEditor should materialize cached values for metadata-only source formulas");
    check(h1.has_value() && h1->kind() == fastxlsx::CellValueKind::Number
            && h1->number_value() == 8.0,
        "WorksheetEditor should ignore source phonetic cell metadata");
    check(!sheet.has_pending_changes(),
        "read-only supported source value materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only supported source value materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only supported source value materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after supported source value materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after supported source value materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after supported source value materialization should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after supported source value materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after supported source value materialization should not mutate source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::blank()},
        {1, 2, fastxlsx::CellValue::boolean(true)},
        {1, 3, fastxlsx::CellValue::boolean(false)},
        {1, 4, fastxlsx::CellValue::text("")},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 6, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 7, fastxlsx::CellValue::number(7.0)},
        {1, 8, fastxlsx::CellValue::number(8.0)},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 8},
        noop_cells,
        "supported source values no-op output");

    sheet.set_cell("I2", fastxlsx::CellValue::text("supported-values-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:I2"/>)",
        "flushed supported source values should contribute to projected dimension");
    check_contains(worksheet_xml, R"(<c r="A1"/>)",
        "source blank should be projected as an explicit blank cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="b"><v>1</v></c>)",
        "source boolean true should be projected as a boolean cell");
    check_contains(worksheet_xml, R"(<c r="C1" t="b"><v>0</v></c>)",
        "source boolean false should be projected as a boolean cell");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t></t></is></c>)",
        "empty source inline text should remain an explicit empty text cell");
    check_contains(worksheet_xml, R"(<c r="E1"/>)",
        "source inline string without text should be projected as blank");
    check_contains(worksheet_xml, R"(<c r="F1"><f>SUM(B1:C1)</f></c>)",
        "source formula metadata should be projected as plain formula text");
    check_contains(worksheet_xml, R"(<c r="G1"><v>7</v></c>)",
        "metadata-only source formulas should be projected as cached scalar values");
    check_contains(worksheet_xml, R"(<c r="H1"><v>8</v></c>)",
        "source phonetic cell metadata should not be projected");
    check_contains(worksheet_xml,
        R"(<c r="I2" t="inlineStr"><is><t>supported-values-new-inline</t></is></c>)",
        "new WorksheetEditor text should continue to write inline strings");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty supported source value projection should not introduce shared string indexes");
    check(output_entries.find("xl/sharedStrings.xml") == output_entries.end(),
        "dirty supported source value projection should not create a sharedStrings part");
    check_not_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "dirty supported source value projection should not create a sharedStrings relationship");
    check_not_contains(output_entries.at("[Content_Types].xml"),
        "spreadsheetml.sharedStrings+xml",
        "dirty supported source value projection should not create a sharedStrings content type");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::blank()},
        {1, 2, fastxlsx::CellValue::boolean(true)},
        {1, 3, fastxlsx::CellValue::boolean(false)},
        {1, 4, fastxlsx::CellValue::text("")},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 6, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 7, fastxlsx::CellValue::number(7.0)},
        {1, 8, fastxlsx::CellValue::number(8.0)},
        {2, 9, fastxlsx::CellValue::text("supported-values-new-inline")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 9},
        expected_cells,
        "supported source values dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "supported source values post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "supported source values post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "supported source values post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "supported source values post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 9},
        expected_cells,
        "supported source values post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("J3", fastxlsx::CellValue::boolean(false));
    check(sheet.has_pending_changes(),
        "supported source values post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "supported source values post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 10,
        "supported source values post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "supported source values post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:J3"/>)",
        "supported source values post-noop reuse save should refresh dimension");
    check_contains(post_noop_reuse_xml, R"(<c r="J3" t="b"><v>0</v></c>)",
        "supported source values post-noop reuse save should include the later boolean edit");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") == post_noop_reuse_entries.end(),
        "supported source values post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "supported source values post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "supported source values post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "supported source values post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "supported source values post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::blank()},
        {1, 2, fastxlsx::CellValue::boolean(true)},
        {1, 3, fastxlsx::CellValue::boolean(false)},
        {1, 4, fastxlsx::CellValue::text("")},
        {1, 5, fastxlsx::CellValue::blank()},
        {1, 6, fastxlsx::CellValue::formula("SUM(B1:C1)")},
        {1, 7, fastxlsx::CellValue::number(7.0)},
        {1, 8, fastxlsx::CellValue::number(8.0)},
        {2, 9, fastxlsx::CellValue::text("supported-values-new-inline")},
        {3, 10, fastxlsx::CellValue::boolean(false)},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 10},
        post_noop_reuse_cells,
        "supported source values post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "supported source values post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "supported source values post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "supported source values post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 10},
        post_noop_reuse_cells,
        "supported source values post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_source_scalar_string_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-string-type-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-string-type-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-string-type")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-string-type")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="str"><v>plain &amp; &lt;text&gt;</v></c>)"
          R"(<c r="B1" t="str"><f>TEXT(C1,"@")&amp;"!"</f><v>cached &amp; stale</v></c>)"
          R"(<c r="C1"><v>7</v></c>)"
          R"(</row></sheetData></worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(t="str")",
        "source scalar-string fixture should contain t=str cells");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "plain & <text>",
        "WorksheetEditor should materialize source t=str scalar cells as text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Formula
            && b1->text_value() == "TEXT(C1,\"@\")&\"!\"",
        "WorksheetEditor should materialize source t=str formula cells as formulas");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Number
            && c1->number_value() == 7.0,
        "WorksheetEditor should still materialize numeric siblings beside t=str cells");
    check(!sheet.has_pending_changes(),
        "source t=str materialization should start as a clean read-only session");
    check(!editor.has_pending_changes(),
        "source t=str materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "source t=str materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source t=str materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source t=str materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source t=str materialization should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after source t=str materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source t=str materialization should not mutate source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("plain & <text>")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(C1,\"@\")&\"!\"")},
        {1, 3, fastxlsx::CellValue::number(7.0)},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 3},
        noop_cells,
        "source t=str no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("string-type-new-inline"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "dirty source t=str projection should refresh dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>plain &amp; &lt;text&gt;</t></is></c>)",
        "dirty projection should write source t=str scalar text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1"><f>TEXT(C1,"@")&amp;"!"</f></c>)",
        "dirty projection should write source t=str formulas without cached values");
    check_not_contains(worksheet_xml, "cached &amp; stale",
        "dirty projection should not preserve stale source t=str formula cached values");
    check_not_contains(worksheet_xml, R"(t="str")",
        "dirty projection should not preserve source t=str cell type tokens");
    check_contains(worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>string-type-new-inline</t></is></c>)",
        "dirty projection should include later edits beside source t=str cells");
    check(output_entries.find("xl/sharedStrings.xml") == output_entries.end(),
        "dirty source t=str projection should not create a sharedStrings part");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-string-type",
        "dirty source t=str projection should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("plain & <text>")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(C1,\"@\")&\"!\"")},
        {1, 3, fastxlsx::CellValue::number(7.0)},
        {2, 4, fastxlsx::CellValue::text("string-type-new-inline")},
    };
    check_reopened_source_success_dirty_output(
        dirty_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source t=str dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source t=str post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source t=str post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source t=str post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source t=str post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "source t=str post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("E3", fastxlsx::CellValue::number(42.5));
    check(sheet.has_pending_changes(),
        "source t=str post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source t=str post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 5,
        "source t=str post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source t=str post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:E3"/>)",
        "source t=str post-noop reuse save should refresh dimension");
    check_contains(post_noop_reuse_xml, R"(<c r="E3"><v>42.5</v></c>)",
        "source t=str post-noop reuse save should include the later numeric edit");
    check_not_contains(post_noop_reuse_xml, R"(t="str")",
        "source t=str post-noop reuse save should keep regenerated cells free of t=str tokens");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") == post_noop_reuse_entries.end(),
        "source t=str post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source t=str post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "source t=str post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "source t=str post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "source t=str post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("plain & <text>")},
        {1, 2, fastxlsx::CellValue::formula("TEXT(C1,\"@\")&\"!\"")},
        {1, 3, fastxlsx::CellValue::number(7.0)},
        {2, 4, fastxlsx::CellValue::text("string-type-new-inline")},
        {3, 5, fastxlsx::CellValue::number(42.5)},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source t=str post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source t=str post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source t=str post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source t=str post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "source t=str post-noop reuse no-op output");
}

void test_public_worksheet_editor_flattens_source_inline_rich_text()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-inline-rich-text-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-inline-rich")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-inline-rich")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="inlineStr"><is>)"
          R"(<r><rPr><b/><color rgb="FFFF0000"/></rPr><t>rich-</t></r>)"
          R"(<r><t>A&amp;B</t></r>)"
          R"(<r><t xml:space="preserve"> kept </t></r>)"
          R"(<rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh>)"
          R"(<phoneticPr fontId="1"/>)"
          R"(<extLst><ext uri="{fastxlsx-test}"><t>ignored-ext</t></ext></extLst>)"
          R"(</is></c>)"
          R"(</row></sheetData>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "rich-A&B kept ",
        "WorksheetEditor should flatten source inline rich text and ignore phonetic/ext text");
    check(!sheet.has_pending_changes(),
        "inline rich text materialization should start clean");
    check(!editor.has_pending_changes(),
        "inline rich text materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "inline rich text materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after inline rich text materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after inline rich text materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after inline rich text materialization should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after inline rich text materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after inline rich text materialization should not mutate source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B kept ")},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 1},
        noop_cells,
        "inline rich text no-op output");

    sheet.set_cell("B2", fastxlsx::CellValue::text("inline-rich-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">rich-A&amp;B kept </t></is></c>)",
        "dirty projection should write flattened source inline rich text as plain inline text");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>inline-rich-new</t></is></c>)",
        "dirty projection should include edits beside flattened inline rich text");
    check_not_contains(worksheet_xml, "<rPr>",
        "dirty projection should not preserve inline rich text formatting");
    check_not_contains(worksheet_xml, "<rPh",
        "dirty projection should not preserve inline phonetic markup");
    check_not_contains(worksheet_xml, "ignored-phonetic",
        "dirty projection should not keep ignored inline phonetic text");
    check_not_contains(worksheet_xml, "ignored-ext",
        "dirty projection should not keep ignored inline extension text");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-inline-rich",
        "dirty inline rich text projection should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B kept ")},
        {2, 2, fastxlsx::CellValue::text("inline-rich-new")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "inline rich text dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "inline rich text post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "inline rich text post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "inline rich text post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "inline rich text post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "inline rich text post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("C3", fastxlsx::CellValue::text("inline-rich-reused & <again>"));
    check(sheet.has_pending_changes(),
        "inline rich text post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "inline rich text post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 3,
        "inline rich text post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "inline rich text post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:C3"/>)",
        "inline rich text post-noop reuse save should refresh dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="C3" t="inlineStr"><is><t>inline-rich-reused &amp; &lt;again&gt;</t></is></c>)",
        "inline rich text post-noop reuse save should include the later text edit");
    check_not_contains(post_noop_reuse_xml, "<rPr>",
        "inline rich text post-noop reuse save should keep rich formatting flattened");
    check_not_contains(post_noop_reuse_xml, "<rPh",
        "inline rich text post-noop reuse save should keep phonetic markup omitted");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") == post_noop_reuse_entries.end(),
        "inline rich text post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "inline rich text post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "inline rich text post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "inline rich text post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "inline rich text post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B kept ")},
        {2, 2, fastxlsx::CellValue::text("inline-rich-new")},
        {3, 3, fastxlsx::CellValue::text("inline-rich-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "inline rich text post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "inline rich text post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "inline rich text post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "inline rich text post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "inline rich text post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_prefixed_source_inline_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-inline-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("prefixed-inline-placeholder")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-inline")});
        writer.close();
    }

    const std::string worksheet_xml =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test">)"
          R"(<x:sheetData>)"
          R"(<x:row r="1">)"
          R"(<x:c r="A1" t="inlineStr"><x:is><x:t>prefixed-inline</x:t></x:is></x:c>)"
          R"(<x:c r="B1" t="inlineStr"><x:is><x:t xml:space="preserve"> spaced </x:t></x:is></x:c>)"
          R"(<x:c r="C1" t="inlineStr"><x:is>)"
          R"(<x:r><x:rPr><x:b/></x:rPr><x:t>rich-</x:t></x:r>)"
          R"(<x:r><x:t>tail</x:t></x:r>)"
          R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
          R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
          R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst>)"
          R"(</x:is></x:c>)"
          R"(</x:row>)"
          R"(<x:row r="2">)"
          R"(<x:c r="A2"><x:v>42</x:v></x:c>)"
          R"(<x:c r="B2" t="b"><x:v>1</x:v></x:c>)"
          R"(<x:c r="C2"><x:f>SUM(A2:A2)</x:f><x:v>999</x:v></x:c>)"
          R"(</x:row>)"
          R"(</x:sheetData>)"
          R"(</x:worksheet>)";
    rewrite_package_entry_as_stored(source, "xl/worksheets/sheet1.xml", worksheet_xml);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:worksheet",
        "prefixed inline fixture should use a qualified worksheet root");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:is>",
        "prefixed inline fixture should use qualified inline-string wrappers");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "ignored-nested-ext",
        "prefixed inline fixture should carry nested ignored extension text");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "<x:extLst/>",
        "prefixed inline fixture should carry self-closing ignored metadata");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> b2 = sheet.try_cell("B2");
    const std::optional<fastxlsx::CellValue> c2 = sheet.try_cell("C2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "prefixed-inline",
        "WorksheetEditor should materialize prefixed source inline text by local-name");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == " spaced ",
        "WorksheetEditor should keep xml:space text from prefixed inline wrappers");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "rich-tail",
        "WorksheetEditor should flatten prefixed source inline rich text by local-name");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Number
            && a2->number_value() == 42.0,
        "WorksheetEditor should materialize prefixed numeric value wrappers");
    check(b2.has_value() && b2->kind() == fastxlsx::CellValueKind::Boolean
            && b2->boolean_value(),
        "WorksheetEditor should materialize prefixed boolean value wrappers");
    check(c2.has_value() && c2->kind() == fastxlsx::CellValueKind::Formula
            && c2->text_value() == "SUM(A2:A2)",
        "WorksheetEditor should materialize prefixed formula wrappers and ignore cached values");
    check(!sheet.has_pending_changes(),
        "prefixed inline materialization should start clean");
    check(!editor.has_pending_changes(),
        "prefixed inline materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "prefixed inline materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after prefixed inline materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after prefixed inline materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after prefixed inline materialization should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after prefixed inline materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after prefixed inline materialization should not mutate source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-inline")},
        {1, 2, fastxlsx::CellValue::text(" spaced ")},
        {1, 3, fastxlsx::CellValue::text("rich-tail")},
        {2, 1, fastxlsx::CellValue::number(42.0)},
        {2, 2, fastxlsx::CellValue::boolean(true)},
        {2, 3, fastxlsx::CellValue::formula("SUM(A2:A2)")},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 2, 3},
        noop_cells,
        "prefixed inline no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("prefixed-inline-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string dirty_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(dirty_worksheet_xml, R"(<x:dimension ref="A1:D2"/>)",
        "dirty prefixed inline sheetData flush should refresh the sparse-store dimension while preserving the source prefix");
    check_contains(dirty_worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>prefixed-inline</t></is></c>)",
        "dirty projection should write prefixed source inline text as plain inlineStr");
    check_contains(dirty_worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve"> spaced </t></is></c>)",
        "dirty projection should preserve prefixed inline whitespace in plain inlineStr");
    check_contains(dirty_worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>rich-tail</t></is></c>)",
        "dirty projection should write flattened prefixed inline rich text as plain text");
    check_contains(dirty_worksheet_xml, R"(<c r="A2"><v>42</v></c>)",
        "dirty projection should preserve materialized numeric values");
    check_contains(dirty_worksheet_xml, R"(<c r="B2" t="b"><v>1</v></c>)",
        "dirty projection should preserve materialized boolean values");
    check_contains(dirty_worksheet_xml, R"(<c r="C2"><f>SUM(A2:A2)</f></c>)",
        "dirty projection should preserve formulas without stale cached values");
    check_contains(dirty_worksheet_xml,
        R"(<c r="D2" t="inlineStr"><is><t>prefixed-inline-dirty</t></is></c>)",
        "dirty projection should include edits beside prefixed source cells");
    check_contains(dirty_worksheet_xml,
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test">)",
        "dirty sheetData flush should preserve the source worksheet wrapper prefix");
    check_not_contains(dirty_worksheet_xml, "<x:c",
        "dirty sheetData flush should not preserve source cell element prefixes inside replaced sheetData");
    check_not_contains(dirty_worksheet_xml, "<x:v",
        "dirty sheetData flush should not preserve source value element prefixes inside replaced sheetData");
    check_not_contains(dirty_worksheet_xml, "ignored-phonetic",
        "dirty projection should not keep ignored prefixed phonetic text");
    check_not_contains(dirty_worksheet_xml, "ignored-nested-phonetic",
        "dirty projection should not keep nested ignored prefixed phonetic text");
    check_not_contains(dirty_worksheet_xml, "ignored-nested-ext",
        "dirty projection should not keep nested ignored prefixed extension text");
    check_not_contains(dirty_worksheet_xml, "ignored-ext",
        "dirty projection should not keep ignored prefixed extension text");
    check_not_contains(dirty_worksheet_xml, "<v>999</v>",
        "dirty projection should not preserve stale cached formula values");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty prefixed inline projection should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-inline")},
        {1, 2, fastxlsx::CellValue::text(" spaced ")},
        {1, 3, fastxlsx::CellValue::text("rich-tail")},
        {2, 1, fastxlsx::CellValue::number(42.0)},
        {2, 2, fastxlsx::CellValue::boolean(true)},
        {2, 3, fastxlsx::CellValue::formula("SUM(A2:A2)")},
        {2, 4, fastxlsx::CellValue::text("prefixed-inline-dirty")},
    };
    check_reopened_source_success_dirty_output(
        dirty_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "prefixed inline dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "prefixed inline post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "prefixed inline post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed inline post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "prefixed inline post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 4},
        expected_cells,
        "prefixed inline post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("E3", fastxlsx::CellValue::text("prefixed-inline-reused & <again>"));
    check(sheet.has_pending_changes(),
        "prefixed inline post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "prefixed inline post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 8,
        "prefixed inline post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "prefixed inline post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<x:dimension ref="A1:E3"/>)",
        "prefixed inline post-noop reuse save should refresh dimension while preserving the source prefix");
    check_contains(post_noop_reuse_xml,
        R"(<c r="E3" t="inlineStr"><is><t>prefixed-inline-reused &amp; &lt;again&gt;</t></is></c>)",
        "prefixed inline post-noop reuse save should include the later escaped text edit");
    check_not_contains(post_noop_reuse_xml, "<x:c",
        "prefixed inline post-noop reuse save should keep regenerated cell elements unprefixed");
    check_not_contains(post_noop_reuse_xml, "<x:v",
        "prefixed inline post-noop reuse save should keep regenerated value elements unprefixed");
    check_not_contains(post_noop_reuse_xml, "ignored-nested-ext",
        "prefixed inline post-noop reuse save should keep ignored extension text omitted");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") == post_noop_reuse_entries.end(),
        "prefixed inline post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed inline post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "prefixed inline post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "prefixed inline post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "prefixed inline post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-inline")},
        {1, 2, fastxlsx::CellValue::text(" spaced ")},
        {1, 3, fastxlsx::CellValue::text("rich-tail")},
        {2, 1, fastxlsx::CellValue::number(42.0)},
        {2, 2, fastxlsx::CellValue::boolean(true)},
        {2, 3, fastxlsx::CellValue::formula("SUM(A2:A2)")},
        {2, 4, fastxlsx::CellValue::text("prefixed-inline-dirty")},
        {3, 5, fastxlsx::CellValue::text("prefixed-inline-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "prefixed inline post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "prefixed inline post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "prefixed inline post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed inline post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 5},
        post_noop_reuse_cells,
        "prefixed inline post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_source_default_style_attribute_as_unstyled()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-default-style-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-default-style-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("loadable-before-style"),
            fastxlsx::CellView::text("explicit-default-source-style"),
            fastxlsx::CellView::text("single-quoted-default-source-style"),
            fastxlsx::CellView::text("spaced-default-source-style")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-explicit-default")});
        writer.close();
    }

    std::map<std::string, std::string> entries =
        fastxlsx::test::read_zip_entries(source);
    std::string& source_worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="B1" t="inlineStr">)",
        R"(<c r="B1" s="0" t="inlineStr">)");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="C1" t="inlineStr">)",
        R"(<c r="C1" s='0' t="inlineStr">)");
    replace_first_or_throw(source_worksheet_xml,
        R"(<c r="D1" t="inlineStr">)",
        R"(<c r="D1" s = "0" t="inlineStr">)");
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s="0")",
        "source default-style fixture should contain an explicit s=0 attribute");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s='0')",
        "source default-style fixture should contain a single-quoted explicit s=0 attribute");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), R"(s = "0")",
        "source default-style fixture should contain an explicit s=0 attribute with whitespace around equals");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    const std::optional<fastxlsx::CellValue> d1 = sheet.try_cell("D1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "loadable-before-style" && !a1->has_style(),
        "WorksheetEditor should materialize the unstyled source cell");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "explicit-default-source-style" && !b1->has_style(),
        "WorksheetEditor should normalize source s=0 to no style handle");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "single-quoted-default-source-style"
            && !c1->has_style(),
        "WorksheetEditor should normalize source single-quoted s=0 to no style handle");
    check(d1.has_value() && d1->kind() == fastxlsx::CellValueKind::Text
            && d1->text_value() == "spaced-default-source-style" && !d1->has_style(),
        "WorksheetEditor should normalize source s=0 with whitespace around equals to no style handle");
    check(!sheet.has_pending_changes(),
        "source s=0 materialization should start as a clean read-only session");
    check(!editor.has_pending_changes(),
        "source s=0 materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "source s=0 materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source s=0 materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source s=0 materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after source s=0 materialization should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after source s=0 materialization should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "no-op save_as after source s=0 materialization should not mutate source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("loadable-before-style")},
        {1, 2, fastxlsx::CellValue::text("explicit-default-source-style")},
        {1, 3, fastxlsx::CellValue::text("single-quoted-default-source-style")},
        {1, 4, fastxlsx::CellValue::text("spaced-default-source-style")},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 4},
        noop_cells,
        "default style no-op output");

    sheet.set_cell("E1", fastxlsx::CellValue::text("dirty-default-style-trigger"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>loadable-before-style</t></is></c>)",
        "dirty projection should keep the prior unstyled source cell");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>explicit-default-source-style</t></is></c>)",
        "dirty projection should write source s=0 as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>single-quoted-default-source-style</t></is></c>)",
        "dirty projection should write source single-quoted s=0 as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>spaced-default-source-style</t></is></c>)",
        "dirty projection should write source s=0 with whitespace around equals as an unstyled inline string");
    check_contains(worksheet_xml,
        R"(<c r="E1" t="inlineStr"><is><t>dirty-default-style-trigger</t></is></c>)",
        "dirty projection should include the trigger edit");
    check_not_contains(worksheet_xml, R"(s="0")",
        "dirty projection should not serialize the normalized default style attribute");
    check_not_contains(worksheet_xml, R"(s='0')",
        "dirty projection should not serialize the normalized single-quoted default style attribute");
    check_not_contains(worksheet_xml, R"(s = "0")",
        "dirty projection should not serialize the normalized whitespace-around-equals default style attribute");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("loadable-before-style")},
        {1, 2, fastxlsx::CellValue::text("explicit-default-source-style")},
        {1, 3, fastxlsx::CellValue::text("single-quoted-default-source-style")},
        {1, 4, fastxlsx::CellValue::text("spaced-default-source-style")},
        {1, 5, fastxlsx::CellValue::text("dirty-default-style-trigger")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 1, 5},
        expected_cells,
        "default style dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "default style post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "default style post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "default style post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "default style post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 1, 5},
        expected_cells,
        "default style post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("F2", fastxlsx::CellValue::text("default-style-reused & <again>"));
    check(sheet.has_pending_changes(),
        "default style post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "default style post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 6,
        "default style post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "default style post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:F2"/>)",
        "default style post-noop reuse save should refresh dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="F2" t="inlineStr"><is><t>default-style-reused &amp; &lt;again&gt;</t></is></c>)",
        "default style post-noop reuse save should include the later escaped text edit");
    check_not_contains(post_noop_reuse_xml, R"(s="0")",
        "default style post-noop reuse save should not serialize normalized default style attributes");
    check_not_contains(post_noop_reuse_xml, R"(s='0')",
        "default style post-noop reuse save should not serialize single-quoted default style attributes");
    check_not_contains(post_noop_reuse_xml, R"(s = "0")",
        "default style post-noop reuse save should not serialize spaced default style attributes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") == post_noop_reuse_entries.end(),
        "default style post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "default style post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "default style post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "default style post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "default style post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("loadable-before-style")},
        {1, 2, fastxlsx::CellValue::text("explicit-default-source-style")},
        {1, 3, fastxlsx::CellValue::text("single-quoted-default-source-style")},
        {1, 4, fastxlsx::CellValue::text("spaced-default-source-style")},
        {1, 5, fastxlsx::CellValue::text("dirty-default-style-trigger")},
        {2, 6, fastxlsx::CellValue::text("default-style-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 2, 6},
        post_noop_reuse_cells,
        "default style post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "default style post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "default style post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "default style post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 2, 6},
        post_noop_reuse_cells,
        "default style post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_empty_source_worksheets()
{
    const auto write_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-empty-source")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-empty-source")});
        writer.close();
        return source;
    };

    const auto worksheet_xml = [](std::string_view body) {
        return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            + std::string(body) + "</worksheet>";
    };

    const auto expect_empty_source_worksheet_materialization =
        [&](std::string_view tag, std::string_view replacement_worksheet_xml) {
            const std::string source_name =
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-source.xlsx";
            const std::filesystem::path source = write_source(source_name);
            const std::filesystem::path noop_output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-noop-output.xlsx");
            const std::filesystem::path output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-output.xlsx");
            const std::filesystem::path dirty_noop_output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-dirty-noop-output.xlsx");
            const std::filesystem::path post_noop_reuse_output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-post-noop-reuse-output.xlsx");
            const std::filesystem::path post_noop_reuse_noop_output = artifact(
                std::string("fastxlsx-workbook-editor-public-empty-source-")
                + std::string(tag) + "-post-noop-reuse-noop-output.xlsx");

            std::map<std::string, std::string> entries =
                fastxlsx::test::read_zip_entries(source);
            entries.at("xl/worksheets/sheet1.xml") =
                std::string(replacement_worksheet_xml);
            write_stored_zip_entries(source, entries);
            const auto source_entries = fastxlsx::test::read_zip_entries(source);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
            check(sheet.cell_count() == 0,
                "empty source worksheet should materialize as an empty sparse store");
            check(!sheet.try_cell("A1").has_value(),
                "empty source worksheet should not invent an A1 sparse record");
            check(sheet.sparse_cells().empty(),
                "empty source worksheet should expose no sparse snapshots");
            check(!sheet.has_pending_changes(),
                "read-only empty source worksheet materialization should start clean");
            check(!editor.has_pending_changes(),
                "read-only empty source worksheet materialization should not dirty WorkbookEditor");
            check(editor.pending_change_count() == 0,
                "read-only empty source worksheet materialization should not queue public Patch edits");

            editor.save_as(noop_output);
            check(!sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " no-op save should keep Data clean");
            check(!editor.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " no-op save should keep WorkbookEditor clean");
            check(editor.pending_change_count() == 0,
                std::string("empty source ") + std::string(tag)
                    + " no-op save should not create public edits");
            const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
            check(noop_entries == source_entries,
                std::string("empty source ") + std::string(tag)
                    + " no-op save should copy source entries");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                std::string("empty source ") + std::string(tag)
                    + " no-op save should not mutate the source package");
            fastxlsx::WorkbookEditor noop_editor = fastxlsx::WorkbookEditor::open(noop_output);
            fastxlsx::WorksheetEditor noop_sheet = noop_editor.worksheet("Data");
            check_workbook_editor_public_clean_state(
                noop_editor, std::string("empty source ") + std::string(tag) + " no-op fresh reopen");
            check(!noop_sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " no-op fresh reopen should materialize a clean worksheet");
            check(noop_sheet.cell_count() == 0,
                std::string("empty source ") + std::string(tag)
                    + " no-op fresh reopen should stay empty");
            check(!noop_sheet.used_range().has_value(),
                std::string("empty source ") + std::string(tag)
                    + " no-op fresh reopen should expose no used range");
            check(noop_sheet.sparse_cells().empty(),
                std::string("empty source ") + std::string(tag)
                    + " no-op fresh reopen should expose no sparse cells");
            check(!noop_sheet.try_cell("A1").has_value(),
                std::string("empty source ") + std::string(tag)
                    + " no-op fresh reopen should not invent A1");
            check_workbook_editor_public_clean_state(
                noop_editor, std::string("empty source ") + std::string(tag) + " no-op fresh reopen reads");

            const std::string inserted_text =
                std::string("empty-source-materialized-") + std::string(tag);
            sheet.set_cell("B2", fastxlsx::CellValue::text(inserted_text));
            editor.save_as(output);

            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
            check_contains(worksheet_xml, R"(<dimension ref="B2"/>)",
                "empty source worksheet edit should project a dimension from sparse records");
            check_contains(worksheet_xml,
                R"(<sheetData><row r="2"><c r="B2" t="inlineStr"><is><t>)"
                    + inserted_text + R"(</t></is></c></row></sheetData>)",
                "empty source worksheet edit should project standalone sheetData");
            check_not_contains(worksheet_xml, "placeholder-empty-source",
                "empty source worksheet materialization should not revive original placeholder cells");
            check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-empty-source",
                "empty source worksheet materialization should preserve untouched sheets");
            const ReopenedSourceSuccessCell expected_cells[] = {
                {2, 2, fastxlsx::CellValue::text(inserted_text)},
            };
            check_reopened_source_success_dirty_output(
                output,
                fastxlsx::CellRange {2, 2, 2, 2},
                expected_cells,
                std::string("empty source ") + std::string(tag) + " dirty output");

            editor.save_as(dirty_noop_output);
            check(!sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " post-dirty no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-dirty no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-dirty no-op save should not mutate the source package");
            check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-dirty no-op save should not mutate the earlier source-copy output");
            check_reopened_source_success_dirty_output(
                dirty_noop_output,
                fastxlsx::CellRange {2, 2, 2, 2},
                expected_cells,
                std::string("empty source ") + std::string(tag)
                    + " post-dirty no-op output");

            const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
            const std::string reused_text =
                std::string("empty-source-reused-") + std::string(tag) + " & <again>";
            sheet.set_cell("C3", fastxlsx::CellValue::text(reused_text));
            check(sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse edit should dirty Data");
            check(editor.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse edit should dirty WorkbookEditor");
            check(sheet.cell_count() == 2,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse edit should add one sparse record");

            editor.save_as(post_noop_reuse_output);
            check(!sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should keep Data clean");
            const auto post_noop_reuse_entries =
                fastxlsx::test::read_zip_entries(post_noop_reuse_output);
            const std::string post_noop_reuse_xml =
                post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
            check_contains(post_noop_reuse_xml, R"(<dimension ref="B2:C3"/>)",
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should refresh dimension from sparse records");
            check_contains(post_noop_reuse_xml,
                R"(<row r="3"><c r="C3" t="inlineStr"><is><t>)"
                    + std::string("empty-source-reused-") + std::string(tag)
                    + R"( &amp; &lt;again&gt;</t></is></c></row>)",
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should include the later escaped text edit");
            check_not_contains(post_noop_reuse_xml, "placeholder-empty-source",
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should not revive original placeholder cells");
            check(post_noop_reuse_entries.find("xl/sharedStrings.xml")
                    == post_noop_reuse_entries.end(),
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should still avoid sharedStrings");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should not mutate the source package");
            check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should not mutate the prior no-op output");
            check(fastxlsx::test::read_zip_entries(output) == output_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should not mutate the prior dirty output");
            check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse save should not mutate the prior dirty no-op output");
            const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
                {2, 2, fastxlsx::CellValue::text(inserted_text)},
                {3, 3, fastxlsx::CellValue::text(reused_text)},
            };
            check_reopened_source_success_dirty_output(
                post_noop_reuse_output,
                fastxlsx::CellRange {2, 2, 3, 3},
                post_noop_reuse_cells,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse output");

            editor.save_as(post_noop_reuse_noop_output);
            check(!sheet.has_pending_changes(),
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
                    == post_noop_reuse_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse no-op save should not mutate the source package");
            check_reopened_source_success_dirty_output(
                post_noop_reuse_noop_output,
                fastxlsx::CellRange {2, 2, 3, 3},
                post_noop_reuse_cells,
                std::string("empty source ") + std::string(tag)
                    + " post-noop reuse no-op output");
        };

    expect_empty_source_worksheet_materialization(
        "missing-sheet-data", worksheet_xml(R"(<dimension ref="A1"/>)"));

    expect_empty_source_worksheet_materialization(
        "self-closing-sheet-data",
        worksheet_xml(R"(<dimension ref="A1"/><sheetData/>)"));
}

void test_public_worksheet_editor_preserves_source_wrapper_metadata_on_dirty_sheet_data_flush()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-wrapper-metadata-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-wrapper")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrapper-metadata")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetPr>ignored-wrapper-text<tabColor rgb="FFFF0000"/></sheetPr>)"
          R"(<dimension ref="A1"/>)"
          R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
          R"(<sheetFormatPr defaultRowHeight="15"/>)"
          R"(<cols><col min="1" max="1" width="20" customWidth="1"/></cols>)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-wrapper</t></is></c>)"
          R"(</row></sheetData>)"
          R"(<autoFilter ref="A1:A1"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "source-wrapper",
        "WorksheetEditor should materialize supported cells beside source wrapper metadata");
    check(!sheet.has_pending_changes(),
        "read-only source wrapper metadata materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source wrapper metadata materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source wrapper metadata materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source wrapper metadata no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source wrapper metadata no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source wrapper metadata no-op save should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "source wrapper metadata no-op save should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source wrapper metadata no-op save should not mutate the source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-wrapper")},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 1},
        noop_cells,
        "source wrapper metadata no-op output");

    sheet.set_cell("B2", fastxlsx::CellValue::text("wrapper-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "dirty source wrapper metadata flush should refresh sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-wrapper</t></is></c>)",
        "dirty source wrapper metadata flush should keep materialized source cells");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>wrapper-new-inline</t></is></c>)",
        "dirty source wrapper metadata flush should write new inline text");
    check_contains(worksheet_xml, R"(<sheetPr>ignored-wrapper-text<tabColor rgb="FFFF0000"/></sheetPr>)",
        "dirty materialized sheetData flush should preserve source sheetPr metadata");
    check_contains(worksheet_xml, R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)",
        "dirty materialized sheetData flush should preserve source sheetViews metadata");
    check_contains(worksheet_xml, R"(<sheetFormatPr defaultRowHeight="15"/>)",
        "dirty materialized sheetData flush should preserve source sheetFormatPr metadata");
    check_contains(worksheet_xml,
        R"(<cols><col min="1" max="1" width="20" customWidth="1"/></cols>)",
        "dirty materialized sheetData flush should preserve source cols metadata");
    check_contains(worksheet_xml, R"(<autoFilter ref="A1:A1"/>)",
        "dirty materialized sheetData flush should preserve source autoFilter metadata");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-wrapper-metadata",
        "dirty source wrapper metadata flush should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-wrapper")},
        {2, 2, fastxlsx::CellValue::text("wrapper-new-inline")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "wrapper metadata dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "wrapper metadata post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "wrapper metadata post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrapper metadata post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "wrapper metadata post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "wrapper metadata post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("C3", fastxlsx::CellValue::text("wrapper-reused & <again>"));
    check(sheet.has_pending_changes(),
        "wrapper metadata post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "wrapper metadata post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 3,
        "wrapper metadata post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "wrapper metadata post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:C3"/>)",
        "wrapper metadata post-noop reuse save should refresh sparse-store dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="C3" t="inlineStr"><is><t>wrapper-reused &amp; &lt;again&gt;</t></is></c>)",
        "wrapper metadata post-noop reuse save should include the later escaped text edit");
    check_contains(post_noop_reuse_xml, R"(<sheetPr>ignored-wrapper-text<tabColor rgb="FFFF0000"/></sheetPr>)",
        "wrapper metadata post-noop reuse save should preserve source sheetPr metadata");
    check_contains(post_noop_reuse_xml, R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)",
        "wrapper metadata post-noop reuse save should preserve source sheetViews metadata");
    check_contains(post_noop_reuse_xml, R"(<sheetFormatPr defaultRowHeight="15"/>)",
        "wrapper metadata post-noop reuse save should preserve source sheetFormatPr metadata");
    check_contains(post_noop_reuse_xml,
        R"(<cols><col min="1" max="1" width="20" customWidth="1"/></cols>)",
        "wrapper metadata post-noop reuse save should preserve source cols metadata");
    check_contains(post_noop_reuse_xml, R"(<autoFilter ref="A1:A1"/>)",
        "wrapper metadata post-noop reuse save should preserve source autoFilter metadata");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml")
            == post_noop_reuse_entries.end(),
        "wrapper metadata post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrapper metadata post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "wrapper metadata post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "wrapper metadata post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "wrapper metadata post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-wrapper")},
        {2, 2, fastxlsx::CellValue::text("wrapper-new-inline")},
        {3, 3, fastxlsx::CellValue::text("wrapper-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "wrapper metadata post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "wrapper metadata post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "wrapper metadata post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrapper metadata post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "wrapper metadata post-noop reuse no-op output");
}

void test_public_worksheet_editor_preserves_relationship_wrapper_metadata_without_pruning()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-relationship-wrapper-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("Name"),
            fastxlsx::CellView::text("Value")});
        data.append_row({fastxlsx::CellView::text("source-link-row"),
            fastxlsx::CellView::number(7.0)});
        data.add_external_hyperlink(2, 1, "https://example.com/source-wrapper-link");

        fastxlsx::TableOptions table;
        table.name = "RelationshipWrapperTable";
        table.column_names = {"Name", "Value"};
        data.add_table({1, 1, 2, 2}, table);

        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-relationship-wrapper")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_worksheet_xml =
        source_entries.at("xl/worksheets/sheet1.xml");
    check_contains(source_worksheet_xml, "<hyperlinks>",
        "source relationship-wrapper fixture should contain hyperlinks metadata");
    check_contains(source_worksheet_xml, "<tableParts",
        "source relationship-wrapper fixture should contain tableParts metadata");
    check(source_entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "source relationship-wrapper fixture should contain worksheet relationships");
    check(source_entries.contains("xl/tables/table1.xml"),
        "source relationship-wrapper fixture should contain a table part");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    const std::optional<fastxlsx::CellValue> b2 = sheet.try_cell("B2");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text
            && a2->text_value() == "source-link-row",
        "WorksheetEditor should materialize source cells beside relationship wrapper metadata");
    check(b2.has_value() && b2->kind() == fastxlsx::CellValueKind::Number
            && b2->number_value() == 7.0,
        "WorksheetEditor should materialize source numbers beside relationship wrapper metadata");
    check(!sheet.has_pending_changes(),
        "relationship wrapper metadata materialization should start clean");
    check(!editor.has_pending_changes(),
        "relationship wrapper metadata materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "relationship wrapper metadata materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "relationship wrapper no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "relationship wrapper no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "relationship wrapper no-op save should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "relationship wrapper no-op save should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "relationship wrapper no-op save should not mutate the source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("Name")},
        {1, 2, fastxlsx::CellValue::text("Value")},
        {2, 1, fastxlsx::CellValue::text("source-link-row")},
        {2, 2, fastxlsx::CellValue::number(7.0)},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        noop_cells,
        "relationship wrapper no-op output");

    sheet.set_cell("C3", fastxlsx::CellValue::text("relationship-wrapper-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "dirty relationship wrapper flush should refresh sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="inlineStr"><is><t>source-link-row</t></is></c>)",
        "dirty relationship wrapper flush should keep materialized source text");
    check_contains(worksheet_xml, R"(<c r="B2"><v>7</v></c>)",
        "dirty relationship wrapper flush should keep materialized source number");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>relationship-wrapper-new</t></is></c>)",
        "dirty relationship wrapper flush should include the new edit");
    check_contains(worksheet_xml, "<hyperlinks>",
        "dirty relationship wrapper flush should preserve source hyperlinks XML");
    check_contains(worksheet_xml, "<tableParts",
        "dirty relationship wrapper flush should preserve source tableParts XML");
    check_contains(worksheet_xml, "r:id",
        "dirty relationship wrapper flush should preserve source relationship references");
    check(output_entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "dirty sheetData flush should not prune the source worksheet relationships part");
    check(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "dirty sheetData flush should preserve source worksheet relationship bytes");
    check(output_entries.contains("xl/tables/table1.xml"),
        "dirty sheetData flush should not prune the source table part");
    check(output_entries.at("xl/tables/table1.xml")
            == source_entries.at("xl/tables/table1.xml"),
        "dirty sheetData flush should preserve source table bytes");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"),
        "keep-relationship-wrapper",
        "dirty relationship wrapper flush should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("Name")},
        {1, 2, fastxlsx::CellValue::text("Value")},
        {2, 1, fastxlsx::CellValue::text("source-link-row")},
        {2, 2, fastxlsx::CellValue::number(7.0)},
        {3, 3, fastxlsx::CellValue::text("relationship-wrapper-new")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "relationship wrapper dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "relationship wrapper post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "relationship wrapper post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "relationship wrapper post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "relationship wrapper post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "relationship wrapper post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("D4", fastxlsx::CellValue::text("relationship-wrapper-reused & <again>"));
    check(sheet.has_pending_changes(),
        "relationship wrapper post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "relationship wrapper post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 6,
        "relationship wrapper post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "relationship wrapper post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:D4"/>)",
        "relationship wrapper post-noop reuse save should refresh sparse-store dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="D4" t="inlineStr"><is><t>relationship-wrapper-reused &amp; &lt;again&gt;</t></is></c>)",
        "relationship wrapper post-noop reuse save should include the later escaped text edit");
    check_contains(post_noop_reuse_xml, "<hyperlinks>",
        "relationship wrapper post-noop reuse save should preserve source hyperlinks XML");
    check_contains(post_noop_reuse_xml, "<tableParts",
        "relationship wrapper post-noop reuse save should preserve source tableParts XML");
    check_contains(post_noop_reuse_xml, "r:id",
        "relationship wrapper post-noop reuse save should preserve source relationship references");
    check(post_noop_reuse_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "relationship wrapper post-noop reuse save should preserve source worksheet relationship bytes");
    check(post_noop_reuse_entries.at("xl/tables/table1.xml")
            == source_entries.at("xl/tables/table1.xml"),
        "relationship wrapper post-noop reuse save should preserve source table bytes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml")
            == post_noop_reuse_entries.end(),
        "relationship wrapper post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "relationship wrapper post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "relationship wrapper post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "relationship wrapper post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "relationship wrapper post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("Name")},
        {1, 2, fastxlsx::CellValue::text("Value")},
        {2, 1, fastxlsx::CellValue::text("source-link-row")},
        {2, 2, fastxlsx::CellValue::number(7.0)},
        {3, 3, fastxlsx::CellValue::text("relationship-wrapper-new")},
        {4, 4, fastxlsx::CellValue::text("relationship-wrapper-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "relationship wrapper post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "relationship wrapper post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "relationship wrapper post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "relationship wrapper post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "relationship wrapper post-noop reuse no-op output");
}

void test_public_worksheet_editor_preserves_range_wrapper_metadata_on_dirty_sheet_data_flush()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-range-wrapper-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-range-wrapper")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-range-wrapper")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<dimension ref="A1:C3"/>)"
          R"(<sheetData>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>range-wrapper-source</t></is></c>)"
          R"(<c r="B1"><v>3</v></c>)"
          R"(</row>)"
          R"(<row r="2">)"
          R"(<c r="A2" t="b"><v>1</v></c>)"
          R"(</row>)"
          R"(</sheetData>)"
          R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
          R"(<dataValidations count="1">)"
          R"(<dataValidation type="whole" operator="between" sqref="B2:B3">)"
          R"(<formula1>1</formula1><formula2>10</formula2>)"
          R"(</dataValidation>)"
          R"(</dataValidations>)"
          R"(<conditionalFormatting sqref="B2:B3">)"
          R"(<cfRule type="cellIs" priority="1" operator="greaterThan">)"
          R"(<formula>5</formula>)"
          R"(</cfRule>)"
          R"(</conditionalFormatting>)"
          R"(<ignoredErrors><ignoredError sqref="A1:C3" numberStoredAsText="1"/></ignoredErrors>)"
          R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
          R"(<pageSetup orientation="landscape"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "range-wrapper-source",
        "WorksheetEditor should materialize source text beside range wrapper metadata");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Number
            && b1->number_value() == 3.0,
        "WorksheetEditor should materialize source numbers beside range wrapper metadata");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Boolean
            && a2->boolean_value(),
        "WorksheetEditor should materialize source booleans beside range wrapper metadata");
    check(!sheet.has_pending_changes(),
        "range wrapper metadata materialization should start clean");
    check(!editor.has_pending_changes(),
        "range wrapper metadata materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "range wrapper metadata materialization should not queue public Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "range wrapper no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "range wrapper no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "range wrapper no-op save should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "range wrapper no-op save should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range wrapper no-op save should not mutate the source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("range-wrapper-source")},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {2, 1, fastxlsx::CellValue::boolean(true)},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        noop_cells,
        "range wrapper no-op output");

    sheet.set_cell("C3", fastxlsx::CellValue::text("range-wrapper-new"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "dirty range wrapper flush should refresh the sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>range-wrapper-source</t></is></c>)",
        "dirty range wrapper flush should keep materialized source text");
    check_contains(worksheet_xml, R"(<c r="B1"><v>3</v></c>)",
        "dirty range wrapper flush should keep materialized source number");
    check_contains(worksheet_xml, R"(<c r="A2" t="b"><v>1</v></c>)",
        "dirty range wrapper flush should keep materialized source boolean");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="inlineStr"><is><t>range-wrapper-new</t></is></c>)",
        "dirty range wrapper flush should include the new edit");
    check_contains(worksheet_xml, R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)",
        "dirty range wrapper flush should preserve source mergeCells metadata");
    check_contains(worksheet_xml, R"(<dataValidations count="1">)",
        "dirty range wrapper flush should preserve source dataValidations metadata");
    check_contains(worksheet_xml, R"(<conditionalFormatting sqref="B2:B3">)",
        "dirty range wrapper flush should preserve source conditionalFormatting metadata");
    check_contains(worksheet_xml,
        R"(<ignoredErrors><ignoredError sqref="A1:C3" numberStoredAsText="1"/></ignoredErrors>)",
        "dirty range wrapper flush should preserve source ignoredErrors metadata");
    check_contains(worksheet_xml,
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)",
        "dirty range wrapper flush should preserve source pageMargins metadata");
    check_contains(worksheet_xml, R"(<pageSetup orientation="landscape"/>)",
        "dirty range wrapper flush should preserve source pageSetup metadata");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-range-wrapper",
        "dirty range wrapper flush should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("range-wrapper-source")},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {2, 1, fastxlsx::CellValue::boolean(true)},
        {3, 3, fastxlsx::CellValue::text("range-wrapper-new")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "range wrapper dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "range wrapper post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "range wrapper post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range wrapper post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "range wrapper post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        expected_cells,
        "range wrapper post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("D4", fastxlsx::CellValue::text("range-wrapper-reused & <again>"));
    check(sheet.has_pending_changes(),
        "range wrapper post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "range wrapper post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 5,
        "range wrapper post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "range wrapper post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:D4"/>)",
        "range wrapper post-noop reuse save should refresh the sparse-store dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="D4" t="inlineStr"><is><t>range-wrapper-reused &amp; &lt;again&gt;</t></is></c>)",
        "range wrapper post-noop reuse save should include the later escaped text edit");
    check_contains(post_noop_reuse_xml, R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)",
        "range wrapper post-noop reuse save should preserve source mergeCells metadata");
    check_contains(post_noop_reuse_xml, R"(<dataValidations count="1">)",
        "range wrapper post-noop reuse save should preserve source dataValidations metadata");
    check_contains(post_noop_reuse_xml, R"(<conditionalFormatting sqref="B2:B3">)",
        "range wrapper post-noop reuse save should preserve source conditionalFormatting metadata");
    check_contains(post_noop_reuse_xml,
        R"(<ignoredErrors><ignoredError sqref="A1:C3" numberStoredAsText="1"/></ignoredErrors>)",
        "range wrapper post-noop reuse save should preserve source ignoredErrors metadata without recalculation");
    check_contains(post_noop_reuse_xml,
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)",
        "range wrapper post-noop reuse save should preserve source pageMargins metadata");
    check_contains(post_noop_reuse_xml, R"(<pageSetup orientation="landscape"/>)",
        "range wrapper post-noop reuse save should preserve source pageSetup metadata");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml")
            == post_noop_reuse_entries.end(),
        "range wrapper post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range wrapper post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "range wrapper post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "range wrapper post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "range wrapper post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("range-wrapper-source")},
        {1, 2, fastxlsx::CellValue::number(3.0)},
        {2, 1, fastxlsx::CellValue::boolean(true)},
        {3, 3, fastxlsx::CellValue::text("range-wrapper-new")},
        {4, 4, fastxlsx::CellValue::text("range-wrapper-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "range wrapper post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "range wrapper post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "range wrapper post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "range wrapper post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 4, 4},
        post_noop_reuse_cells,
        "range wrapper post-noop reuse no-op output");
}

void test_public_worksheet_editor_preserves_source_wrapper_comments_and_processing_instructions_on_dirty_sheet_data_flush()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-noop-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-source-comments-pi-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-comments-pi")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-comments-pi")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<!--source-comment-before-root-->)"
          R"(<?source-pi-before-root keep?>)"
          R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<!--source-comment-inside-root-->)"
          R"(<?source-pi-inside-root keep?>)"
          R"(<sheetData>)"
          R"(<!--source-comment-inside-sheetData-->)"
          R"(<?source-pi-inside-sheetData keep?>)"
          R"(<row r="1">)"
          R"(<c r="A1" t="inlineStr"><is><t>source-comments-pi</t></is></c>)"
          R"(</row>)"
          R"(<!--source-comment-after-row-->)"
          R"(</sheetData>)"
          R"(<?source-pi-after-sheetData keep?>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "source-comments-pi",
        "WorksheetEditor should materialize supported cells beside source comments and processing instructions");
    check(!sheet.has_pending_changes(),
        "read-only source comment/PI materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source comment/PI materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source comment/PI materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "source comment/PI no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "source comment/PI no-op save should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "source comment/PI no-op save should not create public edits");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "source comment/PI no-op save should copy source entries");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source comment/PI no-op save should not mutate the source package");
    const ReopenedSourceSuccessCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-comments-pi")},
    };
    check_reopened_source_success_dirty_output(
        noop_output,
        fastxlsx::CellRange {1, 1, 1, 1},
        noop_cells,
        "source comment/PI no-op output");

    sheet.set_cell("B2", fastxlsx::CellValue::text("comments-pi-new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "dirty source comment/PI flush should insert sparse-store dimension");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>source-comments-pi</t></is></c>)",
        "dirty source comment/PI flush should keep materialized source cells");
    check_contains(worksheet_xml,
        R"(<c r="B2" t="inlineStr"><is><t>comments-pi-new-inline</t></is></c>)",
        "dirty source comment/PI flush should write new inline text");
    check_contains(worksheet_xml, "source-comment-before-root",
        "dirty materialized sheetData flush should preserve comments before the worksheet root");
    check_contains(worksheet_xml, "source-pi-before-root",
        "dirty materialized sheetData flush should preserve processing instructions before the worksheet root");
    check_contains(worksheet_xml, "source-comment-inside-root",
        "dirty materialized sheetData flush should preserve wrapper comments before sheetData");
    check_contains(worksheet_xml, "source-pi-inside-root",
        "dirty materialized sheetData flush should preserve wrapper processing instructions before sheetData");
    check_contains(worksheet_xml, "source-pi-after-sheetData",
        "dirty materialized sheetData flush should preserve wrapper processing instructions after sheetData");
    check_not_contains(worksheet_xml, "source-comment-inside-sheetData",
        "dirty materialized sheetData flush should replace source sheetData comments");
    check_not_contains(worksheet_xml, "source-pi-inside-sheetData",
        "dirty materialized sheetData flush should replace source sheetData processing instructions");
    check_not_contains(worksheet_xml, "source-comment-after-row",
        "dirty materialized sheetData flush should replace trailing source sheetData comments");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-comments-pi",
        "dirty source comment/PI flush should preserve untouched sheets");
    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-comments-pi")},
        {2, 2, fastxlsx::CellValue::text("comments-pi-new-inline")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "comments and processing instructions dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "comment/PI wrapper post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "comment/PI wrapper post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "comment/PI wrapper post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "comment/PI wrapper post-dirty no-op save should not mutate the earlier source-copy output");
    check_reopened_source_success_dirty_output(
        dirty_noop_output,
        fastxlsx::CellRange {1, 1, 2, 2},
        expected_cells,
        "comments and processing instructions post-dirty no-op output");

    const auto dirty_noop_entries = fastxlsx::test::read_zip_entries(dirty_noop_output);
    sheet.set_cell("C3", fastxlsx::CellValue::text("comments-pi-reused & <again>"));
    check(sheet.has_pending_changes(),
        "comment/PI wrapper post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "comment/PI wrapper post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 3,
        "comment/PI wrapper post-noop reuse edit should add one sparse record");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "comment/PI wrapper post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:C3"/>)",
        "comment/PI wrapper post-noop reuse save should refresh sparse-store dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="C3" t="inlineStr"><is><t>comments-pi-reused &amp; &lt;again&gt;</t></is></c>)",
        "comment/PI wrapper post-noop reuse save should include the later escaped text edit");
    check_contains(post_noop_reuse_xml, "source-comment-before-root",
        "comment/PI wrapper post-noop reuse save should preserve comments before the worksheet root");
    check_contains(post_noop_reuse_xml, "source-pi-before-root",
        "comment/PI wrapper post-noop reuse save should preserve processing instructions before the worksheet root");
    check_contains(post_noop_reuse_xml, "source-comment-inside-root",
        "comment/PI wrapper post-noop reuse save should preserve wrapper comments before sheetData");
    check_contains(post_noop_reuse_xml, "source-pi-inside-root",
        "comment/PI wrapper post-noop reuse save should preserve wrapper processing instructions before sheetData");
    check_contains(post_noop_reuse_xml, "source-pi-after-sheetData",
        "comment/PI wrapper post-noop reuse save should preserve wrapper processing instructions after sheetData");
    check_not_contains(post_noop_reuse_xml, "source-comment-inside-sheetData",
        "comment/PI wrapper post-noop reuse save should still replace source sheetData comments");
    check_not_contains(post_noop_reuse_xml, "source-pi-inside-sheetData",
        "comment/PI wrapper post-noop reuse save should still replace source sheetData processing instructions");
    check_not_contains(post_noop_reuse_xml, "source-comment-after-row",
        "comment/PI wrapper post-noop reuse save should still replace trailing source sheetData comments");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml")
            == post_noop_reuse_entries.end(),
        "comment/PI wrapper post-noop reuse save should still avoid sharedStrings");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "comment/PI wrapper post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "comment/PI wrapper post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "comment/PI wrapper post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == dirty_noop_entries,
        "comment/PI wrapper post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-comments-pi")},
        {2, 2, fastxlsx::CellValue::text("comments-pi-new-inline")},
        {3, 3, fastxlsx::CellValue::text("comments-pi-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "comments and processing instructions post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "comment/PI wrapper post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "comment/PI wrapper post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "comment/PI wrapper post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "comments and processing instructions post-noop reuse no-op output");
}

void test_public_worksheet_editor_read_only_materialization_keeps_noop_save_as_copy_original()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-noop-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-second-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-readonly-materialized-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("noop-shared-a"),
            fastxlsx::CellView::text("noop-shared-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-noop-materialized")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<!--readonly-noop-comment-before-root-->)"
          R"(<?readonly-noop-pi keep?>)"
          R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
          R"(<sheetPr><tabColor rgb="FF00FF00"/></sheetPr>)"
          R"(<dimension ref="A1:B1"/>)"
          R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
          R"(<sheetData><row r="1">)"
          R"(<c r="A1" t="s"><v>0</v></c>)"
          R"(<c r="B1" t="s"><v>1</v></c>)"
          R"(</row></sheetData>)"
          R"(<autoFilter ref="A1:B1"/>)"
          R"(</worksheet>)";
    write_stored_zip_entries(source, entries);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "noop-shared-a",
        "read-only no-op materialization should still read source shared string A1");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "noop-shared-b",
        "read-only no-op materialization should still read source shared string B1");
    check(!sheet.has_pending_changes(),
        "read-only no-op materialization should keep the sheet clean");
    check(!editor.has_pending_changes(),
        "read-only no-op materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "read-only no-op materialization should not queue public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only no-op materialization should not expose dirty materialized names");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "no-op save_as after read-only materialization should keep the sheet clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after read-only materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after read-only materialization should not create public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "no-op save_as after read-only materialization should not expose dirty names");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as after read-only materialization should copy source entries");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "readonly-noop-comment-before-root",
        "no-op save_as after read-only materialization should preserve source comments");
    check_contains(worksheet_xml, "readonly-noop-pi",
        "no-op save_as after read-only materialization should preserve source processing instructions");
    check_contains(worksheet_xml, "<sheetPr>",
        "no-op save_as after read-only materialization should preserve source wrapper metadata");
    check_contains(worksheet_xml, R"(<c r="A1" t="s"><v>0</v></c>)",
        "no-op save_as after read-only materialization should preserve source shared string indexes");
    check_not_contains(worksheet_xml, R"(t="inlineStr")",
        "no-op save_as after read-only materialization should not flush inline-string projection");

    const ReopenedSourceSuccessCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("noop-shared-a")},
        {1, 2, fastxlsx::CellValue::text("noop-shared-b")},
    };
    check_reopened_source_success_dirty_output(
        output,
        fastxlsx::CellRange {1, 1, 1, 2},
        expected_cells,
        "read-only materialized no-op output");

    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "second no-op save_as after read-only materialization should keep the sheet clean");
    check(!editor.has_pending_changes(),
        "second no-op save_as after read-only materialization should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "second no-op save_as after read-only materialization should not create public edits");
    check(editor.pending_materialized_worksheet_names().empty(),
        "second no-op save_as after read-only materialization should not expose dirty names");
    const auto second_output_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_output_entries == output_entries,
        "second no-op save_as after read-only materialization should keep output byte-stable");
    check(second_output_entries == source_entries,
        "second no-op save_as after read-only materialization should keep source-copy bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "second no-op save_as after read-only materialization should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "second no-op save_as after read-only materialization should not mutate the first output");
    check_reopened_source_success_dirty_output(
        second_noop_output,
        fastxlsx::CellRange {1, 1, 1, 2},
        expected_cells,
        "read-only materialized second no-op output");

    sheet.set_cell("C3", fastxlsx::CellValue::text("noop-materialized-reused & <again>"));
    check(sheet.has_pending_changes(),
        "read-only materialized post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "read-only materialized post-noop reuse edit should dirty WorkbookEditor");
    check(sheet.cell_count() == 3,
        "read-only materialized post-noop reuse edit should add one sparse record");
    const std::vector<std::string> dirty_names =
        editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names.front() == "Data",
        "read-only materialized post-noop reuse edit should expose Data as dirty");

    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "read-only materialized post-noop reuse save should keep Data clean");
    check(editor.pending_materialized_worksheet_names().empty(),
        "read-only materialized post-noop reuse save should clear dirty names");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_xml =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_xml, R"(<dimension ref="A1:C3"/>)",
        "read-only materialized post-noop reuse save should refresh sparse-store dimension");
    check_contains(post_noop_reuse_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "read-only materialized post-noop reuse save should reuse source shared string index A1");
    check_contains(post_noop_reuse_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "read-only materialized post-noop reuse save should reuse source shared string index B1");
    check_contains(post_noop_reuse_xml,
        R"(<c r="C3" t="s"><v>3</v></c>)",
        "read-only materialized post-noop reuse save should append the later text edit to sharedStrings");
    check_contains(post_noop_reuse_xml, "readonly-noop-comment-before-root",
        "read-only materialized post-noop reuse save should preserve source comments");
    check_contains(post_noop_reuse_xml, "readonly-noop-pi",
        "read-only materialized post-noop reuse save should preserve source processing instructions");
    check_contains(post_noop_reuse_xml, "<sheetPr>",
        "read-only materialized post-noop reuse save should preserve source wrapper metadata");
    check_contains(post_noop_reuse_xml, R"(<autoFilter ref="A1:B1"/>)",
        "read-only materialized post-noop reuse save should preserve source autoFilter metadata without recalculation");
    const std::string post_noop_reuse_shared_strings =
        post_noop_reuse_entries.at("xl/sharedStrings.xml");
    check_contains(post_noop_reuse_shared_strings,
        R"(<si><t>noop-materialized-reused &amp; &lt;again&gt;</t></si></sst>)",
        "read-only materialized post-noop reuse save should append the escaped text to sharedStrings");
    check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="4")",
        "read-only materialized post-noop reuse save should advance sharedStrings uniqueCount");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "read-only materialized post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "read-only materialized post-noop reuse save should not mutate the first no-op output");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_output_entries,
        "read-only materialized post-noop reuse save should not mutate the second no-op output");
    const ReopenedSourceSuccessCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("noop-shared-a")},
        {1, 2, fastxlsx::CellValue::text("noop-shared-b")},
        {3, 3, fastxlsx::CellValue::text("noop-materialized-reused & <again>")},
    };
    check_reopened_source_success_dirty_output(
        post_noop_reuse_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "read-only materialized post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "read-only materialized post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "read-only materialized post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "read-only materialized post-noop reuse no-op save should not mutate the source package");
    check_reopened_source_success_dirty_output(
        post_noop_reuse_noop_output,
        fastxlsx::CellRange {1, 1, 3, 3},
        post_noop_reuse_cells,
        "read-only materialized post-noop reuse no-op output");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_materializes_source_supported_values();
        test_public_worksheet_editor_materializes_source_scalar_string_cells();
        test_public_worksheet_editor_flattens_source_inline_rich_text();
        test_public_worksheet_editor_materializes_prefixed_source_inline_strings();
        test_public_worksheet_editor_materializes_source_default_style_attribute_as_unstyled();
        test_public_worksheet_editor_materializes_empty_source_worksheets();
        test_public_worksheet_editor_preserves_source_wrapper_metadata_on_dirty_sheet_data_flush();
        test_public_worksheet_editor_preserves_relationship_wrapper_metadata_without_pruning();
        test_public_worksheet_editor_preserves_range_wrapper_metadata_on_dirty_sheet_data_flush();
        test_public_worksheet_editor_preserves_source_wrapper_comments_and_processing_instructions_on_dirty_sheet_data_flush();
        test_public_worksheet_editor_read_only_materialization_keeps_noop_save_as_copy_original();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success core check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success core tests passed\n");
    return 0;
}
