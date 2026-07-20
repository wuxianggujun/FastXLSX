#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

class ScopedWorksheetReplacementStagedHook {
public:
    explicit ScopedWorksheetReplacementStagedHook(
        fastxlsx::detail::PackageEditorWorksheetPartReplacementStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            hook);
    }

    ~ScopedWorksheetReplacementStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            nullptr);
    }

    ScopedWorksheetReplacementStagedHook(const ScopedWorksheetReplacementStagedHook&) = delete;
    ScopedWorksheetReplacementStagedHook& operator=(
        const ScopedWorksheetReplacementStagedHook&) = delete;
};

void fail_after_auto_filter_staging()
{
    throw fastxlsx::FastXlsxError("injected auto-filter staging failure");
}

bool ranges_equal(const fastxlsx::CellRange& left, const fastxlsx::CellRange& right)
{
    return left.first_row == right.first_row
        && left.first_column == right.first_column
        && left.last_row == right.last_row
        && left.last_column == right.last_column;
}

void check_set_summary(const fastxlsx::WorkbookEditor& editor,
    std::string_view planned_name, const fastxlsx::CellRange& expected_range,
    std::string_view scenario)
{
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1, std::string(scenario) + " should expose one summary");
    if (summaries.size() != 1) {
        return;
    }
    check(summaries.front().planned_name == planned_name,
        std::string(scenario) + " should expose the planned worksheet name");
    check(summaries.front().auto_filter_changed,
        std::string(scenario) + " should expose an auto-filter edit");
    check(summaries.front().auto_filter_range.has_value()
            && ranges_equal(*summaries.front().auto_filter_range, expected_range),
        std::string(scenario) + " should expose the final auto-filter range");
}

std::filesystem::path write_table_and_root_filter_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Qty")});
    data.append_row({fastxlsx::CellView::text("Widget"), fastxlsx::CellView::number(7.0)});
    data.set_auto_filter({1, 1, 2, 2});
    fastxlsx::TableOptions table;
    table.name = "DataTable";
    table.column_names = {"Name", "Qty"};
    data.add_table({1, 1, 2, 2}, std::move(table));
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

std::filesystem::path write_source_with_suffix(
    std::string_view name, std::string_view suffix)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    const std::string replacement = "</sheetData>" + std::string(suffix);
    replace_first_or_throw(
        worksheet_xml, "</sheetData>", replacement);
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

void test_insert_schema_order_and_unknown_part_preservation()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-auto-filter-insert-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/opaque.bin", "auto-filter unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    const fastxlsx::CellRange range {1, 1, 2, 2};
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_auto_filter("Data", range);
    check(editor.pending_change_count() == 1 && editor.has_unsaved_changes(),
        "set_auto_filter should publish one unsaved edit");
    check_set_summary(editor, "Data", range, "inserted auto-filter");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-auto-filter-insert-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(</sheetData><autoFilter ref="A1:B2"/>)",
        "missing root autoFilter should be inserted after sheetData");
    check(entries.at("custom/opaque.bin") == "auto-filter unknown entry",
        "auto-filter edit should preserve unknown package entries");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "root auto-filter insertion should not create worksheet relationships");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "root auto-filter insertion should not change content types");
}

void test_replace_nested_filter_and_clear_current_state()
{
    const std::filesystem::path source = write_source_with_suffix(
        "fastxlsx-workbook-editor-auto-filter-nested-source.xlsx",
        R"(<autoFilter ref="A1:C9"><filterColumn colId="0"><customFilters><customFilter operator="greaterThan" val="5"/></customFilters></filterColumn><sortState ref="A1:C9"><sortCondition ref="A2:A9"/></sortState></autoFilter>)");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_auto_filter("Data", {1, 2, 5, 3});

    const std::filesystem::path replaced = artifact(
        "fastxlsx-workbook-editor-auto-filter-nested-replaced.xlsx");
    editor.save_as(replaced);
    const auto replaced_entries = fastxlsx::test::read_zip_entries(replaced);
    const std::string& replaced_xml = replaced_entries.at("xl/worksheets/sheet1.xml");
    check_contains(replaced_xml, R"(<autoFilter ref="B1:C5"/>)",
        "set_auto_filter should replace the complete nested root element");
    check_not_contains(replaced_xml, "filterColumn",
        "root auto-filter replacement should remove old filter criteria");
    check_not_contains(replaced_xml, "sortCondition",
        "root auto-filter replacement should remove old sort metadata");

    editor.clear_auto_filter("Data");
    const std::size_t count_after_clear = editor.pending_change_count();
    const auto clear_summaries = editor.pending_worksheet_edits();
    check(clear_summaries.size() == 1 && clear_summaries.front().auto_filter_changed
            && !clear_summaries.front().auto_filter_range.has_value(),
        "clear_auto_filter should expose a final clear diagnostic");
    editor.clear_auto_filter("Data");
    check(editor.pending_change_count() == count_after_clear,
        "clearing an already-cleared current autoFilter should be a clean no-op");

    const std::filesystem::path cleared = artifact(
        "fastxlsx-workbook-editor-auto-filter-nested-cleared.xlsx");
    editor.save_as(cleared);
    check_not_contains(fastxlsx::test::read_zip_entries(cleared)
            .at("xl/worksheets/sheet1.xml"),
        "<autoFilter",
        "clear_auto_filter should remove the complete worksheet-root element");

    fastxlsx::WorkbookEditor absent = fastxlsx::WorkbookEditor::open(
        write_two_sheet_source(
            "fastxlsx-workbook-editor-auto-filter-clear-absent-source.xlsx"));
    absent.clear_auto_filter("Data");
    check(!absent.has_pending_changes() && !absent.has_unsaved_changes()
            && absent.pending_change_count() == 0
            && absent.pending_worksheet_edits().empty(),
        "clear_auto_filter should not publish state when no root filter exists");
}

void test_table_local_filter_relationship_and_content_type_preservation()
{
    const std::filesystem::path source = write_table_and_root_filter_source(
        "fastxlsx-workbook-editor-auto-filter-table-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_auto_filter("Data", {1, 1, 2, 1});
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-auto-filter-table-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);

    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<autoFilter ref="A1:A2"/>)",
        "worksheet-root autoFilter should use the replacement range");
    check(entries.at("xl/tables/table1.xml") == source_entries.at("xl/tables/table1.xml")
            && entries.at("xl/tables/table1.xml").find(
                   R"(<autoFilter ref="A1:B2"/>)") != std::string::npos,
        "table-local autoFilter and table part should remain byte-identical logically");
    check(entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "root auto-filter replacement should preserve worksheet relationships");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "root auto-filter replacement should preserve content types");
}

void test_malformed_existing_metadata_rejected_without_state_pollution()
{
    const std::vector<std::pair<std::string, std::string>> cases {
        {"missing-ref", R"(<autoFilter/>)"},
        {"invalid-ref", R"(<autoFilter ref="A0:B2"/>)"},
        {"duplicate", R"(<autoFilter ref="A1:B2"/><autoFilter ref="C1:D2"/>)"},
        {"wrong-order", R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells><autoFilter ref="A1:B2"/>)"},
        {"unknown-order", R"(<futureMetadata/><autoFilter ref="A1:B2"/>)"},
        {"nested", R"(<sortState ref="A1:B2"><autoFilter ref="A1:B2"/></sortState>)"},
    };

    for (const auto& [name, suffix] : cases) {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
            write_source_with_suffix(
                "fastxlsx-workbook-editor-auto-filter-" + name + ".xlsx", suffix));
        check(threw_fastxlsx_error([&] {
            editor.set_auto_filter("Data", {1, 1, 2, 2});
        }), "malformed existing auto-filter case should reject set: " + name);
        check(!editor.has_pending_changes() && editor.pending_change_count() == 0
                && editor.pending_worksheet_edits().empty(),
            "malformed existing auto-filter set should not publish state: " + name);
    }

    const std::filesystem::path before_sheet_data = write_two_sheet_source(
        "fastxlsx-workbook-editor-auto-filter-before-sheet-data.xlsx");
    auto entries = fastxlsx::test::read_zip_entries(before_sheet_data);
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        "<sheetData>", R"(<autoFilter ref="A1:B2"/><sheetData>)");
    fastxlsx::test::write_stored_zip_entries(before_sheet_data, entries);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(before_sheet_data);
    check(threw_fastxlsx_error([&] { editor.clear_auto_filter("Data"); }),
        "autoFilter before sheetData should be rejected");
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0,
        "pre-sheetData rejection should not publish state");
}

void test_invalid_input_failure_hook_retry_and_save_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-auto-filter-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.set_auto_filter("Data", {0, 1, 2, 2});
    }), "invalid new auto-filter range should be rejected");
    check(threw_fastxlsx_error([&] {
        editor.set_auto_filter("Missing", {1, 1, 2, 2});
    }), "missing planned worksheet should be rejected");
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0,
        "invalid auto-filter inputs should not publish pending state");

    editor.set_auto_filter("Data", {1, 1, 2, 2});
    const auto before_failure = editor.pending_worksheet_edits();
    const std::size_t count_before_failure = editor.pending_change_count();
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_auto_filter_staging);
        check(threw_fastxlsx_error([&] {
            editor.set_auto_filter("Data", {1, 1, 5, 3});
        }), "injected auto-filter staging failure should escape");
    }
    check(editor.pending_change_count() == count_before_failure
            && workbook_editor_edit_summaries_equal(
                editor.pending_worksheet_edits(), before_failure),
        "auto-filter staging failure should preserve public state");

    editor.set_auto_filter("Data", {1, 1, 5, 3});
    check(!editor.last_edit_error().has_value(),
        "successful auto-filter retry should clear the previous error");
    check_set_summary(editor, "Data", {1, 1, 5, 3}, "auto-filter retry");

    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-auto-filter-missing")
            / "parent" / "output.xlsx");
    }), "auto-filter save should fail when the output parent is missing");
    check(editor.has_unsaved_changes(),
        "failed auto-filter save should retain unsaved retry state");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-auto-filter-retry-output.xlsx");
    editor.save_as(output);
    check_contains(fastxlsx::test::read_zip_entries(output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<autoFilter ref="A1:C5"/>)",
        "save retry should retain the final staged auto-filter range");
}

void test_rename_added_worksheet_and_reopen_clear()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-auto-filter-rename-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_auto_filter("Data", {1, 1, 2, 2});
    editor.rename_sheet("Data", "Renamed Data");
    check_set_summary(editor, "Renamed Data", {1, 1, 2, 2},
        "renamed auto-filter worksheet");

    editor.add_worksheet("Added");
    editor.set_auto_filter("Added", {1, 1, 8, 2});
    editor.rename_sheet("Added", "Renamed Added");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 2 && summaries.back().added
            && summaries.back().planned_name == "Renamed Added"
            && summaries.back().auto_filter_range.has_value()
            && ranges_equal(*summaries.back().auto_filter_range, {1, 1, 8, 2}),
        "added worksheet rename should migrate auto-filter diagnostics");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-auto-filter-rename-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<autoFilter ref="A1:B2"/>)",
        "renamed source worksheet should retain its staged root filter");
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<autoFilter ref="A1:B8"/>)",
        "same-session added worksheet should accept a root filter");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.clear_auto_filter("Renamed Data");
    const std::filesystem::path cleared = artifact(
        "fastxlsx-workbook-editor-auto-filter-reopen-cleared.xlsx");
    reopened.save_as(cleared);
    const auto cleared_entries = fastxlsx::test::read_zip_entries(cleared);
    check_not_contains(cleared_entries.at("xl/worksheets/sheet1.xml"),
        "<autoFilter", "reopened editor should clear the selected root filter");
    check_contains(cleared_entries.at("xl/worksheets/sheet3.xml"),
        R"(<autoFilter ref="A1:B8"/>)",
        "reopened clear should preserve other worksheet root filters");
}

} // namespace

int main()
{
    try {
        test_insert_schema_order_and_unknown_part_preservation();
        test_replace_nested_filter_and_clear_current_state();
        test_table_local_filter_relationship_and_content_type_preservation();
        test_malformed_existing_metadata_rejected_without_state_pollution();
        test_invalid_input_failure_hook_retry_and_save_retry();
        test_rename_added_worksheet_and_reopen_clear();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor auto-filter checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor auto-filter tests passed");
    return 0;
}
