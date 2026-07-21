#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
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
    bool failed = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = std::string_view(error.what()).find(expected_text) != std::string_view::npos;
    }
    check(failed, "expected FastXlsxError diagnostic was not observed");
}

std::map<std::string, std::string> workbook_entries(
    std::string worksheet_xml,
    bool include_shared_strings = true,
    bool include_styles = true)
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (include_shared_strings) {
        content_types +=
            R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)";
    }
    if (include_styles) {
        content_types +=
            R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)";
    }
    content_types += "</Types>";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (include_shared_strings) {
        workbook_relationships +=
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)";
    }
    if (include_styles) {
        workbook_relationships +=
            R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)";
    }
    workbook_relationships += "</Relationships>";

    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data &amp; QA" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/worksheets/sheet1.xml", std::move(worksheet_xml));
    if (include_shared_strings) {
        fastxlsx::test::insert_zip_entry(entries, "xl/sharedStrings.xml",
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4"><si><t>zero</t></si><si><t>one</t></si><si><t>two</t></si><si><t>three</t></si></sst>)");
    }
    if (include_styles) {
        fastxlsx::test::insert_zip_entry(entries, "xl/styles.xml",
            R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="1"><font/></fonts><fills count="1"><fill><patternFill patternType="none"/></fill></fills><borders count="1"><border/></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="1" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="2" fontId="0" fillId="0" borderId="0" xfId="0"/></cellXfs></styleSheet>)");
    }
    return entries;
}

std::filesystem::path write_fixture(
    std::string_view name,
    std::string worksheet_xml,
    bool include_shared_strings = true,
    bool include_styles = true)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(path,
        workbook_entries(std::move(worksheet_xml), include_shared_strings, include_styles));
    return path;
}

const std::string& representative_worksheet_xml()
{
    static const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<sheetData>)"
        R"(<row r="1" customHeight="1" ht="18">)"
        R"(<c r="A1" s="2"><v>42.5</v></c>)"
        R"(<c r="B1" t="b"><v>1</v></c>)"
        R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">hello &amp; &#x2603;</t></is></c>)"
        R"(<c r="D1" t="s"><v>3</v></c>)"
        R"(</row>)"
        R"(<row r="3">)"
        R"(<c r="A3" t="str"><f>CONCAT(&quot;a&quot;,&quot;b&quot;)</f><v>ab</v></c>)"
        R"(<c r="B3" t="e"><v>#DIV/0!</v></c>)"
        R"(<c r="C3" t="d"><v>2026-07-21T12:00:00Z</v></c>)"
        R"(<c r="D3"/>)"
        R"(</row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    return xml;
}

struct CopiedCell {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    std::string reference;
    bool has_value = false;
    fastxlsx::WorksheetCellValueKind value_kind =
        fastxlsx::WorksheetCellValueKind::Blank;
    double number_value = 0.0;
    std::string text_value;
    bool boolean_value = false;
    std::uint32_t shared_string_index = 0;
    bool has_formula = false;
    std::string formula_text;
    bool has_style = false;
    std::uint32_t style_index = 0;
};

CopiedCell copy_cell(const fastxlsx::WorksheetCellView& cell)
{
    return CopiedCell {cell.row,
        cell.column,
        std::string(cell.reference),
        cell.has_value,
        cell.value_kind,
        cell.number_value,
        std::string(cell.text_value),
        cell.boolean_value,
        cell.shared_string_index,
        cell.has_formula,
        std::string(cell.formula_text),
        cell.has_style,
        cell.style_index};
}

void test_reader_projects_supported_cells_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-reader-representative.xlsx", representative_worksheet_xml());
    fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    check(reader.source_path() == path, "reader source path mismatch");
    check(reader.worksheet_names() == std::vector<std::string> {"Data & QA"},
        "reader worksheet catalog mismatch");
    check(reader.has_worksheet("Data & QA"), "reader should find decoded sheet name");
    check(!reader.has_worksheet("Missing"), "reader should reject absent sheet name");

    std::vector<std::string> order;
    std::vector<CopiedCell> cells;
    fastxlsx::WorksheetReadCallbacks callbacks;
    callbacks.on_row_start = [&](const fastxlsx::WorksheetRowView& row) {
        order.push_back("row+" + std::to_string(row.row));
    };
    callbacks.on_cell = [&](const fastxlsx::WorksheetCellView& cell) {
        order.push_back(std::string(cell.reference));
        cells.push_back(copy_cell(cell));
    };
    callbacks.on_row_end = [&](const fastxlsx::WorksheetRowView& row) {
        order.push_back("row-" + std::to_string(row.row));
    };

    const fastxlsx::WorksheetReadSummary summary =
        reader.read_worksheet("Data & QA", callbacks);
    check(summary.row_count == 2, "reader row count mismatch");
    check(summary.cell_count == 8, "reader cell count mismatch");
    check(summary.peak_cell_text_bytes >= 2,
        "reader should report active-cell text bytes");
    check(order == std::vector<std::string>({"row+1", "A1", "B1", "C1", "D1",
                         "row-1", "row+3", "A3", "B3", "C3", "D3", "row-3"}),
        "reader callback order mismatch");

    check(cells[0].value_kind == fastxlsx::WorksheetCellValueKind::Number
            && std::abs(cells[0].number_value - 42.5) < 0.000001,
        "numeric projection mismatch");
    check(cells[0].has_style && cells[0].style_index == 2,
        "style projection mismatch");
    check(cells[1].value_kind == fastxlsx::WorksheetCellValueKind::Boolean
            && cells[1].boolean_value,
        "boolean projection mismatch");
    check(cells[2].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[2].text_value == std::string("hello & ") + "\xe2\x98\x83",
        "decoded inline text projection mismatch");
    check(cells[3].value_kind == fastxlsx::WorksheetCellValueKind::SharedStringIndex
            && cells[3].shared_string_index == 3,
        "shared-string index projection mismatch");
    check(cells[4].has_formula && cells[4].formula_text == R"(CONCAT("a","b"))"
            && cells[4].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[4].text_value == "ab",
        "formula and cached text projection mismatch");
    check(cells[5].value_kind == fastxlsx::WorksheetCellValueKind::Error
            && cells[5].text_value == "#DIV/0!",
        "error projection mismatch");
    check(cells[6].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[6].text_value == "2026-07-21T12:00:00Z",
        "date-token projection mismatch");
    check(!cells[7].has_value
            && cells[7].value_kind == fastxlsx::WorksheetCellValueKind::Blank,
        "blank projection mismatch");

    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet("Missing"); }, "worksheet not found");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_releases_entry_and_reader_can_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-reader-callback-retry.xlsx", representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetReadCallbacks throwing_callbacks;
    throwing_callbacks.on_cell = [](const fastxlsx::WorksheetCellView&) {
        throw CallbackFailure("caller stopped traversal");
    };
    bool saw_exact_callback_failure = false;
    try {
        (void)reader.read_worksheet("Data & QA", throwing_callbacks);
    } catch (const CallbackFailure& error) {
        saw_exact_callback_failure = std::string_view(error.what()) == "caller stopped traversal";
    }
    check(saw_exact_callback_failure, "callback exception should propagate unchanged");

    const fastxlsx::WorksheetReadSummary retry = reader.read_worksheet("Data & QA");
    check(retry.row_count == 2 && retry.cell_count == 8,
        "reader should restart worksheet entry after callback failure");
}

void test_reader_projects_formula_without_cached_value_and_empty_text()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-reader-empty-values.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><f>SUM(1,2)</f></c><c r="B1" t="str"><v/></c><c r="C1" t="inlineStr"><is><t/></is></c></row></sheetData></worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<CopiedCell> cells;
    fastxlsx::WorksheetReadCallbacks callbacks;
    callbacks.on_cell = [&](const fastxlsx::WorksheetCellView& cell) {
        cells.push_back(copy_cell(cell));
    };
    const fastxlsx::WorksheetReadSummary summary =
        reader.read_worksheet("Data & QA", callbacks);

    check(summary.row_count == 1 && summary.cell_count == 3,
        "formula-only and empty-text traversal count mismatch");
    check(cells[0].has_formula && cells[0].formula_text == "SUM(1,2)"
            && !cells[0].has_value
            && cells[0].value_kind == fastxlsx::WorksheetCellValueKind::Blank,
        "formula-only projection mismatch");
    check(cells[1].has_value
            && cells[1].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[1].text_value.empty(),
        "empty text projection mismatch");
    check(cells[2].has_value
            && cells[2].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[2].text_value.empty(),
        "empty inline text projection mismatch");
}

void test_reader_move_transfers_open_state()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-reader-move.xlsx", representative_worksheet_xml());
    fastxlsx::WorkbookReader source = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorkbookReader moved = std::move(source);

    check(source.source_path().empty(), "moved-from reader should have an empty path");
    check(source.worksheet_names().empty(),
        "moved-from reader should have an empty worksheet catalog");
    check(!source.has_worksheet("Data & QA"),
        "moved-from reader should not find worksheets");
    expect_fastxlsx_error(
        [&] { (void)source.read_worksheet("Data & QA"); }, "not open");

    const fastxlsx::WorksheetReadSummary summary = moved.read_worksheet("Data & QA");
    check(summary.row_count == 2 && summary.cell_count == 8,
        "moved reader should retain the open workbook state");
}

void test_reader_enforces_memory_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-reader-guardrails.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="str"><f>123456789012</f><v>abcdefghijkl</v></c></row></sheetData></worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetReaderOptions cell_options;
    cell_options.max_xml_window_bytes = 256;
    cell_options.max_cell_text_bytes = 20;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet("Data & QA", {}, cell_options); },
        "max_cell_text_bytes");

    fastxlsx::WorksheetReaderOptions window_options;
    window_options.max_xml_window_bytes = 8;
    window_options.max_cell_text_bytes = 256;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet("Data & QA", {}, window_options); },
        "bounded input window");

    fastxlsx::WorksheetReaderOptions zero_options;
    zero_options.max_xml_window_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet("Data & QA", {}, zero_options); },
        "nonzero max_xml_window_bytes");

    zero_options.max_xml_window_bytes = 256;
    zero_options.max_cell_text_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet("Data & QA", {}, zero_options); },
        "nonzero max_cell_text_bytes");
}

void test_reader_rejects_unsupported_or_malformed_projection_shapes()
{
    const auto expect_worksheet_failure = [](std::string_view file_name,
                                              std::string xml,
                                              std::string_view diagnostic,
                                              bool shared_strings = true,
                                              bool styles = true) {
        const std::filesystem::path path = write_fixture(
            file_name, std::move(xml), shared_strings, styles);
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
        expect_fastxlsx_error(
            [&] { (void)reader.read_worksheet("Data & QA"); }, diagnostic);
    };

    expect_worksheet_failure("worksheet-reader-rich.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><t>rich</t></r></is></c></row></sheetData></worksheet>)",
        "rich or extended cell metadata");
    expect_worksheet_failure("worksheet-reader-formula-metadata.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><f t="shared" si="0">A2</f></c></row></sheetData></worksheet>)",
        "formula metadata attribute");
    expect_worksheet_failure("worksheet-reader-shared-rel.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)",
        "without a workbook sharedStrings relationship", false, true);
    expect_worksheet_failure("worksheet-reader-style-rel.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" s="2"><v>1</v></c></row></sheetData></worksheet>)",
        "without a workbook styles relationship", true, false);
    expect_worksheet_failure("worksheet-reader-default-style-rel.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" s="0"><v>1</v></c></row></sheetData></worksheet>)",
        "without a workbook styles relationship", true, false);
    expect_worksheet_failure("worksheet-reader-shared-index.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s"><v>not-an-index</v></c></row></sheetData></worksheet>)",
        "invalid shared-string index");
    expect_worksheet_failure("worksheet-reader-missing-row-reference.xlsx",
        R"(<worksheet><sheetData><row><c r="A1"><v>1</v></c></row></sheetData></worksheet>)",
        "requires explicit row numbers");
    expect_worksheet_failure("worksheet-reader-missing-cell-reference.xlsx",
        R"(<worksheet><sheetData><row r="1"><c><v>1</v></c></row></sheetData></worksheet>)",
        "requires explicit cell references");
    expect_worksheet_failure("worksheet-reader-duplicate-cell.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c><c r="A1"><v>2</v></c></row></sheetData></worksheet>)",
        "out-of-order cells");
    expect_worksheet_failure("worksheet-reader-order.xlsx",
        R"(<worksheet><sheetData><row r="2"/><row r="1"/></sheetData></worksheet>)",
        "out-of-order rows");
    expect_worksheet_failure("worksheet-reader-attribute-separator.xlsx",
        R"(<worksheet><sheetData><row r="1"customHeight="1"/></sheetData></worksheet>)",
        "whitespace between attributes");
    expect_worksheet_failure("worksheet-reader-formula-order.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>3</v><f>SUM(1,2)</f></c></row></sheetData></worksheet>)",
        "formula markup after its cached value");
    expect_worksheet_failure("worksheet-reader-inline-boundary.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>text</is></t></c></row></sheetData></worksheet>)",
        "nested markup inside a cell value");
    expect_worksheet_failure("worksheet-reader-malformed.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</f></c></row></sheetData></worksheet>)",
        "mismatched cell value boundary");
    expect_worksheet_failure("worksheet-reader-entity.xlsx",
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>&unknown;</t></is></c></row></sheetData></worksheet>)",
        "unknown XML entity reference");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reader_reads_production_deflate_writer_output()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("worksheet-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row(
        {fastxlsx::CellView::number(7.0), fastxlsx::CellView::text("deflate")});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<CopiedCell> cells;
    fastxlsx::WorksheetReadCallbacks callbacks;
    callbacks.on_cell = [&](const fastxlsx::WorksheetCellView& cell) {
        cells.push_back(copy_cell(cell));
    };
    const fastxlsx::WorksheetReadSummary summary = reader.read_worksheet("Data", callbacks);
    check(summary.row_count == 1 && summary.cell_count == 2,
        "DEFLATE worksheet traversal count mismatch");
    check(cells[0].value_kind == fastxlsx::WorksheetCellValueKind::Number
            && cells[0].number_value == 7.0,
        "DEFLATE numeric projection mismatch");
    check(cells[1].value_kind == fastxlsx::WorksheetCellValueKind::Text
            && cells[1].text_value == "deflate",
        "DEFLATE inline text projection mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_reader_projects_supported_cells_in_source_order();
        test_callback_failure_releases_entry_and_reader_can_retry();
        test_reader_projects_formula_without_cached_value_and_empty_text();
        test_reader_move_transfers_open_state();
        test_reader_enforces_memory_guardrails();
        test_reader_rejects_unsupported_or_malformed_projection_shapes();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reader_reads_production_deflate_writer_output();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
