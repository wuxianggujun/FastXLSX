#include "test_workbook_editor_source_success_common.hpp"

struct ReopenedLazySharedStringsCell {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    fastxlsx::CellValue value;
};

bool cell_values_equal(
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

bool shared_strings_snapshot_matches(
    const fastxlsx::WorksheetCellSnapshot& actual,
    const ReopenedLazySharedStringsCell& expected)
{
    return actual.reference.row == expected.row &&
        actual.reference.column == expected.column &&
        cell_values_equal(actual.value, expected.value);
}

std::optional<fastxlsx::CellRange> shared_strings_expected_used_range(
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells)
{
    if (expected_data_cells.empty()) {
        return std::nullopt;
    }

    fastxlsx::CellRange range{
        expected_data_cells.front().row,
        expected_data_cells.front().column,
        expected_data_cells.front().row,
        expected_data_cells.front().column};

    for (const ReopenedLazySharedStringsCell& expected : expected_data_cells) {
        if (expected.row < range.first_row) {
            range.first_row = expected.row;
        }
        if (expected.column < range.first_column) {
            range.first_column = expected.column;
        }
        if (expected.row > range.last_row) {
            range.last_row = expected.row;
        }
        if (expected.column > range.last_column) {
            range.last_column = expected.column;
        }
    }

    return range;
}

bool shared_strings_ranges_equal(
    const fastxlsx::CellRange& actual,
    const fastxlsx::CellRange& expected)
{
    return actual.first_row == expected.first_row &&
        actual.first_column == expected.first_column &&
        actual.last_row == expected.last_row &&
        actual.last_column == expected.last_column;
}

bool shared_strings_row_already_checked(
    const std::vector<std::uint32_t>& checked_rows,
    std::uint32_t row)
{
    for (const std::uint32_t checked_row : checked_rows) {
        if (checked_row == row) {
            return true;
        }
    }

    return false;
}

bool shared_strings_column_already_checked(
    const std::vector<std::uint32_t>& checked_columns,
    std::uint32_t column)
{
    for (const std::uint32_t checked_column : checked_columns) {
        if (checked_column == column) {
            return true;
        }
    }

    return false;
}

void check_reopened_shared_strings_row_snapshots(
    fastxlsx::WorksheetEditor& reopened_data,
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    std::vector<std::uint32_t> checked_rows;

    for (const ReopenedLazySharedStringsCell& expected : expected_data_cells) {
        if (shared_strings_row_already_checked(checked_rows, expected.row)) {
            continue;
        }

        checked_rows.push_back(expected.row);
        std::size_t expected_count = 0;
        for (const ReopenedLazySharedStringsCell& candidate : expected_data_cells) {
            if (candidate.row == expected.row) {
                ++expected_count;
            }
        }

        const std::vector<fastxlsx::WorksheetCellSnapshot> row_cells =
            reopened_data.row_cells(expected.row);
        check(row_cells.size() == expected_count,
            prefix + " fresh reopen row_cells should expose the expected row count");
        if (row_cells.size() != expected_count) {
            continue;
        }

        std::size_t row_index = 0;
        for (const ReopenedLazySharedStringsCell& candidate : expected_data_cells) {
            if (candidate.row != expected.row) {
                continue;
            }

            check(shared_strings_snapshot_matches(row_cells[row_index], candidate),
                prefix + " fresh reopen row_cells should preserve row-major values");
            ++row_index;
        }
    }
}

void check_reopened_shared_strings_column_snapshots(
    fastxlsx::WorksheetEditor& reopened_data,
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    std::vector<std::uint32_t> checked_columns;

    for (const ReopenedLazySharedStringsCell& expected : expected_data_cells) {
        if (shared_strings_column_already_checked(
                checked_columns, expected.column)) {
            continue;
        }

        checked_columns.push_back(expected.column);
        std::size_t expected_count = 0;
        for (const ReopenedLazySharedStringsCell& candidate : expected_data_cells) {
            if (candidate.column == expected.column) {
                ++expected_count;
            }
        }

        const std::vector<fastxlsx::WorksheetCellSnapshot> column_cells =
            reopened_data.column_cells(expected.column);
        check(column_cells.size() == expected_count,
            prefix + " fresh reopen column_cells should expose the expected column count");
        if (column_cells.size() != expected_count) {
            continue;
        }

        std::size_t column_index = 0;
        for (const ReopenedLazySharedStringsCell& candidate : expected_data_cells) {
            if (candidate.column != expected.column) {
                continue;
            }

            check(shared_strings_snapshot_matches(column_cells[column_index], candidate),
                prefix + " fresh reopen column_cells should preserve column-major values");
            ++column_index;
        }
    }
}

void check_reopened_lazy_shared_strings_dirty_output(
    const std::filesystem::path& output,
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells,
    std::string_view expected_shared_failure,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened_editor.worksheet("Data");

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " fresh reopen should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " fresh reopen should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " fresh reopen should not expose dirty materialized memory");
    check(!reopened_data.has_pending_changes(),
        prefix + " fresh reopen should materialize Data as clean");
    check(reopened_data.cell_count() == expected_data_cells.size(),
        prefix + " fresh reopen should keep the expected Data sparse count");

    const std::optional<fastxlsx::CellRange> expected_used_range =
        shared_strings_expected_used_range(expected_data_cells);
    const std::optional<fastxlsx::CellRange> range = reopened_data.used_range();
    check(expected_used_range.has_value() && range.has_value() &&
            shared_strings_ranges_equal(*range, *expected_used_range),
        prefix + " fresh reopen should expose the expected Data used range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reopened_data.sparse_cells();
    check(all_cells.size() == expected_data_cells.size(),
        prefix + " fresh reopen sparse_cells should expose the expected Data cells");
    if (all_cells.size() == expected_data_cells.size()) {
        for (std::size_t index = 0; index < expected_data_cells.size(); ++index) {
            check(shared_strings_snapshot_matches(all_cells[index], expected_data_cells[index]),
                prefix + " fresh reopen sparse_cells should preserve Data order and values");
        }
    }

    for (const ReopenedLazySharedStringsCell& expected : expected_data_cells) {
        const fastxlsx::CellValue actual =
            reopened_data.get_cell(expected.row, expected.column);
        check(cell_values_equal(actual, expected.value),
            prefix + " fresh reopen should read the expected Data cell value");
    }

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        reopened_data.row_cells(1);
    check(row_one.size() == expected_data_cells.size(),
        prefix + " fresh reopen row_cells should expose the represented Data row");
    if (row_one.size() == expected_data_cells.size()) {
        for (std::size_t index = 0; index < expected_data_cells.size(); ++index) {
            const ReopenedLazySharedStringsCell& expected = expected_data_cells[index];
            check(row_one[index].reference.row == expected.row &&
                    row_one[index].reference.column == expected.column &&
                    cell_values_equal(row_one[index].value, expected.value),
                prefix + " fresh reopen row_cells should preserve Data cell order");
        }
    }
    check_reopened_shared_strings_column_snapshots(
        reopened_data, expected_data_cells, scenario);

    bool shared_failed = false;
    try {
        (void)reopened_editor.worksheet("Shared");
    } catch (const fastxlsx::FastXlsxError& error) {
        shared_failed = true;
        check_contains(error.what(), expected_shared_failure,
            prefix + " fresh reopen Shared should keep the original sharedStrings diagnostic");
    }
    check(shared_failed,
        prefix + " fresh reopen Shared should still fail on referenced bad sharedStrings metadata");
    check(!reopened_data.has_pending_changes(),
        prefix + " Shared failure should leave Data clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " Shared failure");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " Shared failure should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " Shared failure should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " Shared failure should not expose dirty materialized memory");
    check(reopened_data.cell_count() == expected_data_cells.size(),
        prefix + " Shared failure should keep Data sparse count stable");
    const std::optional<fastxlsx::CellRange> failure_range =
        reopened_data.used_range();
    check(expected_used_range.has_value() && failure_range.has_value() &&
            shared_strings_ranges_equal(*failure_range, *expected_used_range),
        prefix + " Shared failure should keep Data used range stable");
    const std::vector<fastxlsx::WorksheetCellSnapshot> failure_cells =
        reopened_data.sparse_cells();
    check(failure_cells.size() == expected_data_cells.size(),
        prefix + " Shared failure should keep Data sparse_cells count stable");
    if (failure_cells.size() == expected_data_cells.size()) {
        for (std::size_t index = 0; index < expected_data_cells.size(); ++index) {
            check(shared_strings_snapshot_matches(
                      failure_cells[index], expected_data_cells[index]),
                prefix + " Shared failure should keep Data sparse_cells values stable");
        }
    }
    check_reopened_shared_strings_row_snapshots(
        reopened_data, expected_data_cells, scenario);
    check_reopened_shared_strings_column_snapshots(
        reopened_data, expected_data_cells, scenario);
}

void check_reopened_shared_strings_output(
    const std::filesystem::path& output,
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells,
    const fastxlsx::CellRange& expected_used_range,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    fastxlsx::WorkbookEditor reopened_editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_data = reopened_editor.worksheet("Data");

    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen");
    check(reopened_editor.pending_materialized_worksheet_names().empty(),
        prefix + " fresh reopen should not expose dirty materialized names");
    check(reopened_editor.pending_materialized_cell_count() == 0,
        prefix + " fresh reopen should not expose dirty materialized cells");
    check(reopened_editor.estimated_pending_materialized_memory_usage() == 0,
        prefix + " fresh reopen should not expose dirty materialized memory");
    check(!reopened_data.has_pending_changes(),
        prefix + " fresh reopen should materialize Data as clean");
    check(reopened_data.cell_count() == expected_data_cells.size(),
        prefix + " fresh reopen should keep the expected sparse count");

    const std::optional<fastxlsx::CellRange> range = reopened_data.used_range();
    check(range.has_value() &&
            range->first_row == expected_used_range.first_row &&
            range->first_column == expected_used_range.first_column &&
            range->last_row == expected_used_range.last_row &&
            range->last_column == expected_used_range.last_column,
        prefix + " fresh reopen should expose the expected used range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reopened_data.sparse_cells();
    check(all_cells.size() == expected_data_cells.size(),
        prefix + " fresh reopen sparse_cells should expose the expected cells");
    if (all_cells.size() == expected_data_cells.size()) {
        for (std::size_t index = 0; index < expected_data_cells.size(); ++index) {
            const ReopenedLazySharedStringsCell& expected = expected_data_cells[index];
            check(all_cells[index].reference.row == expected.row &&
                    all_cells[index].reference.column == expected.column &&
                    cell_values_equal(all_cells[index].value, expected.value),
                prefix + " fresh reopen sparse_cells should preserve cell order and values");
        }
    }
    check_reopened_shared_strings_row_snapshots(
        reopened_data, expected_data_cells, scenario);
    check_reopened_shared_strings_column_snapshots(
        reopened_data, expected_data_cells, scenario);

    for (const ReopenedLazySharedStringsCell& expected : expected_data_cells) {
        const fastxlsx::CellValue actual =
            reopened_data.get_cell(expected.row, expected.column);
        check(cell_values_equal(actual, expected.value),
            prefix + " fresh reopen should read the expected cell value directly");
    }

    check(!reopened_data.has_pending_changes(),
        prefix + " fresh reopen readback should leave Data clean");
    check_workbook_editor_public_clean_state(
        reopened_editor, prefix + " fresh reopen readback");
}

void check_reopened_shared_strings_dirty_output(
    const std::filesystem::path& output,
    std::span<const ReopenedLazySharedStringsCell> expected_data_cells,
    const fastxlsx::CellRange& expected_used_range,
    std::string_view scenario)
{
    check_reopened_shared_strings_output(
        output, expected_data_cells, expected_used_range, scenario);
}

void test_public_worksheet_editor_defers_source_shared_strings_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-post-noop-reuse-noop-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-missing-target-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::boolean(true),
            fastxlsx::CellView::formula("A1+1")});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("requires-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") != entries.end(),
        "lazy sharedStrings fixture should contain a sharedStrings part for the second sheet");
    check_not_contains(entries.at("xl/worksheets/sheet1.xml"), R"(t="s")",
        "lazy sharedStrings fixture Data sheet should not contain shared string indexes");
    check_contains(entries.at("xl/worksheets/sheet2.xml"), R"(t="s")",
        "lazy sharedStrings fixture Shared sheet should contain shared string indexes");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");

    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(Target="sharedStrings.xml")",
        R"(Target="missingSharedStrings.xml")");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 2.0,
        "WorksheetEditor should materialize non-shared-string numbers without loading sharedStrings");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Boolean
            && b1->boolean_value(),
        "WorksheetEditor should materialize non-shared-string booleans without loading sharedStrings");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "A1+1",
        "WorksheetEditor should materialize formulas without loading sharedStrings");
    check(!sheet.has_pending_changes(),
        "lazy sharedStrings non-index materialization should start clean");
    check(!editor.has_pending_changes(),
        "lazy sharedStrings non-index materialization should not dirty the editor");

    sheet.set_cell("D1", fastxlsx::CellValue::text("inline-after-lazy-sharedStrings"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>inline-after-lazy-sharedStrings</t></is></c>)",
        "dirty lazy sharedStrings projection should still write new text as inlineStr");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty lazy sharedStrings projection should not introduce shared string indexes");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy sharedStrings projection should preserve the source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="missingSharedStrings.xml")",
        "dirty lazy sharedStrings projection should not repair the stale workbook relationship");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy sharedStrings materialization should not mutate the source package");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(2.0)},
        {1, 2, fastxlsx::CellValue::boolean(true)},
        {1, 3, fastxlsx::CellValue::formula("A1+1")},
        {1, 4, fastxlsx::CellValue::text("inline-after-lazy-sharedStrings")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        "workbook sharedStrings relationship targets an unknown package part",
        "lazy missing sharedStrings target dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy missing sharedStrings target post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy missing sharedStrings target post-dirty no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy missing sharedStrings target post-dirty no-op save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy missing sharedStrings target post-dirty no-op save should not mutate dirty output");
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        "workbook sharedStrings relationship targets an unknown package part",
        "lazy missing sharedStrings target post-dirty no-op output");

    sheet.set_cell("E1", fastxlsx::CellValue::text("lazy-missing-target-reuse"));
    check(sheet.has_pending_changes(),
        "lazy missing sharedStrings target post-noop reuse edit should dirty Data");
    check(!editor.last_edit_error().has_value(),
        "lazy missing sharedStrings target post-noop reuse edit should keep last_edit_error clear");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "lazy missing sharedStrings target post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="E1" t="inlineStr"><is><t>lazy-missing-target-reuse</t></is></c>)",
        "lazy missing sharedStrings target post-noop reuse save should include the later inline text edit");
    check_not_contains(post_noop_reuse_worksheet, R"(t="s")",
        "lazy missing sharedStrings target post-noop reuse save should not introduce shared string indexes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "lazy missing sharedStrings target post-noop reuse save should preserve source sharedStrings bytes");
    check_contains(post_noop_reuse_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="missingSharedStrings.xml")",
        "lazy missing sharedStrings target post-noop reuse save should not repair the stale workbook relationship");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy missing sharedStrings target post-noop reuse save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy missing sharedStrings target post-noop reuse save should not mutate dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy missing sharedStrings target post-noop reuse save should not mutate dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(2.0)},
        {1, 2, fastxlsx::CellValue::boolean(true)},
        {1, 3, fastxlsx::CellValue::formula("A1+1")},
        {1, 4, fastxlsx::CellValue::text("inline-after-lazy-sharedStrings")},
        {1, 5, fastxlsx::CellValue::text("lazy-missing-target-reuse")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        "workbook sharedStrings relationship targets an unknown package part",
        "lazy missing sharedStrings target post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy missing sharedStrings target post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "lazy missing sharedStrings target post-noop reuse no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy missing sharedStrings target post-noop reuse no-op save should not mutate source");
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        "workbook sharedStrings relationship targets an unknown package part",
        "lazy missing sharedStrings target post-noop reuse no-op output");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship targets an unknown package part",
        "usable-after-lazy-missing-sharedstrings-target",
        "lazy missing sharedStrings target",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-post-noop-reuse-noop-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(7.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("duplicate-rel-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(</Relationships>)",
        R"(<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(</Relationships>)");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 7.0,
        "duplicate sharedStrings relationships should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy duplicate sharedStrings relationship read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-duplicate-rel-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy duplicate-rel projection should preserve source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"), R"(Id="rId99")",
        "dirty lazy duplicate-rel projection should preserve duplicate relationship bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-duplicate-rel-lazy-load</t></is></c>)",
        "dirty lazy duplicate-rel projection should still write new text as inlineStr");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(7.0)},
        {1, 2, fastxlsx::CellValue::text("after-duplicate-rel-lazy-load")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "lazy duplicate sharedStrings relationship dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy duplicate sharedStrings relationship post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy duplicate sharedStrings relationship post-dirty no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy duplicate sharedStrings relationship post-dirty no-op save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy duplicate sharedStrings relationship post-dirty no-op save should not mutate dirty output");
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "lazy duplicate sharedStrings relationship post-dirty no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("duplicate-rel-reuse"));
    check(sheet.has_pending_changes(),
        "lazy duplicate sharedStrings relationship post-noop reuse edit should dirty Data");
    check(!editor.last_edit_error().has_value(),
        "lazy duplicate sharedStrings relationship post-noop reuse edit should keep last_edit_error clear");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "lazy duplicate sharedStrings relationship post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="C1" t="inlineStr"><is><t>duplicate-rel-reuse</t></is></c>)",
        "lazy duplicate sharedStrings relationship post-noop reuse save should include the later inline text edit");
    check_not_contains(post_noop_reuse_worksheet, R"(t="s")",
        "lazy duplicate sharedStrings relationship post-noop reuse save should not introduce shared string indexes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "lazy duplicate sharedStrings relationship post-noop reuse save should preserve source sharedStrings bytes");
    check_contains(post_noop_reuse_entries.at("xl/_rels/workbook.xml.rels"), R"(Id="rId99")",
        "lazy duplicate sharedStrings relationship post-noop reuse save should preserve duplicate relationship bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy duplicate sharedStrings relationship post-noop reuse save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy duplicate sharedStrings relationship post-noop reuse save should not mutate dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy duplicate sharedStrings relationship post-noop reuse save should not mutate dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(7.0)},
        {1, 2, fastxlsx::CellValue::text("after-duplicate-rel-lazy-load")},
        {1, 3, fastxlsx::CellValue::text("duplicate-rel-reuse")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "lazy duplicate sharedStrings relationship post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy duplicate sharedStrings relationship post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "lazy duplicate sharedStrings relationship post-noop reuse no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy duplicate sharedStrings relationship post-noop reuse no-op save should not mutate source");
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "lazy duplicate sharedStrings relationship post-noop reuse no-op output");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "usable-after-lazy-duplicate-sharedstrings-relationship",
        "lazy duplicate sharedStrings relationship",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-post-noop-reuse-noop-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(11.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("malformed-xml-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") = R"(<notSst/>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 11.0,
        "malformed sharedStrings XML should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy malformed sharedStrings XML read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-malformed-xml-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == R"(<notSst/>)",
        "dirty lazy malformed-xml projection should preserve malformed sharedStrings bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-malformed-xml-lazy-load</t></is></c>)",
        "dirty lazy malformed-xml projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML materialization should not mutate the source package");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(11.0)},
        {1, 2, fastxlsx::CellValue::text("after-malformed-xml-lazy-load")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        "CellStore sharedStrings loader root is missing an sst element",
        "lazy malformed sharedStrings XML dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy malformed sharedStrings XML post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy malformed sharedStrings XML post-dirty no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML post-dirty no-op save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy malformed sharedStrings XML post-dirty no-op save should not mutate dirty output");
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        "CellStore sharedStrings loader root is missing an sst element",
        "lazy malformed sharedStrings XML post-dirty no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("malformed-xml-reuse"));
    check(sheet.has_pending_changes(),
        "lazy malformed sharedStrings XML post-noop reuse edit should dirty Data");
    check(!editor.last_edit_error().has_value(),
        "lazy malformed sharedStrings XML post-noop reuse edit should keep last_edit_error clear");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "lazy malformed sharedStrings XML post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="C1" t="inlineStr"><is><t>malformed-xml-reuse</t></is></c>)",
        "lazy malformed sharedStrings XML post-noop reuse save should include the later inline text edit");
    check_not_contains(post_noop_reuse_worksheet, R"(t="s")",
        "lazy malformed sharedStrings XML post-noop reuse save should not introduce shared string indexes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == R"(<notSst/>)",
        "lazy malformed sharedStrings XML post-noop reuse save should preserve malformed sharedStrings bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML post-noop reuse save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy malformed sharedStrings XML post-noop reuse save should not mutate dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy malformed sharedStrings XML post-noop reuse save should not mutate dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(11.0)},
        {1, 2, fastxlsx::CellValue::text("after-malformed-xml-lazy-load")},
        {1, 3, fastxlsx::CellValue::text("malformed-xml-reuse")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        "CellStore sharedStrings loader root is missing an sst element",
        "lazy malformed sharedStrings XML post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy malformed sharedStrings XML post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "lazy malformed sharedStrings XML post-noop reuse no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML post-noop reuse no-op save should not mutate source");
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        "CellStore sharedStrings loader root is missing an sst element",
        "lazy malformed sharedStrings XML post-noop reuse no-op output");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "CellStore sharedStrings loader root is missing an sst element",
        "usable-after-lazy-malformed-sharedstrings",
        "lazy malformed sharedStrings XML",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-post-noop-reuse-noop-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(13.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("wrong-content-type-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& content_types = entries.at("[Content_Types].xml");
    replace_first_or_throw(content_types,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 13.0,
        "wrong sharedStrings content type should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy wrong sharedStrings content type read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-wrong-content-type-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy wrong-content-type projection should preserve sharedStrings bytes");
    check_contains(output_entries.at("[Content_Types].xml"),
        R"(PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")",
        "dirty lazy wrong-content-type projection should preserve wrong content type metadata");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-wrong-content-type-lazy-load</t></is></c>)",
        "dirty lazy wrong-content-type projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type materialization should not mutate the source package");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::number(13.0)},
        {1, 2, fastxlsx::CellValue::text("after-wrong-content-type-lazy-load")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "lazy wrong sharedStrings content type dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy wrong sharedStrings content type post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy wrong sharedStrings content type post-dirty no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type post-dirty no-op save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy wrong sharedStrings content type post-dirty no-op save should not mutate dirty output");
    check_reopened_lazy_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "lazy wrong sharedStrings content type post-dirty no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("wrong-content-type-reuse"));
    check(sheet.has_pending_changes(),
        "lazy wrong sharedStrings content type post-noop reuse edit should dirty Data");
    check(!editor.last_edit_error().has_value(),
        "lazy wrong sharedStrings content type post-noop reuse edit should keep last_edit_error clear");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "lazy wrong sharedStrings content type post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="C1" t="inlineStr"><is><t>wrong-content-type-reuse</t></is></c>)",
        "lazy wrong sharedStrings content type post-noop reuse save should include the later inline text edit");
    check_not_contains(post_noop_reuse_worksheet, R"(t="s")",
        "lazy wrong sharedStrings content type post-noop reuse save should not introduce shared string indexes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "lazy wrong sharedStrings content type post-noop reuse save should preserve sharedStrings bytes");
    check_contains(post_noop_reuse_entries.at("[Content_Types].xml"),
        R"(PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")",
        "lazy wrong sharedStrings content type post-noop reuse save should preserve wrong content type metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type post-noop reuse save should not mutate source");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "lazy wrong sharedStrings content type post-noop reuse save should not mutate dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "lazy wrong sharedStrings content type post-noop reuse save should not mutate dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::number(13.0)},
        {1, 2, fastxlsx::CellValue::text("after-wrong-content-type-lazy-load")},
        {1, 3, fastxlsx::CellValue::text("wrong-content-type-reuse")},
    };
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "lazy wrong sharedStrings content type post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "lazy wrong sharedStrings content type post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "lazy wrong sharedStrings content type post-noop reuse no-op output should stay byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type post-noop reuse no-op save should not mutate source");
    check_reopened_lazy_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "lazy wrong sharedStrings content type post-noop reuse no-op output");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "usable-after-lazy-wrong-sharedstrings-content-type",
        "lazy wrong sharedStrings content type",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_materializes_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-a"),
            fastxlsx::CellView::text("A&B <C>")});
        data.append_row({fastxlsx::CellView::text("shared-a")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part for materialization");
    std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    replace_first_or_throw(shared_strings_before, "?><sst",
        "?><?fastxlsx sharedStrings-trivia?>"
        "<?fastxlsx.data-1:probe legal-target?>"
        "<?_fastxlsx legal-start?>"
        "<?:fastxlsx legal-colon-start?>"
        "<?fastxlsx?>"
        "<?xml-stylesheet type=\"text/xsl\" href=\"sharedStrings.xsl\"?><sst");
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_before);
    {
        std::string updated_workbook_rels = source_entries.at("xl/_rels/workbook.xml.rels");
        replace_first_or_throw(updated_workbook_rels,
            R"(Target="sharedStrings.xml")",
            R"(Target="./sharedStrings.xml")");
        rewrite_package_entry_as_stored(
            source, "xl/_rels/workbook.xml.rels", updated_workbook_rels);
    }
    const auto rewritten_source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "shared-a",
        "WorksheetEditor should materialize A1 shared string text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "A&B <C>",
        "WorksheetEditor should decode XML entities from source sharedStrings");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text
            && a2->text_value() == "shared-a",
        "WorksheetEditor should materialize repeated shared string indexes");
    check(shared_strings_before.find("<?fastxlsx sharedStrings-trivia?>")
            != std::string::npos,
        "source sharedStrings success fixture should include prolog processing instruction trivia");
    check(shared_strings_before.find("<?fastxlsx.data-1:probe legal-target?>")
            != std::string::npos,
        "source sharedStrings success fixture should include legal PI target continuation trivia");
    check(shared_strings_before.find("<?_fastxlsx legal-start?>") != std::string::npos,
        "source sharedStrings success fixture should include underscore-start PI target trivia");
    check(shared_strings_before.find("<?:fastxlsx legal-colon-start?>") != std::string::npos,
        "source sharedStrings success fixture should include colon-start PI target trivia");
    check(shared_strings_before.find("<?fastxlsx?>") != std::string::npos,
        "source sharedStrings success fixture should include empty-data PI trivia");
    check(shared_strings_before.find("<?xml-stylesheet") != std::string::npos,
        "source sharedStrings success fixture should include xml-stylesheet PI trivia");
    check(shared_strings_before.find(R"(standalone="yes")") != std::string::npos,
        "source sharedStrings success fixture should include legal standalone declaration metadata");
    check(!sheet.has_pending_changes(),
        "read-only source sharedStrings materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source sharedStrings materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source sharedStrings materialization should not queue Patch edits");

    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-second-noop-output.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-dirty-noop-output.xlsx");
    const std::filesystem::path post_dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-post-dirty-output.xlsx");
    const std::filesystem::path post_dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-post-dirty-noop-output.xlsx");
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "no-op save_as after source sharedStrings materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "no-op save_as after source sharedStrings materialization should keep WorkbookEditor clean");
    const auto noop_output_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_output_entries == rewritten_source_entries,
        "no-op save_as after source sharedStrings materialization should copy rewritten source entries");
    const ReopenedLazySharedStringsCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("shared-a")},
        {1, 2, fastxlsx::CellValue::text("A&B <C>")},
        {2, 1, fastxlsx::CellValue::text("shared-a")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        noop_cells,
        fastxlsx::CellRange {1, 1, 2, 2},
        "source sharedStrings no-op output");

    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "second no-op save_as after source sharedStrings materialization should keep Data clean");
    check(!editor.has_pending_changes(),
        "second no-op save_as after source sharedStrings materialization should keep WorkbookEditor clean");
    check(editor.pending_change_count() == 0,
        "second no-op save_as after source sharedStrings materialization should not queue Patch edits");
    const auto second_noop_entries = fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_output_entries,
        "second no-op save_as after source sharedStrings materialization should keep output byte-stable");
    check(second_noop_entries == rewritten_source_entries,
        "second no-op save_as after source sharedStrings materialization should keep source-copy bytes");
    check(fastxlsx::test::read_zip_entries(source) == rewritten_source_entries,
        "second no-op save_as after source sharedStrings materialization should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_output_entries,
        "second no-op save_as after source sharedStrings materialization should not mutate the first no-op output");
    check_reopened_shared_strings_output(
        second_noop_output,
        noop_cells,
        fastxlsx::CellRange {1, 1, 2, 2},
        "source sharedStrings second no-op output");

    sheet.set_cell("C3", fastxlsx::CellValue::text("new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "flushed WorksheetEditor source shared string should reuse its existing shared string index");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "flushed WorksheetEditor source shared string should keep the decoded table index");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="s"><v>0</v></c>)",
        "flushed WorksheetEditor repeated source text should reuse the same shared string index");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="s"><v>3</v></c>)",
        "new WorksheetEditor text should append to the existing sharedStrings table");
    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<?fastxlsx sharedStrings-trivia?>)",
        "WorksheetEditor sharedStrings append should preserve source prolog trivia");
    check_contains(shared_strings_after,
        R"(<si><t>new-inline</t></si></sst>)",
        "WorksheetEditor sharedStrings append should add the dirty text item before the sst close");
    check_contains(shared_strings_after, R"(count="5")",
        "WorksheetEditor sharedStrings append should advance the conservative count metadata");
    check_contains(shared_strings_after, R"(uniqueCount="4")",
        "WorksheetEditor sharedStrings append should advance uniqueCount metadata");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("shared-a")},
        {1, 2, fastxlsx::CellValue::text("A&B <C>")},
        {2, 1, fastxlsx::CellValue::text("shared-a")},
        {3, 3, fastxlsx::CellValue::text("new-inline")},
    };
    check_reopened_shared_strings_dirty_output(
        output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 3, 3},
        "source sharedStrings dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == rewritten_source_entries,
        "source sharedStrings post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == rewritten_source_entries,
        "source sharedStrings post-dirty no-op save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "source sharedStrings post-dirty no-op save should not mutate the second no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 3, 3},
        "source sharedStrings post-dirty no-op output");

    sheet.set_cell("D3", fastxlsx::CellValue::text("second-inline"));
    check(sheet.has_pending_changes(),
        "source sharedStrings post-dirty edit should dirty Data again");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "source sharedStrings post-dirty edit before save");
    check(!editor.last_edit_error().has_value(),
        "source sharedStrings post-dirty edit before save should keep last_edit_error clear");
    editor.save_as(post_dirty_output);

    const auto post_dirty_entries = fastxlsx::test::read_zip_entries(post_dirty_output);
    const std::string post_dirty_worksheet_xml =
        post_dirty_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_dirty_worksheet_xml,
        R"(<c r="C3" t="s"><v>3</v></c>)",
        "source sharedStrings post-dirty edit should preserve the first appended shared string index");
    check_contains(post_dirty_worksheet_xml,
        R"(<c r="D3" t="s"><v>4</v></c>)",
        "source sharedStrings post-dirty edit should append the next shared string index");
    const std::string post_dirty_shared_strings =
        post_dirty_entries.at("xl/sharedStrings.xml");
    check_contains(post_dirty_shared_strings,
        R"(<si><t>new-inline</t></si><si><t>second-inline</t></si></sst>)",
        "source sharedStrings post-dirty edit should append new strings in save order");
    check_contains(post_dirty_shared_strings, R"(count="6")",
        "source sharedStrings post-dirty edit should advance conservative count again");
    check_contains(post_dirty_shared_strings, R"(uniqueCount="5")",
        "source sharedStrings post-dirty edit should advance uniqueCount again");
    check(!sheet.has_pending_changes(),
        "source sharedStrings post-dirty edit save should clean Data again");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "source sharedStrings post-dirty edit save should clear materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "source sharedStrings post-dirty edit save");
    check(!editor.last_edit_error().has_value(),
        "source sharedStrings post-dirty edit save should keep last_edit_error clear");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings post-dirty edit save should not mutate the prior dirty no-op output");

    const ReopenedLazySharedStringsCell post_dirty_expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("shared-a")},
        {1, 2, fastxlsx::CellValue::text("A&B <C>")},
        {2, 1, fastxlsx::CellValue::text("shared-a")},
        {3, 3, fastxlsx::CellValue::text("new-inline")},
        {3, 4, fastxlsx::CellValue::text("second-inline")},
    };
    check_reopened_shared_strings_dirty_output(
        post_dirty_output,
        post_dirty_expected_cells,
        fastxlsx::CellRange {1, 1, 3, 4},
        "source sharedStrings post-dirty edit output");

    editor.save_as(post_dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings post-dirty edit clean no-op save should keep Data clean");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "source sharedStrings post-dirty edit clean no-op save should keep materialized diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "source sharedStrings post-dirty edit clean no-op save");
    check(!editor.last_edit_error().has_value(),
        "source sharedStrings post-dirty edit clean no-op save should keep last_edit_error clear");
    check(fastxlsx::test::read_zip_entries(post_dirty_noop_output)
            == post_dirty_entries,
        "source sharedStrings post-dirty edit clean no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "source sharedStrings post-dirty edit clean no-op save should not mutate the first dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings post-dirty edit clean no-op save should not mutate the dirty no-op output");
    check_reopened_shared_strings_dirty_output(
        post_dirty_noop_output,
        post_dirty_expected_cells,
        fastxlsx::CellRange {1, 1, 3, 4},
        "source sharedStrings post-dirty edit clean no-op output");
}

void test_public_worksheet_editor_reuses_duplicate_dirty_shared_strings()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-duplicate-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-duplicate-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-duplicate-dirty-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("seed-text")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::number(42.0)});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "duplicate dirty sharedStrings fixture should start with a sharedStrings part");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "seed-text",
        "duplicate dirty sharedStrings fixture should materialize source text");
    check(!sheet.has_pending_changes(),
        "duplicate dirty sharedStrings materialization should start clean");
    check(!editor.has_pending_changes(),
        "duplicate dirty sharedStrings materialization should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("dirty-repeat"));
    sheet.set_cell("C2", fastxlsx::CellValue::text("dirty-repeat"));
    check(sheet.has_pending_changes(),
        "duplicate dirty sharedStrings edits should dirty Data");
    check(editor.pending_materialized_cell_count() == 3,
        "duplicate dirty sharedStrings edits should track all represented cells");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "duplicate dirty sharedStrings save should refresh worksheet dimension");
    check_contains(worksheet_xml, R"(<c r="A1" t="s"><v>0</v></c>)",
        "duplicate dirty sharedStrings save should keep source text on its source index");
    check_contains(worksheet_xml, R"(<c r="B1" t="s"><v>1</v></c>)",
        "duplicate dirty sharedStrings save should append one index for the first dirty text");
    check_contains(worksheet_xml, R"(<c r="C2" t="s"><v>1</v></c>)",
        "duplicate dirty sharedStrings save should reuse the appended index for duplicate dirty text");
    check_not_contains(worksheet_xml, R"(inlineStr)",
        "duplicate dirty sharedStrings save should keep the appendable sharedStrings projection");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "duplicate dirty sharedStrings save should preserve untouched sheet bytes");

    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<si><t>seed-text</t></si><si><t>dirty-repeat</t></si></sst>)",
        "duplicate dirty sharedStrings save should append the repeated dirty text once");
    check_contains(shared_strings_after, R"(count="3")",
        "duplicate dirty sharedStrings save should count repeated dirty cell references");
    check_contains(shared_strings_after, R"(uniqueCount="2")",
        "duplicate dirty sharedStrings save should count only unique shared string items");
    check(!sheet.has_pending_changes(),
        "duplicate dirty sharedStrings save should clean Data");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "duplicate dirty sharedStrings save should clear materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "duplicate dirty sharedStrings save");
    check(!editor.last_edit_error().has_value(),
        "duplicate dirty sharedStrings save should keep last_edit_error clear");

    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("seed-text")},
        {1, 2, fastxlsx::CellValue::text("dirty-repeat")},
        {2, 3, fastxlsx::CellValue::text("dirty-repeat")},
    };
    check_reopened_shared_strings_dirty_output(
        output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "duplicate dirty sharedStrings output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "duplicate dirty sharedStrings post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "duplicate dirty sharedStrings post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "duplicate dirty sharedStrings post-dirty no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "duplicate dirty sharedStrings post-dirty no-op output");
}

void test_public_worksheet_editor_reuses_existing_dirty_shared_strings_without_rewriting_table()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-existing-dirty-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-existing-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-existing-dirty-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("source-reuse")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::number(7.0)});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "source-reuse",
        "existing dirty sharedStrings fixture should materialize source text");
    check(!sheet.has_pending_changes(),
        "existing dirty sharedStrings materialization should start clean");

    sheet.set_cell("B1", fastxlsx::CellValue::text("source-reuse"));
    sheet.set_cell("C2", fastxlsx::CellValue::text("source-reuse"));
    check(sheet.has_pending_changes(),
        "existing dirty sharedStrings edits should dirty Data");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "existing dirty sharedStrings save should refresh worksheet dimension");
    check_contains(worksheet_xml, R"(<c r="A1" t="s"><v>0</v></c>)",
        "existing dirty sharedStrings save should keep the source cell on index 0");
    check_contains(worksheet_xml, R"(<c r="B1" t="s"><v>0</v></c>)",
        "existing dirty sharedStrings save should reuse index 0 for the first dirty cell");
    check_contains(worksheet_xml, R"(<c r="C2" t="s"><v>0</v></c>)",
        "existing dirty sharedStrings save should reuse index 0 for the second dirty cell");
    check_not_contains(worksheet_xml, R"(inlineStr)",
        "existing dirty sharedStrings save should not fall back to inline text");
    check(output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "existing dirty sharedStrings save should not rewrite the sharedStrings table");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "existing dirty sharedStrings save should preserve untouched sheet bytes");
    check(!sheet.has_pending_changes(),
        "existing dirty sharedStrings save should clean Data");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "existing dirty sharedStrings save should clear materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "existing dirty sharedStrings save");
    check(!editor.last_edit_error().has_value(),
        "existing dirty sharedStrings save should keep last_edit_error clear");

    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("source-reuse")},
        {1, 2, fastxlsx::CellValue::text("source-reuse")},
        {2, 3, fastxlsx::CellValue::text("source-reuse")},
    };
    check_reopened_shared_strings_dirty_output(
        output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "existing dirty sharedStrings output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "existing dirty sharedStrings post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "existing dirty sharedStrings post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "existing dirty sharedStrings post-dirty no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "existing dirty sharedStrings post-dirty no-op output");
}

struct ShiftedSharedStringsXmlCell {
    std::string_view reference;
    std::uint32_t shared_string_index = 0;
};

std::string shifted_shared_string_cell_xml(
    std::string_view reference,
    std::uint32_t shared_string_index)
{
    return "<c r=\"" + std::string(reference) + "\" t=\"s\"><v>"
        + std::to_string(shared_string_index) + "</v></c>";
}

void check_live_shifted_shared_strings_cells(
    fastxlsx::WorksheetEditor& sheet,
    std::span<const ReopenedLazySharedStringsCell> expected_cells,
    const fastxlsx::CellRange& expected_used_range,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(sheet.cell_count() == expected_cells.size(),
        prefix + " should expose the expected live sparse count");

    const std::optional<fastxlsx::CellRange> range = sheet.used_range();
    check(range.has_value()
            && shared_strings_ranges_equal(*range, expected_used_range),
        prefix + " should expose the expected live used range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        sheet.sparse_cells();
    check(all_cells.size() == expected_cells.size(),
        prefix + " sparse_cells should expose the expected live cells");
    if (all_cells.size() == expected_cells.size()) {
        for (std::size_t index = 0; index < expected_cells.size(); ++index) {
            check(shared_strings_snapshot_matches(all_cells[index], expected_cells[index]),
                prefix + " sparse_cells should preserve shifted live values");
        }
    }

    for (const ReopenedLazySharedStringsCell& expected : expected_cells) {
        const fastxlsx::CellValue actual =
            sheet.get_cell(expected.row, expected.column);
        check(cell_values_equal(actual, expected.value),
            prefix + " should read the expected shifted live cell");
    }
}

void test_public_worksheet_editor_shifts_source_shared_strings_records()
{
    const auto write_shift_source = [](std::string_view name) {
        const std::filesystem::path source = artifact(name);
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("row1-a"),
            fastxlsx::CellView::text("row1-b"),
            fastxlsx::CellView::text("row1-c")});
        data.append_row({fastxlsx::CellView::text("row2-a"),
            fastxlsx::CellView::text("row2-b"),
            fastxlsx::CellView::text("row2-c")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-rich-shared-shift")});
        writer.close();
        const std::string rich_shared_strings =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="7" uniqueCount="7">)"
            R"(<si><r><t>row1-</t></r><r><t>A&amp;</t></r><r><t xml:space="preserve"> top </t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh></si>)"
            R"(<si><t xml:space="preserve"> row1-b </t></si>)"
            R"(<si><r><t>row1-C</t></r><r><t xml:space="preserve"> tail</t></r></si>)"
            R"(<si><t>row2-a</t></si>)"
            R"(<si><r><t>row2-</t></r><r><t>B&amp;rich</t></r></si>)"
            R"(<si><t xml:space="preserve">row2-c </t></si>)"
            R"(<si><t>keep-rich-shared-shift</t></si>)"
            R"(</sst>)";
        rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", rich_shared_strings);
        return source;
    };

    const auto run_shift_case =
        [&](std::string_view artifact_suffix,
            std::string_view scenario,
            auto mutate,
            const fastxlsx::CellRange& expected_range,
            std::span<const ReopenedLazySharedStringsCell> expected_cells,
            std::span<const ShiftedSharedStringsXmlCell> expected_xml_cells,
            const fastxlsx::CellRange& reopened_range,
            const ReopenedLazySharedStringsCell& reopened_edit,
            const ShiftedSharedStringsXmlCell& reopened_edit_xml) {
            const std::string suffix(artifact_suffix);
            const std::string scenario_text(scenario);
            const std::filesystem::path source = write_shift_source(
                "fastxlsx-workbook-editor-public-sharedstrings-shift-"
                + suffix + "-source.xlsx");
            const std::filesystem::path output = artifact(
                "fastxlsx-workbook-editor-public-sharedstrings-shift-"
                + suffix + "-output.xlsx");
            const std::filesystem::path noop_output = artifact(
                "fastxlsx-workbook-editor-public-sharedstrings-shift-"
                + suffix + "-noop-output.xlsx");
            const std::filesystem::path reopened_output = artifact(
                "fastxlsx-workbook-editor-public-sharedstrings-shift-"
                + suffix + "-reopened-output.xlsx");
            const std::filesystem::path reopened_noop_output = artifact(
                "fastxlsx-workbook-editor-public-sharedstrings-shift-"
                + suffix + "-reopened-noop-output.xlsx");
            const auto source_entries = fastxlsx::test::read_zip_entries(source);
            const std::string shared_strings_before =
                source_entries.at("xl/sharedStrings.xml");
            check_contains(shared_strings_before,
                R"(<r><t>row1-</t></r><r><t>A&amp;</t></r>)",
                scenario_text + " fixture should include rich shared string markup");
            check_contains(shared_strings_before,
                R"(<si><t xml:space="preserve"> row1-b </t></si>)",
                scenario_text + " fixture should include xml:space shared string text");

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
            check(sheet.cell_count() == 6,
                scenario_text + " setup should materialize all source shared strings");
            check(!sheet.has_pending_changes(),
                scenario_text + " setup should start clean");
            check(!editor.has_pending_changes(),
                scenario_text + " setup should not dirty WorkbookEditor");

            mutate(sheet);

            check(sheet.has_pending_changes(),
                scenario_text + " should dirty Data");
            check(editor.has_pending_changes(),
                scenario_text + " should dirty WorkbookEditor");
            check(editor.pending_materialized_cell_count() == expected_cells.size(),
                scenario_text + " should track shifted materialized cell count");
            check_live_shifted_shared_strings_cells(
                sheet, expected_cells, expected_range, scenario);

            editor.save_as(output);
            check(!sheet.has_pending_changes(),
                scenario_text + " save should keep Data clean");
            check(editor.pending_materialized_worksheet_names().empty() &&
                    editor.pending_materialized_cell_count() == 0 &&
                    editor.estimated_pending_materialized_memory_usage() == 0,
                scenario_text + " save should clear materialized diagnostics");
            check_workbook_editor_no_replacement_diagnostics(
                editor, scenario_text + " save");
            check(!editor.last_edit_error().has_value(),
                scenario_text + " save should keep last_edit_error clear");
            const auto output_entries = fastxlsx::test::read_zip_entries(output);
            const std::string worksheet_xml =
                output_entries.at("xl/worksheets/sheet1.xml");
            for (const ShiftedSharedStringsXmlCell& expected_xml : expected_xml_cells) {
                check_contains(worksheet_xml,
                    shifted_shared_string_cell_xml(
                        expected_xml.reference, expected_xml.shared_string_index),
                    scenario_text + " save should write shifted shared string cell "
                        + std::string(expected_xml.reference));
            }
            check_not_contains(worksheet_xml, R"(inlineStr)",
                scenario_text + " save should keep the appendable sharedStrings projection");
            check(output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
                scenario_text + " save should not rewrite the sharedStrings table");
            check(output_entries.at("xl/worksheets/sheet2.xml")
                    == source_entries.at("xl/worksheets/sheet2.xml"),
                scenario_text + " save should preserve untouched worksheet bytes");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " save should not mutate the source package");
            check_reopened_shared_strings_dirty_output(
                output, expected_cells, expected_range, scenario);

            editor.save_as(noop_output);
            check(!sheet.has_pending_changes(),
                scenario_text + " no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " no-op save should not mutate the source package");
            check_reopened_shared_strings_dirty_output(
                noop_output,
                expected_cells,
                expected_range,
                scenario_text + " no-op output");

            fastxlsx::WorkbookEditor reopened_editor =
                fastxlsx::WorkbookEditor::open(noop_output);
            fastxlsx::WorksheetEditor reopened_sheet =
                reopened_editor.worksheet("Data");
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh reopen should start clean");
            reopened_sheet.set_cell(
                reopened_edit.row, reopened_edit.column, reopened_edit.value);
            check(reopened_sheet.has_pending_changes(),
                scenario_text + " fresh reopen edit should dirty Data");
            reopened_editor.save_as(reopened_output);
            const auto reopened_entries =
                fastxlsx::test::read_zip_entries(reopened_output);
            const std::string reopened_worksheet_xml =
                reopened_entries.at("xl/worksheets/sheet1.xml");
            check_contains(reopened_worksheet_xml,
                shifted_shared_string_cell_xml(
                    reopened_edit_xml.reference,
                    reopened_edit_xml.shared_string_index),
                scenario_text + " fresh reopen save should write the later shared string edit");
            check_not_contains(reopened_worksheet_xml, R"(inlineStr)",
                scenario_text + " fresh reopen save should keep sharedStrings projection");
            check_contains(reopened_entries.at("xl/sharedStrings.xml"),
                "<si><t>" + reopened_edit.value.text_value() + "</t></si></sst>",
                scenario_text + " fresh reopen save should append the later text item");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh reopen save should not mutate its input package");
            check(fastxlsx::test::read_zip_entries(source) == source_entries,
                scenario_text + " fresh reopen save should not mutate the source package");

            std::vector<ReopenedLazySharedStringsCell> reopened_expected(
                expected_cells.begin(), expected_cells.end());
            reopened_expected.push_back(reopened_edit);
            check_reopened_shared_strings_dirty_output(
                reopened_output,
                std::span<const ReopenedLazySharedStringsCell>(
                    reopened_expected.data(), reopened_expected.size()),
                reopened_range,
                scenario_text + " fresh reopen output");

            reopened_editor.save_as(reopened_noop_output);
            check(!reopened_sheet.has_pending_changes(),
                scenario_text + " fresh reopen no-op save should keep Data clean");
            check(fastxlsx::test::read_zip_entries(reopened_noop_output)
                    == reopened_entries,
                scenario_text + " fresh reopen no-op save should keep output byte-stable");
            check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
                scenario_text + " fresh reopen no-op save should not mutate its input package");
            check_reopened_shared_strings_dirty_output(
                reopened_noop_output,
                std::span<const ReopenedLazySharedStringsCell>(
                    reopened_expected.data(), reopened_expected.size()),
                reopened_range,
                scenario_text + " fresh reopen no-op output");
        };

    const ReopenedLazySharedStringsCell insert_rows_expected[] = {
        {1, 1, fastxlsx::CellValue::text("row1-A& top ")},
        {1, 2, fastxlsx::CellValue::text(" row1-b ")},
        {1, 3, fastxlsx::CellValue::text("row1-C tail")},
        {3, 1, fastxlsx::CellValue::text("row2-a")},
        {3, 2, fastxlsx::CellValue::text("row2-B&rich")},
        {3, 3, fastxlsx::CellValue::text("row2-c ")},
    };
    const ShiftedSharedStringsXmlCell insert_rows_xml[] = {
        {"A1", 0},
        {"B1", 1},
        {"C1", 2},
        {"A3", 3},
        {"B3", 4},
        {"C3", 5},
    };
    run_shift_case("insert-rows",
        "source sharedStrings insert_rows shift",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(2, 1); },
        fastxlsx::CellRange {1, 1, 3, 3},
        insert_rows_expected,
        insert_rows_xml,
        fastxlsx::CellRange {1, 1, 3, 4},
        ReopenedLazySharedStringsCell {
            3, 4, fastxlsx::CellValue::text("insert-rows-reopened")},
        ShiftedSharedStringsXmlCell {"D3", 7});

    const ReopenedLazySharedStringsCell insert_columns_expected[] = {
        {1, 1, fastxlsx::CellValue::text("row1-A& top ")},
        {1, 3, fastxlsx::CellValue::text(" row1-b ")},
        {1, 4, fastxlsx::CellValue::text("row1-C tail")},
        {2, 1, fastxlsx::CellValue::text("row2-a")},
        {2, 3, fastxlsx::CellValue::text("row2-B&rich")},
        {2, 4, fastxlsx::CellValue::text("row2-c ")},
    };
    const ShiftedSharedStringsXmlCell insert_columns_xml[] = {
        {"A1", 0},
        {"C1", 1},
        {"D1", 2},
        {"A2", 3},
        {"C2", 4},
        {"D2", 5},
    };
    run_shift_case("insert-columns",
        "source sharedStrings insert_columns shift",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(2, 1); },
        fastxlsx::CellRange {1, 1, 2, 4},
        insert_columns_expected,
        insert_columns_xml,
        fastxlsx::CellRange {1, 1, 2, 5},
        ReopenedLazySharedStringsCell {
            2, 5, fastxlsx::CellValue::text("insert-columns-reopened")},
        ShiftedSharedStringsXmlCell {"E2", 7});

    const ReopenedLazySharedStringsCell delete_rows_expected[] = {
        {1, 1, fastxlsx::CellValue::text("row2-a")},
        {1, 2, fastxlsx::CellValue::text("row2-B&rich")},
        {1, 3, fastxlsx::CellValue::text("row2-c ")},
    };
    const ShiftedSharedStringsXmlCell delete_rows_xml[] = {
        {"A1", 3},
        {"B1", 4},
        {"C1", 5},
    };
    run_shift_case("delete-rows",
        "source sharedStrings delete_rows shift",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(1, 1); },
        fastxlsx::CellRange {1, 1, 1, 3},
        delete_rows_expected,
        delete_rows_xml,
        fastxlsx::CellRange {1, 1, 1, 4},
        ReopenedLazySharedStringsCell {
            1, 4, fastxlsx::CellValue::text("delete-rows-reopened")},
        ShiftedSharedStringsXmlCell {"D1", 7});

    const ReopenedLazySharedStringsCell delete_columns_expected[] = {
        {1, 1, fastxlsx::CellValue::text(" row1-b ")},
        {1, 2, fastxlsx::CellValue::text("row1-C tail")},
        {2, 1, fastxlsx::CellValue::text("row2-B&rich")},
        {2, 2, fastxlsx::CellValue::text("row2-c ")},
    };
    const ShiftedSharedStringsXmlCell delete_columns_xml[] = {
        {"A1", 1},
        {"B1", 2},
        {"A2", 4},
        {"B2", 5},
    };
    run_shift_case("delete-columns",
        "source sharedStrings delete_columns shift",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(1, 1); },
        fastxlsx::CellRange {1, 1, 2, 2},
        delete_columns_expected,
        delete_columns_xml,
        fastxlsx::CellRange {1, 1, 2, 3},
        ReopenedLazySharedStringsCell {
            2, 3, fastxlsx::CellValue::text("delete-columns-reopened")},
        ShiftedSharedStringsXmlCell {"C2", 7});
}

void test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations()
{
    struct LegalDeclarationCase {
        std::string_view name;
        std::string_view declaration;
        std::string_view expected_text;
    };

    const std::array<LegalDeclarationCase, 2> cases{{
        {"single-quoted-version-1-1-with-encoding-and-standalone-no",
            "<?xml version='1.1' encoding='UTF_8-Test.1' standalone='no'?>",
            "legal-declaration-version-1-1"},
        {"version-only-single-quoted",
            "<?xml version='1.0'?>",
            "legal-declaration-version-only"},
    }};

    for (const LegalDeclarationCase& test_case : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-source.xlsx");
        const std::filesystem::path noop_output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-noop-output.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-output.xlsx");
        const std::filesystem::path dirty_noop_output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-dirty-noop-output.xlsx");
        const std::filesystem::path post_noop_reuse_output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-post-noop-reuse-output.xlsx");
        const std::filesystem::path post_noop_reuse_noop_output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-post-noop-reuse-noop-output.xlsx");
        {
            fastxlsx::WorkbookWriterOptions options;
            options.string_strategy = fastxlsx::StringStrategy::SharedString;
            fastxlsx::WorkbookWriter writer =
                fastxlsx::WorkbookWriter::create(source, options);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("declaration-placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-legal-declaration")});
            writer.close();
        }

        const std::string shared_strings_xml =
            std::string(test_case.declaration)
            + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
              R"(<si><t>)"
            + std::string(test_case.expected_text)
            + R"(</t></si><si><t>keep-legal-declaration</t></si></sst>)";
        rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_xml);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
        check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
                && a1->text_value() == test_case.expected_text,
            std::string(test_case.name)
                + " should materialize source sharedStrings text");
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should start clean");
        check(!editor.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            std::string(test_case.name)
                + " legal declaration materialization should not queue Patch edits");

        editor.save_as(noop_output);
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration no-op save should keep Data clean");
        check(!editor.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration no-op save should keep WorkbookEditor clean");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            std::string(test_case.name)
                + " legal declaration no-op save should copy source entries");
        const ReopenedLazySharedStringsCell noop_cells[] = {
            {1, 1, fastxlsx::CellValue::text(std::string(test_case.expected_text))},
        };
        check_reopened_shared_strings_output(
            noop_output,
            noop_cells,
            fastxlsx::CellRange {1, 1, 1, 1},
            std::string(test_case.name) + " legal declaration no-op output");

        sheet.set_cell("B2", fastxlsx::CellValue::text("legal-declaration-new-inline"));
        editor.save_as(output);

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="s"><v>0</v></c>)",
            std::string(test_case.name)
                + " dirty projection should reuse materialized shared string index");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="s"><v>2</v></c>)",
            std::string(test_case.name)
                + " dirty projection should append edits beside legal declaration source text");
        check_contains(output_entries.at("xl/sharedStrings.xml"),
            R"(<si><t>legal-declaration-new-inline</t></si></sst>)",
            std::string(test_case.name)
                + " dirty projection should append legal declaration dirty text to sharedStrings");
        check_contains(output_entries.at("xl/sharedStrings.xml"), R"(count="3")",
            std::string(test_case.name)
                + " dirty projection should update legal declaration count metadata");
        check_contains(output_entries.at("xl/sharedStrings.xml"), R"(uniqueCount="3")",
            std::string(test_case.name)
                + " dirty projection should update legal declaration uniqueCount metadata");
        check(output_entries.at("xl/worksheets/sheet2.xml")
                == source_entries.at("xl/worksheets/sheet2.xml"),
            std::string(test_case.name)
                + " dirty projection should preserve untouched sheet bytes");
        const ReopenedLazySharedStringsCell expected_cells[] = {
            {1, 1, fastxlsx::CellValue::text(std::string(test_case.expected_text))},
            {2, 2, fastxlsx::CellValue::text("legal-declaration-new-inline")},
        };
        check_reopened_shared_strings_dirty_output(
            output,
            expected_cells,
            fastxlsx::CellRange {1, 1, 2, 2},
            std::string(test_case.name) + " legal declaration dirty output");

        editor.save_as(dirty_noop_output);
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration post-dirty no-op save should keep Data clean");
        check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
            std::string(test_case.name)
                + " legal declaration post-dirty no-op save should keep output byte-stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(test_case.name)
                + " legal declaration post-dirty no-op save should not mutate the source package");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            std::string(test_case.name)
                + " legal declaration post-dirty no-op save should not mutate the prior no-op output");
        check_reopened_shared_strings_dirty_output(
            dirty_noop_output,
            expected_cells,
            fastxlsx::CellRange {1, 1, 2, 2},
            std::string(test_case.name)
                + " legal declaration post-dirty no-op output");

        sheet.set_cell("C3", fastxlsx::CellValue::text("legal-declaration-reuse"));
        check(sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration post-noop reuse edit should dirty Data");
        check(editor.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration post-noop reuse edit should dirty WorkbookEditor");
        editor.save_as(post_noop_reuse_output);
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should keep Data clean");
        const auto post_noop_reuse_entries =
            fastxlsx::test::read_zip_entries(post_noop_reuse_output);
        const std::string post_noop_reuse_worksheet =
            post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
        check_contains(post_noop_reuse_worksheet,
            R"(<c r="C3" t="s"><v>3</v></c>)",
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should include the later text edit");
        const std::string post_noop_reuse_shared_strings =
            post_noop_reuse_entries.at("xl/sharedStrings.xml");
        check_contains(post_noop_reuse_shared_strings,
            R"(<si><t>legal-declaration-reuse</t></si></sst>)",
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should append the later shared string");
        check_contains(post_noop_reuse_shared_strings, R"(count="4")",
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should update count metadata");
        check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="4")",
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should update uniqueCount metadata");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should not mutate the source package");
        check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should not mutate the prior no-op output");
        check(fastxlsx::test::read_zip_entries(output) == output_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should not mutate the prior dirty output");
        check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse save should not mutate the prior dirty no-op output");
        const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
            {1, 1, fastxlsx::CellValue::text(std::string(test_case.expected_text))},
            {2, 2, fastxlsx::CellValue::text("legal-declaration-new-inline")},
            {3, 3, fastxlsx::CellValue::text("legal-declaration-reuse")},
        };
        check_reopened_shared_strings_dirty_output(
            post_noop_reuse_output,
            post_noop_reuse_cells,
            fastxlsx::CellRange {1, 1, 3, 3},
            std::string(test_case.name)
                + " legal declaration post-noop reuse output");

        editor.save_as(post_noop_reuse_noop_output);
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration post-noop reuse no-op save should keep Data clean");
        check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
                == post_noop_reuse_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse no-op save should keep output byte-stable");
        check(fastxlsx::test::read_zip_entries(source) == source_entries,
            std::string(test_case.name)
                + " legal declaration post-noop reuse no-op save should not mutate the source package");
        check_reopened_shared_strings_dirty_output(
            post_noop_reuse_noop_output,
            post_noop_reuse_cells,
            fastxlsx::CellRange {1, 1, 3, 3},
            std::string(test_case.name)
                + " legal declaration post-noop reuse no-op output");
    }
}

void test_public_worksheet_editor_flattens_rich_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-rich-sharedstrings-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("rich-placeholder"),
            fastxlsx::CellView::text("plain-placeholder")});
        writer.close();
    }

    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/></si>)"
        R"(<si><t>plain</t></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", rich_shared_strings);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "rich-A&B",
        "WorksheetEditor should flatten simple source sharedStrings rich text runs");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "plain",
        "WorksheetEditor should still materialize plain shared string items beside rich text");
    check(!sheet.has_pending_changes(),
        "rich sharedStrings read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "rich sharedStrings read-only materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "rich sharedStrings read-only materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "rich sharedStrings no-op save should keep Data clean");
    check(!editor.has_pending_changes(),
        "rich sharedStrings no-op save should keep WorkbookEditor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "rich sharedStrings no-op save should copy source entries");
    const ReopenedLazySharedStringsCell noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B")},
        {1, 2, fastxlsx::CellValue::text("plain")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        noop_cells,
        fastxlsx::CellRange {1, 1, 1, 2},
        "rich sharedStrings no-op output");

    sheet.set_cell("C2", fastxlsx::CellValue::text("rich-shared-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "dirty projection should preserve flattened rich sharedStrings via source index");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "dirty projection should preserve plain sharedStrings beside rich text via source index");
    check_contains(worksheet_xml,
        R"(<c r="C2" t="s"><v>2</v></c>)",
        "dirty projection should append edits after flattened rich sharedStrings");
    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<si><t>rich-shared-dirty</t></si></sst>)",
        "dirty projection should append new text while preserving rich sharedStrings markup");
    check_contains(shared_strings_after, R"(count="3")",
        "dirty projection should update rich sharedStrings count metadata");
    check_contains(shared_strings_after, R"(uniqueCount="3")",
        "dirty projection should update rich sharedStrings uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "rich sharedStrings dirty projection should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "rich sharedStrings dirty projection should not mutate the prior no-op output");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B")},
        {1, 2, fastxlsx::CellValue::text("plain")},
        {2, 3, fastxlsx::CellValue::text("rich-shared-dirty")},
    };
    check_reopened_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "rich sharedStrings dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "rich sharedStrings post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "rich sharedStrings post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "rich sharedStrings post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "rich sharedStrings post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 2, 3},
        "rich sharedStrings post-dirty no-op output");

    sheet.set_cell("D3", fastxlsx::CellValue::text("rich-shared-reuse"));
    check(sheet.has_pending_changes(),
        "rich sharedStrings post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "rich sharedStrings post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "rich sharedStrings post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="D3" t="s"><v>3</v></c>)",
        "rich sharedStrings post-noop reuse save should include the later text edit");
    const std::string post_noop_reuse_shared_strings =
        post_noop_reuse_entries.at("xl/sharedStrings.xml");
    check_contains(post_noop_reuse_shared_strings,
        R"(<si><t>rich-shared-reuse</t></si></sst>)",
        "rich sharedStrings post-noop reuse save should append the later shared string");
    check_contains(post_noop_reuse_shared_strings, R"(count="4")",
        "rich sharedStrings post-noop reuse save should update count metadata");
    check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="4")",
        "rich sharedStrings post-noop reuse save should update uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "rich sharedStrings post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "rich sharedStrings post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "rich sharedStrings post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "rich sharedStrings post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("rich-A&B")},
        {1, 2, fastxlsx::CellValue::text("plain")},
        {2, 3, fastxlsx::CellValue::text("rich-shared-dirty")},
        {3, 4, fastxlsx::CellValue::text("rich-shared-reuse")},
    };
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 3, 4},
        "rich sharedStrings post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "rich sharedStrings post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "rich sharedStrings post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "rich sharedStrings post-noop reuse no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 3, 4},
        "rich sharedStrings post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_prefixed_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-prefixed-sharedstrings-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("prefix-placeholder-a"),
            fastxlsx::CellView::text("prefix-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-shared")});
        writer.close();
    }

    const std::string prefixed_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:sst xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="2" uniqueCount="2">)"
        R"(<x:si><x:t>prefixed-A&amp;B</x:t></x:si>)"
        R"(<x:si><x:r><x:rPr><x:b/></x:rPr><x:t>rich-</x:t></x:r><x:r><x:t xml:space="preserve"> tail </x:t></x:r>)"
        R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
        R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
        R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst></x:si>)"
        R"(<x:phoneticPr fontId="2"/><x:extLst/><x:extLst><x:ext uri="{fastxlsx-root}"><fx:opaque><x:t>ignored-root-ext</x:t></fx:opaque></x:ext></x:extLst>)"
        R"(</x:sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", prefixed_shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "<x:sst",
        "prefixed sharedStrings fixture should use a qualified root element");
    check_contains(shared_strings_before, "<x:si>",
        "prefixed sharedStrings fixture should use qualified shared string items");
    check_contains(shared_strings_before, "<x:t>",
        "prefixed sharedStrings fixture should use qualified text elements");
    check_contains(shared_strings_before, "ignored-nested-ext",
        "prefixed sharedStrings fixture should carry nested ignored extension text");
    check_contains(shared_strings_before, "ignored-root-ext",
        "prefixed sharedStrings fixture should carry root-level ignored extension text");
    check_contains(shared_strings_before, "<x:extLst/>",
        "prefixed sharedStrings fixture should carry self-closing ignored metadata");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "prefixed-A&B",
        "WorksheetEditor should materialize prefixed source sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "rich- tail ",
        "WorksheetEditor should flatten prefixed rich sharedStrings by local-name");
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after prefixed sharedStrings materialization should copy source entries");
    const ReopenedLazySharedStringsCell prefixed_noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-A&B")},
        {1, 2, fastxlsx::CellValue::text("rich- tail ")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        prefixed_noop_cells,
        fastxlsx::CellRange {1, 1, 1, 2},
        "prefixed sharedStrings no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("prefixed-shared-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>prefixed-A&amp;B</t></is></c>)",
        "dirty projection should write prefixed source sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve">rich- tail </t></is></c>)",
        "dirty projection should preserve flattened prefixed rich sharedStrings whitespace");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>prefixed-shared-dirty</t></is></c>)",
        "dirty projection should include edits beside prefixed source sharedStrings");
    check_not_contains(worksheet_xml, "ignored-nested-phonetic",
        "dirty projection should not leak nested ignored sharedStrings phonetic text");
    check_not_contains(worksheet_xml, "ignored-nested-ext",
        "dirty projection should not leak nested ignored sharedStrings extension text");
    check_not_contains(worksheet_xml, "ignored-root-ext",
        "dirty projection should not leak root-level ignored sharedStrings extension text");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve prefixed source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty prefixed sharedStrings projection should preserve untouched sheets");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-A&B")},
        {1, 2, fastxlsx::CellValue::text("rich- tail ")},
        {1, 3, fastxlsx::CellValue::text("prefixed-shared-dirty")},
    };
    check_reopened_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "prefixed sharedStrings dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "prefixed sharedStrings post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed sharedStrings post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "prefixed sharedStrings post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "prefixed sharedStrings post-dirty no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("prefixed-shared-reuse"));
    check(sheet.has_pending_changes(),
        "prefixed sharedStrings post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "prefixed sharedStrings post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="D2" t="inlineStr"><is><t>prefixed-shared-reuse</t></is></c>)",
        "prefixed sharedStrings post-noop reuse save should include the later inline text edit");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "prefixed sharedStrings post-noop reuse save should preserve source sharedStrings bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed sharedStrings post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "prefixed sharedStrings post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "prefixed sharedStrings post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "prefixed sharedStrings post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("prefixed-A&B")},
        {1, 2, fastxlsx::CellValue::text("rich- tail ")},
        {1, 3, fastxlsx::CellValue::text("prefixed-shared-dirty")},
        {2, 4, fastxlsx::CellValue::text("prefixed-shared-reuse")},
    };
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "prefixed sharedStrings post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "prefixed sharedStrings post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "prefixed sharedStrings post-noop reuse no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "prefixed sharedStrings post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_local_names_without_namespace_validation()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-noop.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-dirty.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-dirty-noop.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-local-name-no-namespace-validation-post-noop-reuse.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-local-name-no-namespace-validation-post-noop-reuse-noop.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("wrong-ns-placeholder-a"),
            fastxlsx::CellView::text("wrong-ns-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrong-namespace")});
        writer.close();
    }

    const std::string wrong_namespace_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="2" uniqueCount="2">)"
        R"(<bad:si><bad:t>wrong-ns-shared</bad:t></bad:si>)"
        R"(<bad:si><bad:r><bad:t>wrong-rich-</bad:t></bad:r><bad:r><bad:t>tail</bad:t></bad:r></bad:si>)"
        R"(</bad:sst>)";
    rewrite_package_entry_as_stored(
        source, "xl/sharedStrings.xml", wrong_namespace_shared_strings);

    const std::string wrong_namespace_worksheet =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)"
          R"(<bad:sheetData><bad:row r="1">)"
          R"(<bad:c r="A1" t="s"><bad:v>0</bad:v></bad:c>)"
          R"(<bad:c r="B1" t="inlineStr"><bad:is><bad:t>wrong-ns-inline</bad:t></bad:is></bad:c>)"
          R"(<bad:c r="C1" t="s"><bad:v>1</bad:v></bad:c>)"
          R"(</bad:row></bad:sheetData>)"
          R"(</bad:worksheet>)";
    rewrite_package_entry_as_stored(
        source, "xl/worksheets/sheet1.xml", wrong_namespace_worksheet);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/sharedStrings.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml sharedStrings URI");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml worksheet URI");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "wrong-ns-shared",
        "WorksheetEditor should materialize sharedStrings by local-name without namespace URI validation");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "wrong-ns-inline",
        "WorksheetEditor should materialize inline strings by local-name without namespace URI validation");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "wrong-rich-tail",
        "WorksheetEditor should flatten rich sharedStrings by local-name without namespace URI validation");
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name materialization should start clean");
    check(!editor.has_pending_changes(),
        "wrong-namespace local-name materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after wrong-namespace local-name materialization should copy source entries");
    const ReopenedLazySharedStringsCell wrong_namespace_noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("wrong-ns-shared")},
        {1, 2, fastxlsx::CellValue::text("wrong-ns-inline")},
        {1, 3, fastxlsx::CellValue::text("wrong-rich-tail")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        wrong_namespace_noop_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "wrong-namespace local-name no-op output");

    sheet.set_cell("D1", fastxlsx::CellValue::text("wrong-ns-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>wrong-ns-shared</t></is></c>)",
        "dirty projection should write wrong-namespace sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>wrong-ns-inline</t></is></c>)",
        "dirty projection should write wrong-namespace inline text as plain inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>wrong-rich-tail</t></is></c>)",
        "dirty projection should write flattened wrong-namespace sharedStrings rich text");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>wrong-ns-dirty</t></is></c>)",
        "dirty projection should include edits beside wrong-namespace local-name source cells");
    check_contains(worksheet_xml,
        R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)",
        "dirty sheetData flush should preserve wrong source worksheet namespace declarations");
    check_contains(worksheet_xml, R"(<bad:dimension ref="A1:D1"/>)",
        "dirty sheetData flush should refresh dimension using the source worksheet prefix");
    check_not_contains(worksheet_xml, "<bad:c",
        "dirty sheetData flush should not preserve wrong source cell namespace prefixes");
    check_not_contains(worksheet_xml, "<bad:v",
        "dirty sheetData flush should not preserve wrong source value namespace prefixes");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml")
                == source_entries.at("xl/sharedStrings.xml"),
        "dirty wrong-namespace projection should preserve source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty wrong-namespace projection should preserve untouched sheets");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("wrong-ns-shared")},
        {1, 2, fastxlsx::CellValue::text("wrong-ns-inline")},
        {1, 3, fastxlsx::CellValue::text("wrong-rich-tail")},
        {1, 4, fastxlsx::CellValue::text("wrong-ns-dirty")},
    };
    check_reopened_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 4},
        "wrong-namespace local-name dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "wrong-namespace local-name post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrong-namespace local-name post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "wrong-namespace local-name post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 4},
        "wrong-namespace local-name post-dirty no-op output");

    sheet.set_cell("E2", fastxlsx::CellValue::text("wrong-ns-reuse"));
    check(sheet.has_pending_changes(),
        "wrong-namespace local-name post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "wrong-namespace local-name post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet, R"(<bad:dimension ref="A1:E2"/>)",
        "wrong-namespace local-name post-noop reuse save should refresh dimension with the source prefix");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="E2" t="inlineStr"><is><t>wrong-ns-reuse</t></is></c>)",
        "wrong-namespace local-name post-noop reuse save should include the later inline text edit");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml")
                == source_entries.at("xl/sharedStrings.xml"),
        "wrong-namespace local-name post-noop reuse save should preserve source sharedStrings bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrong-namespace local-name post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "wrong-namespace local-name post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "wrong-namespace local-name post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "wrong-namespace local-name post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("wrong-ns-shared")},
        {1, 2, fastxlsx::CellValue::text("wrong-ns-inline")},
        {1, 3, fastxlsx::CellValue::text("wrong-rich-tail")},
        {1, 4, fastxlsx::CellValue::text("wrong-ns-dirty")},
        {2, 5, fastxlsx::CellValue::text("wrong-ns-reuse")},
    };
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 5},
        "wrong-namespace local-name post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "wrong-namespace local-name post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "wrong-namespace local-name post-noop reuse no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 5},
        "wrong-namespace local-name post-noop reuse no-op output");
}

void test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-xml-space-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-xml-space-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("space-placeholder"),
            fastxlsx::CellView::text("rich-space-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t xml:space="preserve">  plain &amp; space  </t></si>)"
        R"(<si><r><t xml:space="preserve">  rich </t></r><r><t>&amp; B</t></r><r><t xml:space="preserve"> tail  </t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "  plain & space  ",
        "WorksheetEditor should preserve xml:space whitespace from plain sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "  rich & B tail  ",
        "WorksheetEditor should preserve xml:space whitespace while flattening rich sharedStrings runs");
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space materialization should start clean");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings xml:space materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings xml:space materialization should copy source entries");
    const ReopenedLazySharedStringsCell xml_space_noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("  plain & space  ")},
        {1, 2, fastxlsx::CellValue::text("  rich & B tail  ")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        xml_space_noop_cells,
        fastxlsx::CellRange {1, 1, 1, 2},
        "source sharedStrings xml:space no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("dirty-space-trigger"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "dirty projection should preserve source sharedStrings whitespace via shared string index");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "dirty projection should preserve flattened rich sharedStrings text via shared string index");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="s"><v>2</v></c>)",
        "dirty projection should append the new trigger edit to sharedStrings");
    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<si><t>dirty-space-trigger</t></si></sst>)",
        "dirty projection should append new text while preserving source sharedStrings markup");
    check_contains(shared_strings_after, R"(count="3")",
        "dirty projection should update sharedStrings xml:space count metadata");
    check_contains(shared_strings_after, R"(uniqueCount="3")",
        "dirty projection should update sharedStrings xml:space uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space projection should not mutate the source package");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("  plain & space  ")},
        {1, 2, fastxlsx::CellValue::text("  rich & B tail  ")},
        {1, 3, fastxlsx::CellValue::text("dirty-space-trigger")},
    };
    check_reopened_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "source sharedStrings xml:space dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings xml:space post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "source sharedStrings xml:space post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "source sharedStrings xml:space post-dirty no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("  xml-space-reuse  "));
    check(sheet.has_pending_changes(),
        "source sharedStrings xml:space post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source sharedStrings xml:space post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="D2" t="s"><v>3</v></c>)",
        "source sharedStrings xml:space post-noop reuse save should include the later text edit");
    const std::string post_noop_reuse_shared_strings =
        post_noop_reuse_entries.at("xl/sharedStrings.xml");
    check_contains(post_noop_reuse_shared_strings,
        R"(<si><t xml:space="preserve">  xml-space-reuse  </t></si></sst>)",
        "source sharedStrings xml:space post-noop reuse save should append whitespace-preserving text");
    check_contains(post_noop_reuse_shared_strings, R"(count="4")",
        "source sharedStrings xml:space post-noop reuse save should update count metadata");
    check_contains(post_noop_reuse_shared_strings, R"(uniqueCount="4")",
        "source sharedStrings xml:space post-noop reuse save should update uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "source sharedStrings xml:space post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "source sharedStrings xml:space post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings xml:space post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("  plain & space  ")},
        {1, 2, fastxlsx::CellValue::text("  rich & B tail  ")},
        {1, 3, fastxlsx::CellValue::text("dirty-space-trigger")},
        {2, 4, fastxlsx::CellValue::text("  xml-space-reuse  ")},
    };
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "source sharedStrings xml:space post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source sharedStrings xml:space post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space post-noop reuse no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "source sharedStrings xml:space post-noop reuse no-op output");
}

void test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-dirty-output.xlsx");
    const std::filesystem::path dirty_noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-dirty-noop-output.xlsx");
    const std::filesystem::path post_noop_reuse_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-post-noop-reuse-output.xlsx");
    const std::filesystem::path post_noop_reuse_noop_output = artifact(
        "fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-post-noop-reuse-noop-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("first-placeholder"),
            fastxlsx::CellView::text("second-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="999" uniqueCount="0" fx:root="ignored">)"
        R"(<si fx:item="first"><t fx:text="first">first-meta</t></si>)"
        R"(<si fx:item="second"><r fx:run="1"><t fx:text="second">second</t></r><r fx:run="2"><t>-meta</t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(count="999")",
        "source sharedStrings metadata fixture should carry inconsistent count");
    check_contains(shared_strings_before, R"(uniqueCount="0")",
        "source sharedStrings metadata fixture should carry inconsistent uniqueCount");
    check_contains(shared_strings_before, R"(fx:root="ignored")",
        "source sharedStrings metadata fixture should carry unknown root attributes");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "first-meta",
        "WorksheetEditor should use actual sharedStrings item text, not root count metadata");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "second-meta",
        "WorksheetEditor should ignore unknown sharedStrings item/run/text attributes");
    check(!sheet.has_pending_changes(),
        "source sharedStrings count/attribute materialization should start clean");
    check(!editor.has_pending_changes(),
        "source sharedStrings count/attribute materialization should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "source sharedStrings count/attribute materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings count/attribute materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings count/attribute materialization should copy source entries");
    const ReopenedLazySharedStringsCell metadata_noop_cells[] = {
        {1, 1, fastxlsx::CellValue::text("first-meta")},
        {1, 2, fastxlsx::CellValue::text("second-meta")},
    };
    check_reopened_shared_strings_output(
        noop_output,
        metadata_noop_cells,
        fastxlsx::CellRange {1, 1, 1, 2},
        "source sharedStrings count metadata no-op output");

    sheet.set_cell("C1", fastxlsx::CellValue::text("after-metadata"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>first-meta</t></is></c>)",
        "dirty projection should write count-mismatched source sharedStrings as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>second-meta</t></is></c>)",
        "dirty projection should write unknown-attribute source sharedStrings as flattened inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>after-metadata</t></is></c>)",
        "dirty projection should include the metadata-boundary trigger edit");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty projection should not write shared string indexes after materialization");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve source sharedStrings bytes with inconsistent metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count/attribute projection should not mutate the source package");
    const ReopenedLazySharedStringsCell expected_cells[] = {
        {1, 1, fastxlsx::CellValue::text("first-meta")},
        {1, 2, fastxlsx::CellValue::text("second-meta")},
        {1, 3, fastxlsx::CellValue::text("after-metadata")},
    };
    check_reopened_shared_strings_dirty_output(
        dirty_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "source sharedStrings count metadata dirty output");

    editor.save_as(dirty_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings count metadata post-dirty no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings count metadata post-dirty no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count metadata post-dirty no-op save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "source sharedStrings count metadata post-dirty no-op save should not mutate the prior no-op output");
    check_reopened_shared_strings_dirty_output(
        dirty_noop_output,
        expected_cells,
        fastxlsx::CellRange {1, 1, 1, 3},
        "source sharedStrings count metadata post-dirty no-op output");

    sheet.set_cell("D2", fastxlsx::CellValue::text("metadata-reuse"));
    check(sheet.has_pending_changes(),
        "source sharedStrings count metadata post-noop reuse edit should dirty Data");
    check(editor.has_pending_changes(),
        "source sharedStrings count metadata post-noop reuse edit should dirty WorkbookEditor");
    editor.save_as(post_noop_reuse_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings count metadata post-noop reuse save should keep Data clean");
    const auto post_noop_reuse_entries =
        fastxlsx::test::read_zip_entries(post_noop_reuse_output);
    const std::string post_noop_reuse_worksheet =
        post_noop_reuse_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_reuse_worksheet, R"(<dimension ref="A1:D2"/>)",
        "source sharedStrings count metadata post-noop reuse save should refresh dimension");
    check_contains(post_noop_reuse_worksheet,
        R"(<c r="D2" t="inlineStr"><is><t>metadata-reuse</t></is></c>)",
        "source sharedStrings count metadata post-noop reuse save should include the later inline text edit");
    check_not_contains(post_noop_reuse_worksheet, R"(t="s")",
        "source sharedStrings count metadata post-noop reuse save should still avoid shared string indexes");
    check(post_noop_reuse_entries.find("xl/sharedStrings.xml") != post_noop_reuse_entries.end()
            && post_noop_reuse_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "source sharedStrings count metadata post-noop reuse save should preserve source sharedStrings bytes");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count metadata post-noop reuse save should not mutate the source package");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "source sharedStrings count metadata post-noop reuse save should not mutate the prior no-op output");
    check(fastxlsx::test::read_zip_entries(dirty_output) == output_entries,
        "source sharedStrings count metadata post-noop reuse save should not mutate the prior dirty output");
    check(fastxlsx::test::read_zip_entries(dirty_noop_output) == output_entries,
        "source sharedStrings count metadata post-noop reuse save should not mutate the prior dirty no-op output");
    const ReopenedLazySharedStringsCell post_noop_reuse_cells[] = {
        {1, 1, fastxlsx::CellValue::text("first-meta")},
        {1, 2, fastxlsx::CellValue::text("second-meta")},
        {1, 3, fastxlsx::CellValue::text("after-metadata")},
        {2, 4, fastxlsx::CellValue::text("metadata-reuse")},
    };
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "source sharedStrings count metadata post-noop reuse output");

    editor.save_as(post_noop_reuse_noop_output);
    check(!sheet.has_pending_changes(),
        "source sharedStrings count metadata post-noop reuse no-op save should keep Data clean");
    check(fastxlsx::test::read_zip_entries(post_noop_reuse_noop_output)
            == post_noop_reuse_entries,
        "source sharedStrings count metadata post-noop reuse no-op save should keep output byte-stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count metadata post-noop reuse no-op save should not mutate the source package");
    check_reopened_shared_strings_dirty_output(
        post_noop_reuse_noop_output,
        post_noop_reuse_cells,
        fastxlsx::CellRange {1, 1, 2, 4},
        "source sharedStrings count metadata post-noop reuse no-op output");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_defers_source_shared_strings_until_index_cells();
        test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells();
        test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells();
        test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells();
        test_public_worksheet_editor_materializes_source_shared_strings();
        test_public_worksheet_editor_reuses_duplicate_dirty_shared_strings();
        test_public_worksheet_editor_reuses_existing_dirty_shared_strings_without_rewriting_table();
        test_public_worksheet_editor_shifts_source_shared_strings_records();
        test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations();
        test_public_worksheet_editor_flattens_rich_source_shared_strings();
        test_public_worksheet_editor_materializes_prefixed_source_shared_strings();
        test_public_worksheet_editor_materializes_local_names_without_namespace_validation();
        test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline();
        test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success sharedStrings check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success sharedStrings tests passed\n");
    return 0;
}
