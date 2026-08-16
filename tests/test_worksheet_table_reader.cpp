#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view table_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";
constexpr std::string_view standard_table_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml";

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename Callback>
void expect_fastxlsx_error(Callback&& callback, std::string_view expected_text)
{
    bool matched = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        matched = std::string_view(error.what()).find(expected_text)
            != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: "
            + std::string(expected_text));
    }
}

struct TableFixturePart {
    std::string entry_name;
    std::string xml;
    std::string content_type = std::string(standard_table_content_type);
};

std::map<std::string, std::string> workbook_entries(
    std::string worksheet_xml,
    std::string worksheet_relationships,
    std::vector<TableFixturePart> tables)
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    for (const TableFixturePart& table : tables) {
        content_types += "<Override PartName=\"/" + table.entry_name
            + "\" ContentType=\"" + table.content_type + "\"/>";
    }
    content_types += "</Types>";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/worksheets/sheet1.xml", std::move(worksheet_xml));
    if (!worksheet_relationships.empty()) {
        fastxlsx::test::insert_zip_entry(entries,
            "xl/worksheets/_rels/sheet1.xml.rels",
            std::move(worksheet_relationships));
    }
    for (TableFixturePart& table : tables) {
        fastxlsx::test::insert_zip_entry(
            entries, table.entry_name, std::move(table.xml));
    }
    return entries;
}

std::filesystem::path write_fixture(
    std::string_view name,
    std::string worksheet_xml,
    std::string worksheet_relationships,
    std::vector<TableFixturePart> tables)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(path,
        workbook_entries(std::move(worksheet_xml),
            std::move(worksheet_relationships), std::move(tables)));
    return path;
}

std::string representative_worksheet_xml()
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<x:sheetData/>)"
        R"(<x:tableParts count="2">)"
        R"(<x:tablePart rel:id="rIdTable1"/>)"
        R"(<x:tablePart rel:id="rIdTable2"></x:tablePart>)"
        R"(</x:tableParts>)"
        R"(</x:worksheet>)";
}

std::string representative_relationships()
{
    return std::string(
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdTable1" Type=")")
        + std::string(table_relationship_type)
        + R"(" Target="../tables/table%31.xml"/>)"
          R"(<Relationship Id="rIdTable2" Type=")"
        + std::string(table_relationship_type)
        + R"(" Target="/xl/tables/table2.xml"/>)"
          R"(</Relationships>)";
}

std::string first_table_xml()
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<t:table xmlns:t="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="7" name="InventoryTable" displayName="Inventory_Display" ref="$A$1:$B$3" totalsRowShown="0">)"
        R"(<t:autoFilter ref="A1:B3"></t:autoFilter>)"
        R"(<t:tableColumns count="2">)"
        R"(<t:tableColumn id="11" name="Item &amp; Code"/>)"
        R"(<t:tableColumn id="12" name="Qty"></t:tableColumn>)"
        R"(</t:tableColumns>)"
        R"(<t:tableStyleInfo name="TableStyleMedium9" showFirstColumn="0" showLastColumn="0" showRowStripes="1" showColumnStripes="0"/>)"
        R"(</t:table>)";
}

std::string second_table_xml()
{
    return R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="8" name="SecondTable" displayName="SecondTable" ref="C1:D2" headerRowCount="1" totalsRowCount="0">)"
        R"(<tableColumns count="2"><tableColumn id="1" name="Kind"/><tableColumn id="2" name="Value"/></tableColumns>)"
        R"(</table>)";
}

std::vector<TableFixturePart> representative_tables()
{
    return {
        {"xl/tables/table1.xml", first_table_xml()},
        {"xl/tables/table2.xml", second_table_xml()},
    };
}

std::string single_table_worksheet(std::string_view relationship_id = "rIdTable1")
{
    return std::string(
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData/><tableParts count="1"><tablePart r:id=")")
        + std::string(relationship_id)
        + R"("/></tableParts></worksheet>)";
}

std::string single_table_relationships(
    std::string_view target = "../tables/table1.xml",
    std::string_view type = table_relationship_type,
    std::string_view target_mode = {})
{
    std::string xml =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdTable1" Type=")";
    xml += type;
    xml += R"(" Target=")";
    xml += target;
    xml += '"';
    if (!target_mode.empty()) {
        xml += R"( TargetMode=")";
        xml += target_mode;
        xml += '"';
    }
    xml += R"(/></Relationships>)";
    return xml;
}

std::filesystem::path write_single_table_fixture(
    std::string_view name,
    std::string table_xml,
    std::string relationships = single_table_relationships(),
    std::string content_type = std::string(standard_table_content_type),
    std::string worksheet_xml = single_table_worksheet())
{
    return write_fixture(name, std::move(worksheet_xml),
        std::move(relationships),
        {{"xl/tables/table1.xml", std::move(table_xml), std::move(content_type)}});
}

void test_projects_linked_tables_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-table-reader-representative.xlsx",
        representative_worksheet_xml(), representative_relationships(),
        representative_tables());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetTableView> values;
    fastxlsx::WorksheetTableReadCallbacks callbacks;
    callbacks.on_table = [&](const fastxlsx::WorksheetTableView& value) {
        values.push_back(value);
    };
    const fastxlsx::WorksheetTableReadSummary summary =
        reader.read_worksheet_tables("Data", callbacks);

    check(values.size() == 2 && values[0].index == 0 && values[1].index == 1,
        "table source-order projection mismatch");
    check(values[0].name == "InventoryTable"
            && values[0].id == 7
            && values[0].display_name == "Inventory_Display"
            && values[0].range.first_row == 1
            && values[0].range.last_row == 3
            && values[0].range.last_column == 2,
        "first table identity/range projection mismatch");
    check(values[0].columns.size() == 2
            && values[0].columns[0].id == 11
            && values[0].columns[0].name == "Item & Code"
            && values[0].columns[1].id == 12
            && values[0].columns[1].name == "Qty",
        "first table header projection mismatch");
    check(values[0].auto_filter_range.has_value()
            && values[0].auto_filter_range->last_row == 3
            && !values[1].auto_filter_range.has_value(),
        "table-local auto-filter projection mismatch");
    check(values[0].style_name == "TableStyleMedium9"
            && !values[0].show_first_column
            && !values[0].show_last_column
            && values[0].show_row_stripes
            && !values[0].show_column_stripes,
        "table style projection mismatch");
    check(values[1].name == "SecondTable"
            && values[1].columns.size() == 2
            && values[1].range.first_column == 3
            && values[1].range.last_column == 4,
        "second table projection mismatch");
    check(summary.table_count == 2 && summary.column_count == 4
            && summary.auto_filter_count == 1,
        "table reader summary counts mismatch");
    check(summary.peak_xml_nesting_depth >= 2,
        "table reader nesting telemetry mismatch");
    check(summary.peak_relationship_id_bytes >= 9,
        "table reader relationship-id telemetry mismatch");
    check(summary.peak_relationship_target_bytes >= 20,
        "table reader relationship-target telemetry mismatch");
    check(summary.peak_table_name_bytes >= 17,
        "table reader name telemetry mismatch");
    check(summary.peak_columns_per_table == 2,
        "table reader column-count telemetry mismatch");
    check(summary.peak_column_name_bytes >= 11,
        "table reader column-name telemetry mismatch");
    check(summary.peak_range_reference_bytes >= 9,
        "table reader range telemetry mismatch");
    check(summary.peak_retained_relationship_count == 2,
        "table reader retained-relationship telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "table reader changed the source package");
}

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-table-reader-retry.xlsx",
        representative_worksheet_xml(), representative_relationships(),
        representative_tables());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetTableReadCallbacks throwing;
    throwing.on_table = [](const fastxlsx::WorksheetTableView&) {
        throw CallbackFailure("table callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_tables("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "table callback stopped traversal";
    }
    check(saw_exact_exception,
        "table callback exception should propagate unchanged");

    std::size_t count = 0;
    fastxlsx::WorksheetTableReadCallbacks retry;
    retry.on_table = [&](const fastxlsx::WorksheetTableView&) { ++count; };
    const auto summary = reader.read_worksheet_tables("Data", retry);
    check(count == 2 && summary.table_count == 2,
        "table reader should retry from the worksheet after callback failure");
}

void test_projects_writer_compatible_totals_and_style()
{
    const std::filesystem::path path = write_single_table_fixture(
        "worksheet-table-reader-totals.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="9" name="TotalsTable" displayName="TotalsTable" ref="A1:B3" totalsRowCount="1"><autoFilter ref="A1:B2"/><tableColumns count="2"><tableColumn id="1" name="Metric" totalsRowLabel="Total"/><tableColumn id="2" name="Value" totalsRowFunction="sum"/></tableColumns><tableStyleInfo name="TableStyleMedium4" showFirstColumn="1" showLastColumn="0" showRowStripes="0" showColumnStripes="1"/></table>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetTableView> values;
    fastxlsx::WorksheetTableReadCallbacks callbacks;
    callbacks.on_table = [&](const fastxlsx::WorksheetTableView& value) {
        values.push_back(value);
    };
    const auto summary = reader.read_worksheet_tables("Data", callbacks);

    check(values.size() == 1 && values[0].id == 9
            && values[0].show_totals_row
            && values[0].auto_filter_range.has_value()
            && values[0].auto_filter_range->last_row == 2,
        "writer-compatible totals table projection mismatch");
    check(values[0].columns.size() == 2
            && values[0].columns[0].totals_label == "Total"
            && !values[0].columns[0].totals_function.has_value()
            && values[0].columns[1].totals_function
                == fastxlsx::TableTotalsFunction::Sum,
        "writer-compatible table column totals projection mismatch");
    check(values[0].style_name == "TableStyleMedium4"
            && values[0].show_first_column
            && !values[0].show_last_column
            && !values[0].show_row_stripes
            && values[0].show_column_stripes
            && summary.table_count == 1,
        "writer-compatible table style projection mismatch");
}

void test_absent_and_foreign_table_parts_are_clean()
{
    const std::filesystem::path absent = write_fixture(
        "worksheet-table-reader-absent.xlsx",
        R"(<worksheet><sheetData/></worksheet>)", {}, {});
    const fastxlsx::WorkbookReader absent_reader =
        fastxlsx::WorkbookReader::open(absent);
    check(absent_reader.read_worksheet_tables("Data").table_count == 0,
        "absent tableParts should be a clean empty result");

    const std::filesystem::path foreign = write_fixture(
        "worksheet-table-reader-foreign-extension.xlsx",
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:f="urn:foreign"><x:sheetData/><x:extLst><f:tableParts><f:tablePart/></f:tableParts></x:extLst></x:worksheet>)",
        {}, {});
    const fastxlsx::WorkbookReader foreign_reader =
        fastxlsx::WorkbookReader::open(foreign);
    check(foreign_reader.read_worksheet_tables("Data").table_count == 0,
        "foreign extension local names must not alias worksheet tableParts");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-table-reader-guardrails.xlsx",
        representative_worksheet_xml(), representative_relationships(),
        representative_tables());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetTableReaderOptions options;
    options.max_table_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_table_count");
    options = {};
    options.max_relationship_id_bytes = 4;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_relationship_id_bytes");
    options = {};
    options.max_relationship_target_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_relationship_target_bytes");
    options = {};
    options.max_table_name_bytes = 4;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_table_name_bytes");
    options = {};
    options.max_columns_per_table = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_columns_per_table");
    options = {};
    options.max_column_name_bytes = 3;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_column_name_bytes");
    options = {};
    options.max_range_reference_bytes = 4;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_range_reference_bytes");
    options = {};
    options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "bounded input window");

    options = {};
    options.max_relationship_target_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data", {}, options); },
        "nonzero max_relationship_target_bytes");
}

void test_relationship_and_content_type_audit()
{
    const std::filesystem::path missing_rels = write_single_table_fixture(
        "worksheet-table-reader-missing-rels.xlsx", first_table_xml(), {});
    const auto missing_rels_reader = fastxlsx::WorkbookReader::open(missing_rels);
    expect_fastxlsx_error(
        [&] { (void)missing_rels_reader.read_worksheet_tables("Data"); },
        "requires worksheet relationships");

    const std::filesystem::path missing_id = write_single_table_fixture(
        "worksheet-table-reader-missing-id.xlsx", first_table_xml(),
        single_table_relationships(), std::string(standard_table_content_type),
        single_table_worksheet("rIdMissing"));
    const auto missing_id_reader = fastxlsx::WorkbookReader::open(missing_id);
    expect_fastxlsx_error(
        [&] { (void)missing_id_reader.read_worksheet_tables("Data"); },
        "relationship id is missing");

    const std::filesystem::path wrong_type = write_single_table_fixture(
        "worksheet-table-reader-wrong-type.xlsx", first_table_xml(),
        single_table_relationships("../tables/table1.xml",
            "urn:not-a-table"));
    const auto wrong_type_reader = fastxlsx::WorkbookReader::open(wrong_type);
    expect_fastxlsx_error(
        [&] { (void)wrong_type_reader.read_worksheet_tables("Data"); },
        "wrong type");

    const std::filesystem::path external = write_single_table_fixture(
        "worksheet-table-reader-external.xlsx", first_table_xml(),
        single_table_relationships("https://example.invalid/table.xml",
            table_relationship_type, "External"));
    const auto external_reader = fastxlsx::WorkbookReader::open(external);
    expect_fastxlsx_error(
        [&] { (void)external_reader.read_worksheet_tables("Data"); },
        "must be internal");

    const std::filesystem::path unknown = write_single_table_fixture(
        "worksheet-table-reader-unknown-target.xlsx", first_table_xml(),
        single_table_relationships("../tables/missing.xml"));
    const auto unknown_reader = fastxlsx::WorkbookReader::open(unknown);
    expect_fastxlsx_error(
        [&] { (void)unknown_reader.read_worksheet_tables("Data"); },
        "unknown part");

    const std::filesystem::path wrong_content_type = write_single_table_fixture(
        "worksheet-table-reader-wrong-content-type.xlsx", first_table_xml(),
        single_table_relationships(), "application/xml");
    const auto wrong_content_type_reader =
        fastxlsx::WorkbookReader::open(wrong_content_type);
    expect_fastxlsx_error(
        [&] { (void)wrong_content_type_reader.read_worksheet_tables("Data"); },
        "wrong content type");

    const std::filesystem::path invalid_percent = write_single_table_fixture(
        "worksheet-table-reader-invalid-percent.xlsx", first_table_xml(),
        single_table_relationships("../tables/table%XZ.xml"));
    const auto invalid_percent_reader =
        fastxlsx::WorkbookReader::open(invalid_percent);
    expect_fastxlsx_error(
        [&] { (void)invalid_percent_reader.read_worksheet_tables("Data"); },
        "percent encoding");

    const std::string duplicate_target_relationships =
        std::string(
            R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
            R"(<Relationship Id="rIdTable1" Type=")")
        + std::string(table_relationship_type)
        + R"(" Target="../tables/table1.xml"/>)"
          R"(<Relationship Id="rIdTable2" Type=")"
        + std::string(table_relationship_type)
        + R"(" Target="../tables/./table1.xml"/>)"
          R"(</Relationships>)";
    const std::filesystem::path duplicate_target = write_fixture(
        "worksheet-table-reader-duplicate-target.xlsx",
        representative_worksheet_xml(), duplicate_target_relationships,
        {{"xl/tables/table1.xml", first_table_xml()}});
    const auto duplicate_target_reader =
        fastxlsx::WorkbookReader::open(duplicate_target);
    expect_fastxlsx_error(
        [&] { (void)duplicate_target_reader.read_worksheet_tables("Data"); },
        "target unique parts");
}

void expect_table_shape_error(
    std::string_view artifact_name,
    std::string table_xml,
    std::string_view expected_text)
{
    const std::filesystem::path path = write_single_table_fixture(
        artifact_name, std::move(table_xml));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_tables("Data"); }, expected_text);
}

void test_rejects_unsupported_and_malformed_table_shapes()
{
    expect_table_shape_error("worksheet-table-reader-missing-name.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" displayName="T" ref="A1:B2"><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "requires id, name, displayName, and ref");
    expect_table_shape_error("worksheet-table-reader-count-mismatch.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableColumns count="1"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "count does not match");
    expect_table_shape_error("worksheet-table-reader-filter-boundary.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B3"><autoFilter ref="A1:B2"/><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "autoFilter boundary");
    expect_table_shape_error("worksheet-table-reader-filter-criteria.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><autoFilter ref="A1:B2"><filterColumn colId="0"/></autoFilter><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "unsupported nested table semantics");
    expect_table_shape_error("worksheet-table-reader-totals-filter-boundary.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B3" totalsRowCount="1"><autoFilter ref="A1:B3"/><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B" totalsRowFunction="sum"/></tableColumns></table>)",
        "autoFilter boundary");
    expect_table_shape_error("worksheet-table-reader-unsupported-totals-function.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B3" totalsRowCount="1"><autoFilter ref="A1:B2"/><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B" totalsRowFunction="custom"/></tableColumns></table>)",
        "unsupported totalsRowFunction");
    expect_table_shape_error("worksheet-table-reader-totals-without-function.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B3" totalsRowCount="1"><autoFilter ref="A1:B2"/><tableColumns count="2"><tableColumn id="1" name="A" totalsRowLabel="Total"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "requires at least one totals function");
    expect_table_shape_error("worksheet-table-reader-calculated-formula.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableColumns count="2"><tableColumn id="1" name="A"><calculatedColumnFormula>A2*2</calculatedColumnFormula></tableColumn><tableColumn id="2" name="B"/></tableColumns></table>)",
        "unsupported nested table semantics");
    expect_table_shape_error("worksheet-table-reader-duplicate-column.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableColumns count="2"><tableColumn id="1" name="Name"/><tableColumn id="2" name="name"/></tableColumns></table>)",
        "names must be unique");
    expect_table_shape_error("worksheet-table-reader-schema-order.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableStyleInfo name="TableStyleMedium2"/><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "not in schema order");
    expect_table_shape_error("worksheet-table-reader-missing-namespace.xlsx",
        R"(<table id="1" name="T" displayName="T" ref="A1:B2"><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "requires the spreadsheet namespace declaration");
    expect_table_shape_error("worksheet-table-reader-namespace-rebind.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableColumns xmlns="urn:foreign" count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns></table>)",
        "not in the spreadsheet namespace");
    expect_table_shape_error("worksheet-table-reader-extension.xlsx",
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="T" displayName="T" ref="A1:B2"><tableColumns count="2"><tableColumn id="1" name="A"/><tableColumn id="2" name="B"/></tableColumns><extLst/></table>)",
        "does not support table extensions");

    const std::filesystem::path count_mismatch = write_fixture(
        "worksheet-table-reader-tableparts-count.xlsx",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData/><tableParts count="2"><tablePart r:id="rIdTable1"/></tableParts></worksheet>)",
        single_table_relationships(),
        {{"xl/tables/table1.xml", first_table_xml()}});
    const auto count_reader = fastxlsx::WorkbookReader::open(count_mismatch);
    expect_fastxlsx_error(
        [&] { (void)count_reader.read_worksheet_tables("Data"); },
        "count does not match");

    const std::filesystem::path wrong_namespace = write_fixture(
        "worksheet-table-reader-relationship-namespace.xlsx",
        R"(<worksheet xmlns:r="urn:foreign"><sheetData/><tableParts count="1"><tablePart r:id="rIdTable1"/></tableParts></worksheet>)",
        single_table_relationships(),
        {{"xl/tables/table1.xml", first_table_xml()}});
    const auto namespace_reader = fastxlsx::WorkbookReader::open(wrong_namespace);
    expect_fastxlsx_error(
        [&] { (void)namespace_reader.read_worksheet_tables("Data"); },
        "OpenXML relationship id");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_tables()
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(
        "worksheet-table-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions writer_options;
    writer_options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer =
        fastxlsx::WorkbookWriter::create(path, writer_options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty")});
    sheet.append_row({fastxlsx::CellView::text("One"),
        fastxlsx::CellView::number(1.0)});
    sheet.append_row({fastxlsx::CellView::text("Two"),
        fastxlsx::CellView::number(2.0)});
    fastxlsx::TableOptions table;
    table.name = "GeneratedTable";
    table.column_names = {"Name", "Qty"};
    table.style_name = "TableStyleMedium4";
    sheet.add_table({1, 1, 3, 2}, table);
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetTableView> values;
    fastxlsx::WorksheetTableReadCallbacks callbacks;
    callbacks.on_table = [&](const fastxlsx::WorksheetTableView& value) {
        values.push_back(value);
    };
    const auto summary = reader.read_worksheet_tables("Data", callbacks);
    check(values.size() == 1
            && values[0].name == "GeneratedTable"
            && values[0].display_name == "GeneratedTable"
            && values[0].columns.size() == 2
            && values[0].columns[1].name == "Qty"
            && values[0].auto_filter_range.has_value()
            && values[0].auto_filter_range->last_row == 3
            && summary.table_count == 1
            && summary.column_count == 2,
        "DEFLATE worksheet table traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_linked_tables_in_source_order();
        test_callback_failure_allows_retry();
        test_projects_writer_compatible_totals_and_style();
        test_absent_and_foreign_table_parts_are_clean();
        test_guardrails();
        test_relationship_and_content_type_audit();
        test_rejects_unsupported_and_malformed_table_shapes();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_tables();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "All worksheet table reader tests passed\n";
    return 0;
}
