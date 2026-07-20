#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

#include <stdexcept>

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

void fail_after_merged_cell_staging()
{
    throw fastxlsx::FastXlsxError("injected merged-cell staging failure");
}

void check_merged_cell_summary(const fastxlsx::WorkbookEditor& editor,
    std::string_view planned_name, std::size_t additions, std::size_t removals,
    std::string_view scenario)
{
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1, std::string(scenario) + " should expose one summary");
    if (summaries.size() != 1) {
        return;
    }
    check(summaries.front().planned_name == planned_name,
        std::string(scenario) + " should expose the planned worksheet name");
    check(summaries.front().merged_cell_addition_count == additions,
        std::string(scenario) + " should expose merged-cell additions");
    check(summaries.front().merged_cell_removal_count == removals,
        std::string(scenario) + " should expose merged-cell removals");
}

std::filesystem::path write_merged_cell_suffix_source(
    std::string_view name, std::string_view suffix)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>", "</sheetData>" + std::string(suffix));
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_prefixed_merged_cell_source(
    std::string_view name, std::string_view suffix)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    entries.at("xl/worksheets/sheet1.xml") =
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><x:sheetData/>)"
        + std::string(suffix) + "</x:worksheet>";
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::string worksheet_cell_record(
    std::string_view worksheet_xml, std::string_view reference)
{
    const std::string marker = "<c r=\"" + std::string(reference) + "\"";
    const std::size_t start = worksheet_xml.find(marker);
    if (start == std::string_view::npos) {
        throw std::runtime_error(
            "worksheet fixture is missing cell " + std::string(reference));
    }
    const std::size_t opening_end = worksheet_xml.find('>', start);
    if (opening_end == std::string_view::npos) {
        throw std::runtime_error(
            "worksheet fixture has an incomplete cell " + std::string(reference));
    }
    if (opening_end > start && worksheet_xml[opening_end - 1] == '/') {
        return std::string(worksheet_xml.substr(start, opening_end - start + 1));
    }
    const std::size_t closing = worksheet_xml.find("</c>", opening_end + 1);
    if (closing == std::string_view::npos) {
        throw std::runtime_error(
            "worksheet fixture has an unclosed cell " + std::string(reference));
    }
    return std::string(worksheet_xml.substr(start, closing - start + 4));
}

std::filesystem::path write_table_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    const fastxlsx::StyleId preserved_formula_style =
        writer.add_style(fastxlsx::CellStyle {"0.00"});
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
        fastxlsx::CellView::text("keep-c"),
        fastxlsx::CellView::formula("B2").with_style(preserved_formula_style)});
    data.append_row({fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::text("row-two-c"),
        fastxlsx::CellView::text("row-two-d")});
    fastxlsx::TableOptions table;
    table.name = "DataTable";
    table.column_names = {"Name", "Qty"};
    data.add_table({1, 1, 2, 2}, std::move(table));
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

void test_insert_schema_order_and_unknown_part_preservation()
{
    const std::filesystem::path source = write_merged_cell_suffix_source(
        "fastxlsx-workbook-editor-merged-cell-insert-source.xlsx",
        R"(<autoFilter ref="A1:B2"/><conditionalFormatting sqref="A1:A2"><cfRule type="expression" priority="1"><formula>A1&gt;0</formula></cfRule></conditionalFormatting>)");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/opaque.bin", "merged-cell unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.merge_cells("Data", {1, 3, 1, 4});
    check(editor.pending_change_count() == 1 && editor.has_unsaved_changes(),
        "merge_cells should publish one unsaved edit");
    check_merged_cell_summary(editor, "Data", 1, 0, "inserted merged range");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-merged-cell-insert-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<autoFilter ref="A1:B2"/><mergeCells count="1"><mergeCell ref="C1:D1"/></mergeCells><conditionalFormatting)",
        "missing mergeCells should be inserted at schema rank nine");
    check(entries.at("custom/opaque.bin") == "merged-cell unknown entry",
        "merged-cell insertion should preserve unknown package entries");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "merged-cell insertion should preserve content types");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "merged-cell insertion should not create worksheet relationships");
}

void test_append_missing_count_and_expand_self_closing_container()
{
    fastxlsx::WorkbookEditor append = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-merged-cell-append-source.xlsx",
            R"(<mergeCells><mergeCell ref="A1:B1"></mergeCell></mergeCells>)"));
    append.merge_cells("Data", {1, 3, 1, 4});
    const std::filesystem::path appended = artifact(
        "fastxlsx-workbook-editor-merged-cell-appended.xlsx");
    append.save_as(appended);
    check_contains(fastxlsx::test::read_zip_entries(appended)
            .at("xl/worksheets/sheet1.xml"),
        R"(<mergeCells count="2"><mergeCell ref="A1:B1"></mergeCell><mergeCell ref="C1:D1"/></mergeCells>)",
        "append should fill missing count and preserve explicit empty child form");

    fastxlsx::WorkbookEditor expand = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-merged-cell-self-closing-source.xlsx",
            R"(<mergeCells count="0"/>)"));
    expand.merge_cells("Data", {2, 1, 2, 2});
    const std::filesystem::path expanded = artifact(
        "fastxlsx-workbook-editor-merged-cell-self-closing-output.xlsx");
    expand.save_as(expanded);
    check_contains(fastxlsx::test::read_zip_entries(expanded)
            .at("xl/worksheets/sheet1.xml"),
        R"(<mergeCells count="1"><mergeCell ref="A2:B2"/></mergeCells>)",
        "self-closing mergeCells should expand with count one");
}

void test_unmerge_child_container_and_absent_noop()
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-unmerge-source.xlsx",
            R"(<mergeCells count="2"><mergeCell ref="A1:B1"/><mergeCell ref="C1:D1"/></mergeCells>)"));
    editor.unmerge_cells("Data", {1, 1, 1, 2});
    check_merged_cell_summary(editor, "Data", 0, 1, "first exact unmerge");
    const std::filesystem::path one_remaining = artifact(
        "fastxlsx-workbook-editor-unmerge-one-remaining.xlsx");
    editor.save_as(one_remaining);
    const std::string one_remaining_xml = fastxlsx::test::read_zip_entries(one_remaining)
        .at("xl/worksheets/sheet1.xml");
    check_contains(one_remaining_xml,
        R"(<mergeCells count="1"><mergeCell ref="C1:D1"/></mergeCells>)",
        "exact unmerge should update count and preserve the other range");
    check_not_contains(one_remaining_xml, R"(ref="A1:B1")",
        "exact unmerge should remove the selected child");

    editor.unmerge_cells("Data", {1, 3, 1, 4});
    const std::size_t count_after_clear = editor.pending_change_count();
    check_merged_cell_summary(editor, "Data", 0, 2, "last exact unmerge");
    editor.unmerge_cells("Data", {1, 3, 1, 4});
    check(editor.pending_change_count() == count_after_clear,
        "repeated absent unmerge should be a clean no-op");
    const std::filesystem::path cleared = artifact(
        "fastxlsx-workbook-editor-unmerge-container-cleared.xlsx");
    editor.save_as(cleared);
    check_not_contains(fastxlsx::test::read_zip_entries(cleared)
            .at("xl/worksheets/sheet1.xml"),
        "<mergeCells", "removing the last range should remove the container");

    fastxlsx::WorkbookEditor absent = fastxlsx::WorkbookEditor::open(
        write_two_sheet_source(
            "fastxlsx-workbook-editor-unmerge-absent-source.xlsx"));
    absent.unmerge_cells("Data", {1, 1, 1, 2});
    check(!absent.has_pending_changes() && !absent.has_unsaved_changes()
            && absent.pending_change_count() == 0
            && absent.pending_worksheet_edits().empty(),
        "unmerge with no mergeCells container should not publish state");

    fastxlsx::WorkbookEditor disjoint = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-unmerge-disjoint-source.xlsx",
            R"(<mergeCells count="1"><mergeCell ref="C1:D1"/></mergeCells>)"));
    disjoint.unmerge_cells("Data", {2, 1, 2, 2});
    check(!disjoint.has_pending_changes() && !disjoint.has_unsaved_changes()
            && disjoint.pending_change_count() == 0,
        "disjoint unmerge in a non-empty container should be a clean no-op");

    fastxlsx::WorkbookEditor self_closing = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-unmerge-self-closing-noop-source.xlsx",
            R"(<mergeCells count="0"/>)"));
    self_closing.unmerge_cells("Data", {1, 1, 1, 2});
    check(!self_closing.has_pending_changes() && !self_closing.has_unsaved_changes()
            && self_closing.pending_change_count() == 0,
        "unmerge in an empty self-closing container should be a clean no-op");

    fastxlsx::WorkbookEditor explicit_child = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-unmerge-explicit-child-source.xlsx",
            R"(<mergeCells count="2"><mergeCell ref="A1:B1"></mergeCell><mergeCell ref="C1:D1"/></mergeCells>)"));
    explicit_child.unmerge_cells("Data", {1, 1, 1, 2});
    const std::filesystem::path explicit_child_output = artifact(
        "fastxlsx-workbook-editor-unmerge-explicit-child-output.xlsx");
    explicit_child.save_as(explicit_child_output);
    const std::string explicit_child_xml =
        fastxlsx::test::read_zip_entries(explicit_child_output)
            .at("xl/worksheets/sheet1.xml");
    check_contains(explicit_child_xml,
        R"(<mergeCells count="1"><mergeCell ref="C1:D1"/></mergeCells>)",
        "unmerge should remove an explicit opening/closing mergeCell child");
    check_not_contains(explicit_child_xml, R"(ref="A1:B1")",
        "explicit child removal should erase the entire selected element");
}

void test_prefixed_qname_preservation()
{
    fastxlsx::WorkbookEditor missing = fastxlsx::WorkbookEditor::open(
        write_prefixed_merged_cell_source(
            "fastxlsx-workbook-editor-merged-cell-prefixed-missing-source.xlsx", {}));
    missing.merge_cells("Data", {1, 1, 1, 2});
    const std::filesystem::path missing_output = artifact(
        "fastxlsx-workbook-editor-merged-cell-prefixed-missing-output.xlsx");
    missing.save_as(missing_output);
    const std::string missing_xml = fastxlsx::test::read_zip_entries(missing_output)
        .at("xl/worksheets/sheet1.xml");
    check_contains(missing_xml,
        R"(<x:mergeCells count="1"><x:mergeCell ref="A1:B1"/></x:mergeCells>)",
        "missing prefixed mergeCells should use the worksheet root prefix");
    check_not_contains(missing_xml, "<mergeCells",
        "prefixed insertion should not introduce an unqualified container");

    fastxlsx::WorkbookEditor append = fastxlsx::WorkbookEditor::open(
        write_prefixed_merged_cell_source(
            "fastxlsx-workbook-editor-merged-cell-prefixed-append-source.xlsx",
            R"(<x:mergeCells count="1"><x:mergeCell ref="A1:B1"/></x:mergeCells>)"));
    append.merge_cells("Data", {2, 1, 2, 2});
    const std::filesystem::path append_output = artifact(
        "fastxlsx-workbook-editor-merged-cell-prefixed-append-output.xlsx");
    append.save_as(append_output);
    check_contains(fastxlsx::test::read_zip_entries(append_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<x:mergeCells count="2"><x:mergeCell ref="A1:B1"/><x:mergeCell ref="A2:B2"/></x:mergeCells>)",
        "prefixed append should reuse the existing container prefix");

    fastxlsx::WorkbookEditor expand = fastxlsx::WorkbookEditor::open(
        write_prefixed_merged_cell_source(
            "fastxlsx-workbook-editor-merged-cell-prefixed-self-closing-source.xlsx",
            R"(<x:mergeCells count="0"/>)"));
    expand.merge_cells("Data", {3, 1, 3, 2});
    const std::filesystem::path expand_output = artifact(
        "fastxlsx-workbook-editor-merged-cell-prefixed-self-closing-output.xlsx");
    expand.save_as(expand_output);
    check_contains(fastxlsx::test::read_zip_entries(expand_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<x:mergeCells count="1"><x:mergeCell ref="A3:B3"/></x:mergeCells>)",
        "prefixed self-closing container should expand with matching QNames");
}

void test_table_relationship_cell_and_calc_preservation()
{
    const std::filesystem::path source = write_table_source(
        "fastxlsx-workbook-editor-merged-cell-table-source.xlsx");
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
        R"(<calcChain><c r="D1" i="1"/></calcChain>)");
    source_entries.emplace("custom/opaque.bin", "merged-cell table unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);
    const std::string source_c1_payload = worksheet_cell_record(
        source_entries.at("xl/worksheets/sheet1.xml"), "C1");
    const std::string source_d1_payload = worksheet_cell_record(
        source_entries.at("xl/worksheets/sheet1.xml"), "D1");
    check_contains(source_d1_payload, R"(s="1")",
        "merged-range preservation fixture should use a non-default style");
    check_contains(source_d1_payload, "<f>B2</f>",
        "merged-range preservation fixture should place a formula in the range");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.merge_cells("Data", {1, 3, 1, 4});
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-merged-cell-table-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);

    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<mergeCell ref="C1:D1"/>)",
        "table worksheet should receive the requested merged range");
    check(worksheet_cell_record(entries.at("xl/worksheets/sheet1.xml"), "C1")
            == source_c1_payload,
        "merge metadata should preserve the complete top-left cell payload");
    check(worksheet_cell_record(entries.at("xl/worksheets/sheet1.xml"), "D1")
            == source_d1_payload,
        "merge metadata should preserve the complete styled formula payload");
    check(entries.at("xl/tables/table1.xml") == source_entries.at("xl/tables/table1.xml"),
        "merged-cell edit should preserve the table part");
    check(entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            == source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "merged-cell edit should preserve worksheet relationships");
    check(entries.at("[Content_Types].xml") == source_entries.at("[Content_Types].xml"),
        "merged-cell edit should preserve content types");
    check(entries.at("xl/workbook.xml") == source_entries.at("xl/workbook.xml"),
        "merged-cell edit should preserve workbook calculation metadata");
    check(entries.at("xl/_rels/workbook.xml.rels")
            == source_entries.at("xl/_rels/workbook.xml.rels"),
        "merged-cell edit should preserve the calcChain relationship");
    check(entries.at("xl/calcChain.xml") == source_entries.at("xl/calcChain.xml"),
        "merged-cell edit should preserve calcChain bytes");
    check(entries.at("custom/opaque.bin") == "merged-cell table unknown entry",
        "merged-cell edit should preserve unrelated unknown parts");
}

void test_malformed_existing_metadata_and_overlap_rejected_without_pollution()
{
    const std::vector<std::pair<std::string, std::string>> cases {
        {"invalid-count", R"(<mergeCells count="x"><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"count-mismatch", R"(<mergeCells count="2"><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"self-closing-count", R"(<mergeCells count="1"/>)"},
        {"missing-ref", R"(<mergeCells count="1"><mergeCell/></mergeCells>)"},
        {"container-trailing-attribute", R"(<mergeCells count="1" broken><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"child-trailing-attribute", R"(<mergeCells count="1"><mergeCell ref="A1:B1" broken/></mergeCells>)"},
        {"duplicate-count", R"(<mergeCells count="1" count="1"><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"duplicate-ref", R"(<mergeCells count="1"><mergeCell ref="A1:B1" ref="C1:D1"/></mergeCells>)"},
        {"invalid-ref", R"(<mergeCells count="1"><mergeCell ref="A0:B1"/></mergeCells>)"},
        {"single-cell", R"(<mergeCells count="1"><mergeCell ref="A1"/></mergeCells>)"},
        {"duplicate-container", R"(<mergeCells count="0"/><mergeCells count="0"/>)"},
        {"unsupported-child", R"(<mergeCells count="1"><future/></mergeCells>)"},
        {"child-content", R"(<mergeCells count="1"><mergeCell ref="A1:B1"><future/></mergeCell></mergeCells>)"},
        {"container-text", R"(<mergeCells count="1">BAD<mergeCell ref="A1:B1"/></mergeCells>)"},
        {"child-text", R"(<mergeCells count="1"><mergeCell ref="A1:B1">BAD</mergeCell></mergeCells>)"},
        {"container-cdata", R"(<mergeCells count="1"><![CDATA[BAD]]><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"container-prefix-mismatch", R"(<x:mergeCells xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1"><x:mergeCell ref="A1:B1"/></mergeCells>)"},
        {"child-prefix-mismatch", R"(<x:mergeCells xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1"><mergeCell ref="A1:B1"/></x:mergeCells>)"},
        {"overlapping-existing", R"(<mergeCells count="2"><mergeCell ref="A1:B2"/><mergeCell ref="B2:C3"/></mergeCells>)"},
        {"wrong-order", R"(<dataValidations count="0"/><mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"},
        {"nested", R"(<conditionalFormatting sqref="A1"><mergeCells count="0"/></conditionalFormatting>)"},
    };

    for (const auto& [name, suffix] : cases) {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
            write_merged_cell_suffix_source(
                "fastxlsx-workbook-editor-merged-cell-" + name + ".xlsx", suffix));
        check(threw_fastxlsx_error([&] {
            editor.merge_cells("Data", {4, 1, 4, 2});
        }), "malformed merged-cell source should reject edit: " + name);
        check(!editor.has_pending_changes() && editor.pending_change_count() == 0
                && editor.pending_worksheet_edits().empty(),
            "malformed merged-cell rejection should not publish state: " + name);
    }

    fastxlsx::WorkbookEditor duplicate = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-merged-cell-duplicate-request.xlsx",
            R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"));
    check(threw_fastxlsx_error([&] {
        duplicate.merge_cells("Data", {1, 1, 1, 2});
    }), "merge should reject an exact duplicate range");
    check(threw_fastxlsx_error([&] {
        duplicate.merge_cells("Data", {1, 2, 1, 3});
    }), "merge should reject a partially overlapping range");
    check(threw_fastxlsx_error([&] {
        duplicate.unmerge_cells("Data", {1, 2, 1, 3});
    }), "unmerge should reject a partially overlapping range");
    check(!duplicate.has_pending_changes() && duplicate.pending_change_count() == 0,
        "overlap rejection should preserve clean public state");
}

void test_large_disjoint_overlap_audit()
{
    constexpr std::size_t range_count = 20000;
    std::string suffix = "<mergeCells count=\"" + std::to_string(range_count) + "\">";
    suffix.reserve(suffix.size() + range_count * 34U);
    for (std::size_t row = 1; row <= range_count; ++row) {
        const std::string row_text = std::to_string(row);
        suffix += "<mergeCell ref=\"A" + row_text + ":B" + row_text + "\"/>";
    }
    suffix += "</mergeCells>";

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(
        write_merged_cell_suffix_source(
            "fastxlsx-workbook-editor-merged-cell-large-disjoint-source.xlsx", suffix));
    editor.unmerge_cells("Data", {30000, 3, 30000, 4});
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_change_count() == 0,
        "large disjoint overlap audit should complete as a clean no-op");
}

void test_invalid_input_failure_hook_rename_added_save_retry_and_reopen()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-merged-cell-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.merge_cells("Data", {0, 1, 1, 2});
    }), "merge should reject an invalid range");
    check(threw_fastxlsx_error([&] {
        editor.unmerge_cells("Data", {1, 1, 1, 1});
    }), "unmerge should reject a single-cell range");
    check(threw_fastxlsx_error([&] {
        editor.merge_cells("Missing", {1, 1, 1, 2});
    }), "merge should reject a missing planned worksheet");
    check(!editor.has_pending_changes() && editor.pending_change_count() == 0,
        "invalid merged-cell inputs should not publish state");

    editor.merge_cells("Data", {1, 1, 1, 2});
    const auto before_failure = editor.pending_worksheet_edits();
    const std::size_t count_before_failure = editor.pending_change_count();
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_merged_cell_staging);
        check(threw_fastxlsx_error([&] {
            editor.merge_cells("Data", {2, 1, 2, 2});
        }), "injected merged-cell staging failure should escape");
    }
    check(editor.pending_change_count() == count_before_failure
            && workbook_editor_edit_summaries_equal(
                editor.pending_worksheet_edits(), before_failure),
        "merged-cell staging failure should preserve public state");

    editor.merge_cells("Data", {2, 1, 2, 2});
    editor.unmerge_cells("Data", {1, 1, 1, 2});
    check(!editor.last_edit_error().has_value(),
        "successful merged-cell retry should clear the previous error");
    editor.rename_sheet("Data", "Renamed Data");
    check_merged_cell_summary(editor, "Renamed Data", 2, 1,
        "renamed merged-cell worksheet");

    editor.add_worksheet("Added");
    editor.merge_cells("Added", {1, 1, 2, 1});
    editor.rename_sheet("Added", "Renamed Added");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 2 && summaries.back().added
            && summaries.back().planned_name == "Renamed Added"
            && summaries.back().merged_cell_addition_count == 1,
        "added worksheet rename should migrate merged-cell diagnostics");
    check(threw_fastxlsx_error([&] {
        editor.remove_worksheet("Renamed Added");
    }), "worksheet removal should reject queued merged-cell edits");

    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-merged-cell-missing")
            / "parent" / "output.xlsx");
    }), "merged-cell save should fail when the output parent is missing");
    check(editor.has_unsaved_changes(),
        "failed merged-cell save should retain unsaved retry state");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-merged-cell-retry-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<mergeCell ref="A2:B2"/>)",
        "save retry should retain the final source worksheet merged range");
    check_not_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(ref="A1:B1")",
        "save retry should not restore the unmerged source range");
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<mergeCell ref="A1:A2"/>)",
        "same-session added worksheet should accept a merged range");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.unmerge_cells("Renamed Data", {2, 1, 2, 2});
    const std::filesystem::path cleared = artifact(
        "fastxlsx-workbook-editor-merged-cell-reopen-cleared.xlsx");
    reopened.save_as(cleared);
    const auto cleared_entries = fastxlsx::test::read_zip_entries(cleared);
    check_not_contains(cleared_entries.at("xl/worksheets/sheet1.xml"),
        "<mergeCells", "reopened editor should remove the final source merge");
    check_contains(cleared_entries.at("xl/worksheets/sheet3.xml"),
        R"(<mergeCell ref="A1:A2"/>)",
        "reopened unmerge should preserve other worksheet merged ranges");
}

} // namespace

int main()
{
    try {
        test_insert_schema_order_and_unknown_part_preservation();
        test_append_missing_count_and_expand_self_closing_container();
        test_unmerge_child_container_and_absent_noop();
        test_prefixed_qname_preservation();
        test_table_relationship_cell_and_calc_preservation();
        test_malformed_existing_metadata_and_overlap_rejected_without_pollution();
        test_large_disjoint_overlap_audit();
        test_invalid_input_failure_hook_rename_added_save_retry_and_reopen();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor merged-cell checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor merged-cell tests passed");
    return 0;
}
