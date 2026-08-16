#include "../src/package_editor.hpp"
#include <fastxlsx/worksheet_reader.hpp>

#include "test_workbook_editor_facade_common.hpp"

constexpr std::string_view table_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";

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

    ScopedWorksheetReplacementStagedHook(
        const ScopedWorksheetReplacementStagedHook&) = delete;
    ScopedWorksheetReplacementStagedHook& operator=(
        const ScopedWorksheetReplacementStagedHook&) = delete;
};

void fail_after_table_staging()
{
    throw fastxlsx::FastXlsxError("injected table staging failure");
}

fastxlsx::TableOptions basic_table(
    std::string name, std::vector<std::string> columns)
{
    fastxlsx::TableOptions options;
    options.name = std::move(name);
    options.column_names = std::move(columns);
    return options;
}

std::string sheet_data_xml(const std::string& worksheet_xml)
{
    const std::size_t begin = worksheet_xml.find("<sheetData");
    const std::size_t end = worksheet_xml.find("</sheetData>");
    if (begin == std::string::npos || end == std::string::npos) {
        throw std::runtime_error("worksheet fixture has no complete sheetData");
    }
    return worksheet_xml.substr(begin, end + std::string_view("</sheetData>").size() - begin);
}

std::vector<fastxlsx::WorksheetTableView> read_tables(
    const std::filesystem::path& path, std::string_view sheet_name)
{
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetTableView> values;
    fastxlsx::WorksheetTableReadCallbacks callbacks;
    callbacks.on_table = [&](const fastxlsx::WorksheetTableView& value) {
        values.push_back(value);
    };
    (void)reader.read_worksheet_tables(sheet_name, callbacks);
    return values;
}

std::filesystem::path write_source_with_existing_table(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty")});
    data.append_row({fastxlsx::CellView::text("One"),
        fastxlsx::CellView::number(1.0)});
    data.add_table({1, 1, 2, 2},
        basic_table("ExistingTable", {"Name", "Qty"}));
    auto other = writer.add_worksheet("Other");
    other.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

std::filesystem::path write_source_with_two_existing_tables(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"), fastxlsx::CellView::text("Spacer"),
        fastxlsx::CellView::text("Region"), fastxlsx::CellView::text("Amount")});
    data.append_row({fastxlsx::CellView::text("One"),
        fastxlsx::CellView::number(1.0), fastxlsx::CellView::blank(),
        fastxlsx::CellView::text("East"), fastxlsx::CellView::number(2.0)});
    data.add_table({1, 1, 2, 2},
        basic_table("ExistingTable", {"Name", "Qty"}));
    data.add_table({1, 4, 2, 5},
        basic_table("RemainingTable", {"Region", "Amount"}));
    auto other = writer.add_worksheet("Other");
    other.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

const fastxlsx::WorkbookEditorWorksheetEditSummary* find_summary(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& summaries,
    std::string_view planned_name)
{
    const auto summary = std::find_if(summaries.begin(), summaries.end(),
        [planned_name](const auto& value) {
            return value.planned_name == planned_name;
        });
    return summary == summaries.end() ? nullptr : &*summary;
}

void test_adds_table_package_transaction_and_reader_projection()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-table-basic-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string original_sheet_data =
        sheet_data_xml(source_entries.at("xl/worksheets/sheet1.xml"));
    source_entries.emplace("custom/table-opaque.bin", "preserve table opaque entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    fastxlsx::TableOptions options = basic_table(
        "TotalsTable", {"Metric", "Value"});
    options.show_totals_row = true;
    options.column_totals_functions = {
        std::nullopt, fastxlsx::TableTotalsFunction::Sum};
    options.column_totals_labels = {"Total", ""};
    options.style_name = "TableStyleMedium4";
    options.show_first_column = true;
    options.show_row_stripes = false;
    options.show_column_stripes = true;

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-basic-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_table("Data", {1, 1, 3, 2}, options);
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().table_addition_count == 1,
        "table addition should publish one worksheet diagnostic");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.contains("xl/tables/table1.xml"),
        "table insertion should generate table1.xml");
    check_contains(entries.at("[Content_Types].xml"),
        R"(<Override PartName="/xl/tables/table1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"/>)",
        "table insertion should add a content-type override");
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<tableParts count="1"><tablePart r:id="rId1"/></tableParts>)",
        "table insertion should add one worksheet tableParts reference");
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")",
        "table insertion should ensure the worksheet relationship namespace");
    check_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        std::string(R"(Type=")") + std::string(table_relationship_type)
            + R"(" Target="../tables/table1.xml")",
        "table insertion should add the worksheet table relationship");
    check(sheet_data_xml(entries.at("xl/worksheets/sheet1.xml"))
            == original_sheet_data,
        "table insertion must not modify worksheet cell payloads");
    check(entries.at("custom/table-opaque.bin") == "preserve table opaque entry",
        "table insertion should preserve unknown package entries");

    const auto tables = read_tables(output, "Data");
    check(tables.size() == 1 && tables[0].id == 1
            && tables[0].name == "TotalsTable"
            && tables[0].show_totals_row
            && tables[0].columns[0].totals_label == "Total"
            && tables[0].columns[1].totals_function
                == fastxlsx::TableTotalsFunction::Sum
            && tables[0].style_name == "TableStyleMedium4"
            && tables[0].show_first_column
            && !tables[0].show_row_stripes
            && tables[0].show_column_stripes,
        "reopened reader should project inserted totals/style metadata");
}

void test_continuous_add_self_closing_and_reopen()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-table-continuous-source.xlsx");
    auto entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"),
        "</worksheet>", R"(<tableParts count="0"/></worksheet>)");
    fastxlsx::test::write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_table("Data", {1, 1, 2, 2},
        basic_table("FirstTable", {"A", "B"}));
    editor.add_table("Data", {1, 4, 2, 5},
        basic_table("SecondTable", {"D", "E"}));
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-continuous-output.xlsx");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<tableParts count="2"><tablePart r:id="rId1"/><tablePart r:id="rId2"/></tableParts>)",
        "self-closing tableParts should expand and support consecutive additions");
    check(output_entries.contains("xl/tables/table1.xml")
            && output_entries.contains("xl/tables/table2.xml"),
        "consecutive table additions should allocate unique part names");
    const auto tables = read_tables(output, "Data");
    check(tables.size() == 2 && tables[0].id == 1 && tables[1].id == 2,
        "consecutive table additions should allocate unique ids and reopen");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.add_table("Untouched", {1, 1, 2, 2},
        basic_table("ThirdTable", {"One", "Two"}));
    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-table-reopened-output.xlsx");
    reopened.save_as(reopened_output);
    const auto reopened_tables = read_tables(reopened_output, "Untouched");
    check(reopened_tables.size() == 1 && reopened_tables[0].id == 3,
        "reopened table insertion should continue workbook-wide table ids");
}

void test_appends_after_source_owned_table_on_same_worksheet()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-source-append-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_table_xml = source_entries.at("xl/tables/table1.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_table("Data", {1, 4, 2, 5},
        basic_table("AppendedTable", {"Region", "Amount"}));
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-source-append-output.xlsx");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.at("xl/tables/table1.xml") == source_table_xml
            && entries.contains("xl/tables/table2.xml"),
        "source table append should preserve table1 and allocate table2");
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<tableParts count="2"><tablePart r:id="rId1"/><tablePart r:id="rId2"/></tableParts>)",
        "source table append should retain the old relationship reference");
    const std::string& relationships =
        entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(relationships, R"(Id="rId1")",
        "source table append should preserve the source relationship id");
    check_contains(relationships, R"(Id="rId2")",
        "source table append should allocate a distinct relationship id");

    const auto tables = read_tables(output, "Data");
    check(tables.size() == 2 && tables[0].name == "ExistingTable"
            && tables[0].id == 1 && tables[1].name == "AppendedTable"
            && tables[1].id == 2,
        "source table append should reopen both tables in worksheet order");
}

void test_conflicts_fail_without_state_pollution()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-conflict-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.add_table("Other", {1, 1, 2, 2},
            basic_table("existingtable", {"A", "B"}));
    }), "table names should be unique workbook-wide ignoring ASCII case");
    check(threw_fastxlsx_error([&] {
        editor.add_table("Data", {2, 2, 3, 3},
            basic_table("OverlapTable", {"B", "C"}));
    }), "new table ranges should not overlap source tables on the same sheet");
    check(threw_fastxlsx_error([&] {
        editor.add_table("Other", {1, 1, 2, 2},
            basic_table("WrongWidth", {"OnlyOne"}));
    }), "table column count should match range width");
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_worksheet_edits().empty(),
        "rejected table calls should not publish package or public state");

    editor.add_table("Other", {1, 1, 2, 2},
        basic_table("ValidTable", {"A", "B"}));
    check(!editor.last_edit_error().has_value()
            && editor.pending_worksheet_edits().front().table_addition_count == 1,
        "a valid retry should clear the previous table error");
    check(threw_fastxlsx_error([&] {
        editor.add_table("Other", {1, 2, 2, 3},
            basic_table("PendingOverlap", {"B", "C"}));
    }), "new table ranges should not overlap pending same-session tables");
    check(editor.pending_change_count() == 1
            && editor.pending_worksheet_edits().front().table_addition_count == 1,
        "pending overlap failure should retain only the prior successful table");
}

void test_failure_retry_rename_and_added_worksheet()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-table-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_table("Data", {1, 1, 2, 2},
        basic_table("FirstTable", {"A", "B"}));
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_table_staging);
        check(threw_fastxlsx_error([&] {
            editor.add_table("Data", {1, 4, 2, 5},
                basic_table("SecondTable", {"D", "E"}));
        }), "injected table staging failure should escape");
    }
    check(editor.pending_change_count() == 1
            && editor.pending_worksheet_edits().front().table_addition_count == 1,
        "staging failure should not publish a second table diagnostic");
    editor.add_table("Data", {1, 4, 2, 5},
        basic_table("SecondTable", {"D", "E"}));
    editor.rename_sheet("Data", "Renamed Data");
    const auto renamed_summaries = editor.pending_worksheet_edits();
    check(renamed_summaries.front().planned_name == "Renamed Data"
            && renamed_summaries.front().table_addition_count == 2,
        "rename should migrate pending table diagnostics");
    check(threw_fastxlsx_error([&] { editor.remove_worksheet("Renamed Data"); }),
        "worksheet removal should reject pending table edits");

    editor.add_worksheet("Added");
    editor.add_table("Added", {1, 1, 2, 2},
        basic_table("AddedTable", {"One", "Two"}));
    editor.rename_sheet("Added", "Renamed Added");

    const std::size_t pending_before_failed_save = editor.pending_change_count();
    const std::size_t unsaved_before_failed_save = editor.unsaved_change_count();
    check(threw_fastxlsx_error([&] {
        editor.save_as(std::filesystem::path {});
    }), "table save_as failure should preserve the staged package transaction");
    check(editor.pending_change_count() == pending_before_failed_save
            && editor.unsaved_change_count() == unsaved_before_failed_save
            && editor.has_unsaved_changes(),
        "table save_as failure should preserve pending and unsaved state");
    const auto summaries_after_failed_save = editor.pending_worksheet_edits();
    const auto added_summary = std::find_if(summaries_after_failed_save.begin(),
        summaries_after_failed_save.end(), [](const auto& summary) {
            return summary.planned_name == "Renamed Added";
        });
    check(added_summary != summaries_after_failed_save.end()
            && added_summary->table_addition_count == 1,
        "table save_as failure should preserve table addition diagnostics");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-retry-output.xlsx");
    editor.save_as(output);
    const auto renamed_tables = read_tables(output, "Renamed Data");
    const auto added_tables = read_tables(output, "Renamed Added");
    check(renamed_tables.size() == 2 && added_tables.size() == 1
            && added_tables[0].id == 3,
        "renamed and same-session added worksheets should retain table additions");
}

void test_updates_source_table_in_place()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-update-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_sheet_data =
        sheet_data_xml(source_entries.at("xl/worksheets/sheet1.xml"));
    const std::string source_relationships =
        source_entries.at("xl/worksheets/_rels/sheet1.xml.rels");

    fastxlsx::TableOptions replacement = basic_table(
        "UpdatedTable", {"Metric", "Value"});
    replacement.show_totals_row = true;
    replacement.column_totals_functions = {
        std::nullopt, fastxlsx::TableTotalsFunction::Average};
    replacement.column_totals_labels = {"Average", ""};
    replacement.style_name = "TableStyleMedium7";
    replacement.show_last_column = true;
    replacement.show_row_stripes = false;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.update_table("Data", "existingtable", {1, 1, 3, 2}, replacement);
    const auto summaries = editor.pending_worksheet_edits();
    const auto* summary = find_summary(summaries, "Data");
    check(summary != nullptr && summary->table_addition_count == 0
            && summary->table_update_count == 1
            && summary->table_removal_count == 0,
        "source table update should publish one update diagnostic");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-update-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.contains("xl/tables/table1.xml")
            && entries.at("xl/worksheets/_rels/sheet1.xml.rels")
                == source_relationships,
        "table update should retain the source part and relationship identity");
    check(sheet_data_xml(entries.at("xl/worksheets/sheet1.xml"))
            == source_sheet_data,
        "table update must not modify worksheet cell payloads");

    const auto tables = read_tables(output, "Data");
    check(tables.size() == 1 && tables[0].id == 1
            && tables[0].name == "UpdatedTable"
            && tables[0].range.last_row == 3
            && tables[0].show_totals_row
            && tables[0].columns[1].totals_function
                == fastxlsx::TableTotalsFunction::Average
            && tables[0].style_name == "TableStyleMedium7"
            && tables[0].show_last_column && !tables[0].show_row_stripes,
        "reopened source table update projection mismatch");
}

void test_removes_source_tables_and_cleans_last_table()
{
    const std::filesystem::path source = write_source_with_two_existing_tables(
        "fastxlsx-workbook-editor-table-remove-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string remaining_table_xml =
        source_entries.at("xl/tables/table2.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.remove_table("Data", "existingtable");
    const std::filesystem::path one_left = artifact(
        "fastxlsx-workbook-editor-table-remove-one-output.xlsx");
    editor.save_as(one_left);

    const auto one_left_entries = fastxlsx::test::read_zip_entries(one_left);
    check(!one_left_entries.contains("xl/tables/table1.xml")
            && one_left_entries.at("xl/tables/table2.xml") == remaining_table_xml,
        "table removal should remove only the selected table part");
    check_contains(one_left_entries.at("xl/worksheets/sheet1.xml"),
        R"(<tableParts count="1"><tablePart r:id="rId2"/></tableParts>)",
        "table removal should retain the remaining tablePart identity");
    check(one_left_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
                .find(R"(Id="rId1")") == std::string::npos
            && one_left_entries.at("xl/worksheets/_rels/sheet1.xml.rels")
                .find(R"(Id="rId2")") != std::string::npos,
        "table removal should remove only the selected worksheet relationship");
    check(one_left_entries.at("[Content_Types].xml")
                .find("/xl/tables/table1.xml") == std::string::npos,
        "table removal should remove the selected content-type override");
    const auto remaining = read_tables(one_left, "Data");
    check(remaining.size() == 1 && remaining[0].name == "RemainingTable",
        "table removal should reopen the remaining table");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(one_left);
    reopened.remove_table("Data", "remainingtable");
    const std::filesystem::path empty = artifact(
        "fastxlsx-workbook-editor-table-remove-last-output.xlsx");
    reopened.save_as(empty);
    const auto empty_entries = fastxlsx::test::read_zip_entries(empty);
    check(empty_entries.at("xl/worksheets/sheet1.xml")
                .find("<tableParts") == std::string::npos,
        "removing the final table should remove the tableParts container");
    check(!empty_entries.contains("xl/tables/table2.xml")
            && empty_entries.at("[Content_Types].xml")
                .find("/xl/tables/table2.xml") == std::string::npos,
        "removing the final table should remove its part and content type");
    check(read_tables(empty, "Data").empty(),
        "removing the final table should reopen as an empty table projection");
}

void test_same_session_lifecycle_noop_and_conflicts()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-table-session-lifecycle-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::TableOptions original =
        basic_table("SessionTable", {"A", "B"});
    editor.add_table("Data", {1, 1, 2, 2}, original);

    const std::size_t pending_before_noop = editor.pending_change_count();
    const std::size_t unsaved_before_noop = editor.unsaved_change_count();
    editor.update_table("Data", "sessiontable", {1, 1, 2, 2}, original);
    check(editor.pending_change_count() == pending_before_noop
            && editor.unsaved_change_count() == unsaved_before_noop,
        "identical table update should be a clean no-op");

    editor.add_table("Data", {1, 4, 2, 5},
        basic_table("ConflictTable", {"D", "E"}));
    const auto before_conflicts = editor.pending_worksheet_edits();
    check(threw_fastxlsx_error([&] {
        editor.update_table("Data", "MissingTable", {1, 7, 2, 8},
            basic_table("MissingTable", {"G", "H"}));
    }), "table update should reject a missing target");
    check(threw_fastxlsx_error([&] {
        editor.remove_table("Data", "MissingTable");
    }), "table removal should reject a missing target");
    check(threw_fastxlsx_error([&] {
        editor.update_table("Data", "SessionTable", {1, 1, 2, 2},
            basic_table("conflicttable", {"A", "B"}));
    }), "table update should reject a pending workbook-wide name conflict");
    check(threw_fastxlsx_error([&] {
        editor.update_table("Data", "SessionTable", {1, 5, 2, 6},
            basic_table("MovedTable", {"E", "F"}));
    }), "table update should reject a pending same-sheet range overlap");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), before_conflicts),
        "rejected table updates should not publish lifecycle state");

    editor.update_table("Data", "SessionTable", {1, 1, 3, 2},
        basic_table("RenamedSessionTable", {"A", "B"}));
    editor.remove_table("Data", "renamedsessiontable");
    editor.add_table("Data", {1, 1, 3, 2},
        basic_table("RenamedSessionTable", {"A", "B"}));
    const auto summaries = editor.pending_worksheet_edits();
    const auto* summary = find_summary(summaries, "Data");
    check(summary != nullptr && summary->table_addition_count == 3
            && summary->table_update_count == 1
            && summary->table_removal_count == 1,
        "same-session add/update/remove diagnostics mismatch");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-session-lifecycle-output.xlsx");
    editor.save_as(output);
    const auto tables = read_tables(output, "Data");
    check(tables.size() == 2 && tables[0].name == "ConflictTable"
            && tables[0].id == 2
            && tables[1].name == "RenamedSessionTable"
            && tables[1].id == 3,
        "removed table name/range should be reusable without reusing its id");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(!output_entries.contains("xl/tables/table1.xml")
            && output_entries.contains("xl/tables/table2.xml")
            && output_entries.contains("xl/tables/table3.xml"),
        "removed generated table part identity must not be reused in-session");
}

void test_lifecycle_after_worksheet_rename_and_add()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-rename-added-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "Renamed Data");
    editor.update_table("Renamed Data", "ExistingTable", {1, 1, 2, 2},
        basic_table("RenamedSourceTable", {"Name", "Qty"}));

    editor.add_worksheet("Added");
    editor.add_table("Added", {1, 1, 2, 2},
        basic_table("AddedTable", {"One", "Two"}));
    editor.rename_sheet("Added", "Renamed Added");
    editor.update_table("Renamed Added", "AddedTable", {1, 1, 3, 2},
        basic_table("UpdatedAddedTable", {"One", "Two"}));

    const auto summaries = editor.pending_worksheet_edits();
    const auto* source_summary = find_summary(summaries, "Renamed Data");
    const auto* added_summary = find_summary(summaries, "Renamed Added");
    check(source_summary != nullptr && source_summary->table_update_count == 1
            && added_summary != nullptr && added_summary->table_addition_count == 1
            && added_summary->table_update_count == 1,
        "renamed source/added worksheet lifecycle diagnostics mismatch");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-rename-added-output.xlsx");
    editor.save_as(output);
    const auto source_tables = read_tables(output, "Renamed Data");
    const auto added_tables = read_tables(output, "Renamed Added");
    check(source_tables.size() == 1
            && source_tables[0].name == "RenamedSourceTable"
            && added_tables.size() == 1
            && added_tables[0].name == "UpdatedAddedTable"
            && added_tables[0].range.last_row == 3,
        "renamed source/added worksheet lifecycle reopen mismatch");
}

void test_update_remove_failure_retry()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-mutation-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_table_staging);
        check(threw_fastxlsx_error([&] {
            editor.update_table("Data", "ExistingTable", {1, 1, 3, 2},
                basic_table("UpdatedTable", {"Name", "Qty"}));
        }), "injected table update staging failure should escape");
    }
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && editor.pending_worksheet_edits().empty(),
        "table update staging failure should preserve clean public state");
    editor.update_table("Data", "ExistingTable", {1, 1, 3, 2},
        basic_table("UpdatedTable", {"Name", "Qty"}));
    const auto before_remove_failure = editor.pending_worksheet_edits();
    const std::size_t pending_before_remove_failure = editor.pending_change_count();
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_table_staging);
        check(threw_fastxlsx_error([&] {
            editor.remove_table("Data", "UpdatedTable");
        }), "injected table removal staging failure should escape");
    }
    check(editor.pending_change_count() == pending_before_remove_failure
            && workbook_editor_edit_summaries_equal(
                editor.pending_worksheet_edits(), before_remove_failure),
        "table removal staging failure should retain the prior update state");
    editor.remove_table("Data", "UpdatedTable");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-table-mutation-retry-output.xlsx");
    editor.save_as(output);
    check(read_tables(output, "Data").empty(),
        "retried table update/removal should save the final empty projection");
}

void test_rejects_uri_aliased_table_part_ownership()
{
    const std::filesystem::path source = write_source_with_existing_table(
        "fastxlsx-workbook-editor-table-uri-alias-base.xlsx");
    auto entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(entries.at("_rels/.rels"), "</Relationships>",
        R"(<Relationship Id="rIdSharedTable" Type="urn:fastxlsx:test:shared-table" Target="xl/%74ables/table1.xml#shared"/></Relationships>)");
    const std::filesystem::path aliased = artifact(
        "fastxlsx-workbook-editor-table-uri-alias-source.xlsx");
    fastxlsx::test::write_stored_zip_entries(aliased, entries);

    fastxlsx::WorkbookEditor update_editor =
        fastxlsx::WorkbookEditor::open(aliased);
    check(threw_fastxlsx_error([&] {
        update_editor.update_table("Data", "ExistingTable", {1, 1, 2, 2},
            basic_table("UpdatedTable", {"Name", "Qty"}));
    }), "table update should reject a package-root URI alias inbound edge");
    check(!update_editor.has_pending_changes(),
        "URI-aliased table update rejection should preserve clean state");

    fastxlsx::WorkbookEditor remove_editor =
        fastxlsx::WorkbookEditor::open(aliased);
    check(threw_fastxlsx_error([&] {
        remove_editor.remove_table("Data", "ExistingTable");
    }), "table removal should reject a package-root URI alias inbound edge");
    check(!remove_editor.has_pending_changes(),
        "URI-aliased table removal rejection should preserve clean state");
}

} // namespace

int main()
{
    try {
        test_adds_table_package_transaction_and_reader_projection();
        test_continuous_add_self_closing_and_reopen();
        test_appends_after_source_owned_table_on_same_worksheet();
        test_conflicts_fail_without_state_pollution();
        test_failure_retry_rename_and_added_worksheet();
        test_updates_source_table_in_place();
        test_removes_source_tables_and_cleans_last_table();
        test_same_session_lifecycle_noop_and_conflicts();
        test_lifecycle_after_worksheet_rename_and_add();
        test_update_remove_failure_retry();
        test_rejects_uri_aliased_table_part_ownership();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor table checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor table tests passed");
    return 0;
}
