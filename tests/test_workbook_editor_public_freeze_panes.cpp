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

void fail_after_freeze_pane_staging()
{
    throw fastxlsx::FastXlsxError("injected freeze-pane staging failure");
}

void check_freeze_pane_summary(const fastxlsx::WorkbookEditor& editor,
    std::string_view planned_name, std::uint32_t expected_rows,
    std::uint32_t expected_columns, std::string_view scenario)
{
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1, std::string(scenario) + " should expose one summary");
    if (summaries.size() != 1) {
        return;
    }
    const auto& summary = summaries.front();
    check(summary.planned_name == planned_name,
        std::string(scenario) + " should expose the planned worksheet name");
    check(summary.freeze_panes_changed,
        std::string(scenario) + " should expose a freeze-pane edit");
    check(summary.frozen_row_count == expected_rows
            && summary.frozen_column_count == expected_columns,
        std::string(scenario) + " should expose the final frozen split");
}

std::filesystem::path write_source_with_prefix_metadata(
    std::string_view name, std::string_view metadata)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        "<sheetData>", std::string(metadata) + "<sheetData>");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_prefixed_source(
    std::string_view name, std::string_view sheet_view_children)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    entries.at("xl/worksheets/sheet1.xml") =
        std::string(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><x:sheetViews><x:sheetView showGridLines="0" workbookViewId="0">)")
        + std::string(sheet_view_children)
        + R"(</x:sheetView></x:sheetViews><x:sheetData><x:row r="1"><x:c r="A1" t="inlineStr"><x:is><x:t>keep</x:t></x:is></x:c></x:row></x:sheetData></x:worksheet>)";
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::string sheet_data_record(std::string_view worksheet_xml)
{
    const std::size_t start = worksheet_xml.find("<sheetData");
    const std::size_t end = worksheet_xml.find("</sheetData>", start);
    if (start == std::string_view::npos || end == std::string_view::npos) {
        throw std::runtime_error("worksheet fixture is missing sheetData");
    }
    return std::string(worksheet_xml.substr(start, end - start + 12U));
}

std::filesystem::path write_table_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    const fastxlsx::StyleId formula_style =
        writer.add_style(fastxlsx::CellStyle {"0.00"});
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
        fastxlsx::CellView::formula("B2").with_style(formula_style)});
    data.append_row({fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0), fastxlsx::CellView::text("keep")});
    fastxlsx::TableOptions table;
    table.name = "DataTable";
    table.column_names = {"Name", "Qty"};
    data.add_table({1, 1, 2, 2}, std::move(table));
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

void test_missing_insert_and_single_axis_serialization()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-freeze-pane-missing-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/opaque.bin", "freeze-pane unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_freeze_panes("Data", 2, 3);
    check(editor.pending_change_count() == 1 && editor.has_unsaved_changes(),
        "set_freeze_panes should publish one unsaved edit");
    check_freeze_pane_summary(editor, "Data", 2, 3, "inserted freeze pane");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-missing-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<sheetViews><sheetView workbookViewId="0"><pane xSplit="3" ySplit="2" topLeftCell="D3" activePane="bottomRight" state="frozen"/></sheetView></sheetViews>)",
        "missing sheetViews should be inserted with a dual-axis frozen pane");
    check(entries.at("custom/opaque.bin") == "freeze-pane unknown entry",
        "freeze-pane edit should preserve unknown package entries");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "freeze-pane edit should preserve content types");

    fastxlsx::WorkbookEditor row_only = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-row-only-source.xlsx",
            "<sheetViews/>"));
    row_only.set_freeze_panes("Data", 4, 0);
    const std::filesystem::path row_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-row-only-output.xlsx");
    row_only.save_as(row_output);
    const std::string row_xml = fastxlsx::test::read_zip_entries(row_output)
        .at("xl/worksheets/sheet1.xml");
    check_contains(row_xml,
        R"(<sheetViews><sheetView workbookViewId="0"><pane ySplit="4" topLeftCell="A5" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>)",
        "self-closing sheetViews should expand for a row-only frozen pane");
    check_not_contains(row_xml, "xSplit=",
        "row-only frozen pane should not serialize xSplit");

    fastxlsx::WorkbookEditor column_only = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-column-only-source.xlsx",
            R"(<sheetViews><sheetView workbookViewId="1" showGridLines="0"/></sheetViews>)"));
    column_only.set_freeze_panes("Data", 0, 5);
    const std::filesystem::path column_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-column-only-output.xlsx");
    column_only.save_as(column_output);
    const std::string column_xml = fastxlsx::test::read_zip_entries(column_output)
        .at("xl/worksheets/sheet1.xml");
    check_contains(column_xml,
        R"(<sheetView workbookViewId="1" showGridLines="0"/><sheetView workbookViewId="0"><pane xSplit="5" topLeftCell="F1" activePane="topRight" state="frozen"/></sheetView>)",
        "missing primary sheetView should append after other workbook views");
    check_not_contains(column_xml, "ySplit=",
        "column-only frozen pane should not serialize ySplit");
}

void test_expand_replace_clear_and_selection_preservation()
{
    fastxlsx::WorkbookEditor expand = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-expand-view-source.xlsx",
            R"(<sheetViews><sheetView showGridLines="0" workbookViewId="0"/></sheetViews>)"));
    expand.set_freeze_panes("Data", 1, 1);
    const std::filesystem::path expanded_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-expand-view-output.xlsx");
    expand.save_as(expanded_output);
    check_contains(fastxlsx::test::read_zip_entries(expanded_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<sheetView showGridLines="0" workbookViewId="0"><pane xSplit="1" ySplit="1" topLeftCell="B2" activePane="bottomRight" state="frozen"/></sheetView>)",
        "self-closing primary sheetView should expand without changing attributes");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-replace-source.xlsx",
            R"(<sheetViews><sheetView showGridLines="0" workbookViewId="0"><pane xSplit="1" ySplit="1" topLeftCell="B2" activePane="bottomRight" state="frozen">
</pane><selection pane="topRight" activeCell="C1" sqref="C1"/><selection activeCell="A1" sqref="A1"/></sheetView><sheetView workbookViewId="2"><pane state="split" xSplit="10"/></sheetView></sheetViews>)"));
    editor.set_freeze_panes("Data", 0, 2);
    const std::filesystem::path replaced_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-replace-output.xlsx");
    editor.save_as(replaced_output);
    const std::string replaced_xml = fastxlsx::test::read_zip_entries(replaced_output)
        .at("xl/worksheets/sheet1.xml");
    check_contains(replaced_xml,
        R"(<pane xSplit="2" topLeftCell="C1" activePane="topRight" state="frozen"/><selection pane="topRight" activeCell="C1" sqref="C1"/><selection activeCell="A1" sqref="A1"/>)",
        "set should replace an explicit pane and preserve valid selections");
    check_contains(replaced_xml,
        R"(<sheetView workbookViewId="2"><pane state="split" xSplit="10"/></sheetView>)",
        "set should preserve non-primary workbook views byte-for-byte");

    fastxlsx::WorkbookEditor clear = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-clear-source.xlsx",
            R"(<sheetViews><sheetView showGridLines="0" workbookViewId="0"><pane ySplit="2" topLeftCell="A3" activePane="bottomLeft" state="frozen"/><selection activeCell="A3" sqref="A3"/></sheetView></sheetViews>)"));
    clear.clear_freeze_panes("Data");
    const std::size_t count_after_clear = clear.pending_change_count();
    check_freeze_pane_summary(clear, "Data", 0, 0, "cleared freeze pane");
    clear.set_freeze_panes("Data", 0, 0);
    clear.clear_freeze_panes("Data");
    check(clear.pending_change_count() == count_after_clear,
        "repeated clear and zero/zero set should be clean no-ops");
    const std::filesystem::path cleared_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-clear-output.xlsx");
    clear.save_as(cleared_output);
    const std::string cleared_xml = fastxlsx::test::read_zip_entries(cleared_output)
        .at("xl/worksheets/sheet1.xml");
    check_not_contains(cleared_xml, "<pane",
        "clear_freeze_panes should remove the direct primary pane");
    check_contains(cleared_xml,
        R"(<sheetView showGridLines="0" workbookViewId="0"><selection activeCell="A3" sqref="A3"/></sheetView>)",
        "clear should preserve the sheetView and unbound selection");

    fastxlsx::WorkbookEditor absent = fastxlsx::WorkbookEditor::open(
        write_two_sheet_source(
            "fastxlsx-workbook-editor-freeze-pane-clear-absent-source.xlsx"));
    absent.clear_freeze_panes("Data");
    check(!absent.has_pending_changes() && !absent.has_unsaved_changes()
            && absent.pending_change_count() == 0
            && absent.pending_worksheet_edits().empty(),
        "clearing an absent pane should not publish public state");
}

void test_prefixed_qnames_and_strict_view_guards()
{
    fastxlsx::WorkbookEditor prefixed = fastxlsx::WorkbookEditor::open(
        write_prefixed_source(
            "fastxlsx-workbook-editor-freeze-pane-prefixed-source.xlsx",
            R"(<x:selection activeCell="A1" sqref="A1"/><x:extLst><x:ext uri="keep"/></x:extLst>)"));
    prefixed.set_freeze_panes("Data", 1, 2);
    const std::filesystem::path prefixed_output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-prefixed-output.xlsx");
    prefixed.save_as(prefixed_output);
    const std::string prefixed_xml = fastxlsx::test::read_zip_entries(prefixed_output)
        .at("xl/worksheets/sheet1.xml");
    check_contains(prefixed_xml,
        R"(<x:pane xSplit="2" ySplit="1" topLeftCell="C2" activePane="bottomRight" state="frozen"/><x:selection activeCell="A1" sqref="A1"/><x:extLst>)",
        "prefixed primary sheetView should receive a matching pane QName");
    check_not_contains(prefixed_xml, "<pane",
        "prefixed freeze-pane rewrite should not introduce an unqualified pane");

    const std::vector<std::pair<std::string, std::string>> set_failures {
        {"split-state", R"(<pane xSplit="1" state="split"/>)"},
        {"frozen-split-state", R"(<pane xSplit="1" state="frozenSplit"/>)"},
        {"duplicate-pane", R"(<pane xSplit="1" state="frozen"/><pane ySplit="1" state="frozen"/>)"},
        {"invalid-split", R"(<pane xSplit="16384" state="frozen"/>)"},
        {"pane-child", R"(<pane xSplit="1" state="frozen"><future/></pane>)"},
        {"selection-before-pane", R"(<selection/><pane xSplit="1" state="frozen"/>)"},
        {"removed-selection-pane", R"(<pane xSplit="1" ySplit="1" state="frozen"/><selection pane="bottomLeft"/>)"},
        {"pivot-selection", R"(<pivotSelection pane="bottomRight"/>)"},
    };
    for (const auto& [name, children] : set_failures) {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
            write_source_with_prefix_metadata(
                "fastxlsx-workbook-editor-freeze-pane-guard-" + name + ".xlsx",
                "<sheetViews><sheetView workbookViewId=\"0\">" + children
                    + "</sheetView></sheetViews>"));
        check(threw_fastxlsx_error([&] {
            editor.set_freeze_panes("Data", 0, 2);
        }), "strict primary sheetView guard should reject: " + name);
        check(!editor.has_pending_changes() && editor.pending_change_count() == 0
                && editor.pending_worksheet_edits().empty(),
            "strict primary sheetView rejection should preserve public state: " + name);
    }

    fastxlsx::WorkbookEditor bound_clear = fastxlsx::WorkbookEditor::open(
        write_source_with_prefix_metadata(
            "fastxlsx-workbook-editor-freeze-pane-bound-clear-source.xlsx",
            R"(<sheetViews><sheetView workbookViewId="0"><pane xSplit="1" state="frozen"/><selection pane="topRight"/></sheetView></sheetViews>)"));
    check(threw_fastxlsx_error([&] {
        bound_clear.clear_freeze_panes("Data");
    }), "clear should reject a selection bound to the removed pane");
    check(!bound_clear.has_pending_changes()
            && bound_clear.pending_change_count() == 0,
        "bound-selection clear rejection should preserve public state");

    fastxlsx::WorkbookEditor mixed_prefix = fastxlsx::WorkbookEditor::open(
        write_prefixed_source(
            "fastxlsx-workbook-editor-freeze-pane-mixed-prefix-source.xlsx",
            R"(<selection activeCell="A1" sqref="A1"/>)"));
    check(threw_fastxlsx_error([&] {
        mixed_prefix.set_freeze_panes("Data", 1, 1);
    }), "mixed selection QName prefix should be rejected");
}

void test_package_preservation()
{
    const std::filesystem::path source = write_table_source(
        "fastxlsx-workbook-editor-freeze-pane-preservation-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(source_entries.at("[Content_Types].xml"),
        "</Types>",
        R"(<Override PartName="/xl/calcChain.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"/></Types>)");
    replace_first_or_throw(source_entries.at("xl/_rels/workbook.xml.rels"),
        "</Relationships>",
        R"(<Relationship Id="rIdCalcChain" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/></Relationships>)");
    replace_first_or_throw(source_entries.at("xl/workbook.xml"),
        "</workbook>", R"(<calcPr calcId="1" fullCalcOnLoad="0"/></workbook>)");
    source_entries.emplace("xl/calcChain.xml",
        R"(<calcChain><c r="C1" i="1"/></calcChain>)");
    source_entries.emplace("custom/opaque.bin", "freeze-pane preservation entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);
    const std::string source_sheet_data = sheet_data_record(
        source_entries.at("xl/worksheets/sheet1.xml"));

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.set_freeze_panes("Data", 1, 1);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-preservation-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);

    check(sheet_data_record(entries.at("xl/worksheets/sheet1.xml"))
            == source_sheet_data,
        "freeze-pane edit should preserve all cell records exactly");
    check(entries.at("xl/tables/table1.xml") == source_entries.at("xl/tables/table1.xml"),
        "freeze-pane edit should preserve table parts");
    check(entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "freeze-pane edit should preserve worksheet relationships");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "freeze-pane edit should preserve content types");
    check(entries.at("xl/workbook.xml") == source_entries.at("xl/workbook.xml"),
        "freeze-pane edit should preserve workbook calculation metadata");
    check(entries.at("xl/_rels/workbook.xml.rels")
            == source_entries.at("xl/_rels/workbook.xml.rels"),
        "freeze-pane edit should preserve the calcChain relationship");
    check(entries.at("xl/calcChain.xml") == source_entries.at("xl/calcChain.xml"),
        "freeze-pane edit should preserve calcChain bytes");
    check(entries.at("custom/opaque.bin") == "freeze-pane preservation entry",
        "freeze-pane edit should preserve unrelated unknown parts");
}

void test_invalid_input_failure_hook_rename_added_save_retry_and_reopen()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-freeze-pane-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.set_freeze_panes("Data", 1048576U, 0);
    }), "row split at the worksheet row count should be rejected");
    check(threw_fastxlsx_error([&] {
        editor.set_freeze_panes("Data", 0, 16384U);
    }), "column split at the worksheet column count should be rejected");
    check(threw_fastxlsx_error([&] {
        editor.set_freeze_panes("Missing", 1, 1);
    }), "missing planned worksheet should be rejected");
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0,
        "invalid freeze-pane inputs should not publish pending state");

    editor.set_freeze_panes("Data", 1, 1);
    const auto before_failure = editor.pending_worksheet_edits();
    const std::size_t count_before_failure = editor.pending_change_count();
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_freeze_pane_staging);
        check(threw_fastxlsx_error([&] {
            editor.set_freeze_panes("Data", 2, 3);
        }), "injected freeze-pane staging failure should escape");
    }
    const auto after_failure = editor.pending_worksheet_edits();
    check(editor.pending_change_count() == count_before_failure
            && after_failure.size() == before_failure.size()
            && after_failure.front().frozen_row_count
                == before_failure.front().frozen_row_count
            && after_failure.front().frozen_column_count
                == before_failure.front().frozen_column_count,
        "freeze-pane staging failure should preserve public state");

    editor.set_freeze_panes("Data", 2, 3);
    check(!editor.last_edit_error().has_value(),
        "successful freeze-pane retry should clear the previous error");
    editor.rename_sheet("Data", "Renamed Data");
    check_freeze_pane_summary(editor, "Renamed Data", 2, 3,
        "renamed freeze-pane worksheet");

    editor.add_worksheet("Added");
    editor.set_freeze_panes("Added", 4, 0);
    editor.rename_sheet("Added", "Renamed Added");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 2 && summaries.back().added
            && summaries.back().planned_name == "Renamed Added"
            && summaries.back().freeze_panes_changed
            && summaries.back().frozen_row_count == 4
            && summaries.back().frozen_column_count == 0,
        "added worksheet rename should migrate freeze-pane diagnostics");
    check(threw_fastxlsx_error([&] {
        editor.remove_worksheet("Renamed Added");
    }), "worksheet removal should reject queued freeze-pane edits");

    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-freeze-pane-missing")
            / "parent" / "output.xlsx");
    }), "freeze-pane save should fail when the output parent is missing");
    check(editor.has_unsaved_changes(),
        "failed freeze-pane save should retain unsaved retry state");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-freeze-pane-retry-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<pane xSplit="3" ySplit="2" topLeftCell="D3" activePane="bottomRight" state="frozen"/>)",
        "save retry should retain the final source worksheet frozen pane");
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<pane ySplit="4" topLeftCell="A5" activePane="bottomLeft" state="frozen"/>)",
        "same-session added worksheet should accept a frozen pane");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.clear_freeze_panes("Renamed Data");
    const std::filesystem::path cleared = artifact(
        "fastxlsx-workbook-editor-freeze-pane-reopen-cleared.xlsx");
    reopened.save_as(cleared);
    const auto cleared_entries = fastxlsx::test::read_zip_entries(cleared);
    check_not_contains(cleared_entries.at("xl/worksheets/sheet1.xml"),
        "<pane", "reopened editor should clear the selected frozen pane");
    check_contains(cleared_entries.at("xl/worksheets/sheet3.xml"),
        R"(<pane ySplit="4" topLeftCell="A5" activePane="bottomLeft" state="frozen"/>)",
        "reopened clear should preserve other worksheet frozen panes");
}

} // namespace

int main()
{
    try {
        test_missing_insert_and_single_axis_serialization();
        test_expand_replace_clear_and_selection_preservation();
        test_prefixed_qnames_and_strict_view_guards();
        test_package_preservation();
        test_invalid_input_failure_hook_rename_added_save_retry_and_reopen();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor freeze-pane checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor freeze-pane tests passed");
    return 0;
}
