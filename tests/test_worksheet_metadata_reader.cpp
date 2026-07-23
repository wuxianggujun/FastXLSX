#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

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
    bool matched = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        matched = std::string_view(error.what()).find(expected_text) != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: " + std::string(expected_text));
    }
}

std::map<std::string, std::string> workbook_entries(std::string worksheet_xml)
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
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
    return entries;
}

std::filesystem::path write_fixture(std::string_view name, std::string worksheet_xml)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(
        path, workbook_entries(std::move(worksheet_xml)));
    return path;
}

std::string representative_worksheet_xml()
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<x:sheetViews>)"
        R"(<x:sheetView workbookViewId="1"><x:pane state="split" xSplit="1"/></x:sheetView>)"
        R"(<x:sheetView workbookViewId="0"><x:pane xSplit="2" ySplit="3" topLeftCell="C4" activePane="bottomRight" state="frozen"/><x:selection pane="bottomRight"/></x:sheetView>)"
        R"(</x:sheetViews>)"
        R"(<x:sheetData><x:row r="1"><x:c r="A1" t="inlineStr"><x:is><x:t>stored</x:t></x:is></x:c></x:row></x:sheetData>)"
        R"(<x:autoFilter ref="A1:D9"><x:filterColumn colId="0"/></x:autoFilter>)"
        R"(<x:mergeCells count="2"><x:mergeCell ref="A2:B2"/><x:mergeCell ref="C3:D4"></x:mergeCell></x:mergeCells>)"
        R"(</x:worksheet>)";
}

void test_projects_metadata_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-metadata-reader-representative.xlsx", representative_worksheet_xml());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<std::string> order;
    fastxlsx::WorksheetFrozenPaneView frozen;
    fastxlsx::WorksheetAutoFilterView filter;
    std::vector<fastxlsx::WorksheetMergedCellView> merged;
    fastxlsx::WorksheetMetadataReadCallbacks callbacks;
    callbacks.on_frozen_pane = [&](const fastxlsx::WorksheetFrozenPaneView& value) {
        order.push_back("frozen");
        frozen = value;
    };
    callbacks.on_auto_filter = [&](const fastxlsx::WorksheetAutoFilterView& value) {
        order.push_back("filter");
        filter = value;
    };
    callbacks.on_merged_cell = [&](const fastxlsx::WorksheetMergedCellView& value) {
        order.push_back("merge" + std::to_string(value.index));
        merged.push_back(value);
    };

    const fastxlsx::WorksheetMetadataReadSummary summary =
        reader.read_worksheet_metadata("Data", callbacks);
    check(order == std::vector<std::string>({"frozen", "filter", "merge0", "merge1"}),
        "worksheet metadata callback order mismatch");
    check(summary.frozen_pane_count == 1 && summary.auto_filter_count == 1
            && summary.merged_cell_count == 2,
        "worksheet metadata summary count mismatch");
    check(frozen.row_split == 3 && frozen.column_split == 2,
        "frozen pane projection mismatch");
    check(filter.range.first_row == 1 && filter.range.first_column == 1
            && filter.range.last_row == 9 && filter.range.last_column == 4,
        "auto-filter projection mismatch");
    check(merged.size() == 2 && merged[0].index == 0 && merged[1].index == 1
            && merged[0].range.first_row == 2 && merged[0].range.last_column == 2
            && merged[1].range.first_row == 3 && merged[1].range.last_column == 4,
        "merged-cell projection mismatch");
    check(summary.peak_sheet_view_count == 2
            && summary.peak_range_reference_bytes >= 5
            && summary.peak_xml_nesting_depth >= 2,
        "worksheet metadata guardrail telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "worksheet metadata reader changed the source package");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-metadata-reader-retry.xlsx", representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetMetadataReadCallbacks throwing;
    throwing.on_auto_filter = [](const fastxlsx::WorksheetAutoFilterView&) {
        throw CallbackFailure("metadata callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_metadata("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "metadata callback stopped traversal";
    }
    check(saw_exact_exception, "metadata callback exception should propagate unchanged");

    std::size_t merged_count = 0;
    fastxlsx::WorksheetMetadataReadCallbacks retry;
    retry.on_merged_cell = [&](const fastxlsx::WorksheetMergedCellView&) { ++merged_count; };
    const fastxlsx::WorksheetMetadataReadSummary summary =
        reader.read_worksheet_metadata("Data", retry);
    check(summary.merged_cell_count == 2 && merged_count == 2,
        "metadata reader should retry after callback failure");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-metadata-reader-guardrails.xlsx", representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetMetadataReaderOptions options;
    options.max_range_reference_bytes = 3;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "max_range_reference_bytes");

    options = {};
    options.max_sheet_view_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "max_sheet_view_count");

    options = {};
    options.max_merged_cell_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "max_merged_cell_count");

    options = {};
    options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "max_xml_nesting_depth");

    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "bounded input window");

    options = {};
    options.max_range_reference_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data", {}, options); },
        "nonzero max_range_reference_bytes");
}

void expect_metadata_failure(
    std::string_view name, std::string xml, std::string_view diagnostic)
{
    const std::filesystem::path path = write_fixture(name, std::move(xml));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_metadata("Data"); }, diagnostic);
}

void test_rejects_unsupported_and_malformed_shapes()
{
    expect_metadata_failure("worksheet-metadata-duplicate-filter.xlsx",
        R"(<worksheet><sheetData/><autoFilter ref="A1"/><autoFilter ref="B1"/></worksheet>)",
        "duplicate");
    expect_metadata_failure("worksheet-metadata-invalid-filter.xlsx",
        R"(<worksheet><sheetData/><autoFilter ref="A0:B2"/></worksheet>)",
        "valid A1 range");
    expect_metadata_failure("worksheet-metadata-invalid-pane.xlsx",
        R"(<worksheet><sheetViews><sheetView workbookViewId="0"><pane state="split" xSplit="1"/></sheetView></sheetViews><sheetData/></worksheet>)",
        "supported frozen pane");
    expect_metadata_failure("worksheet-metadata-pivot-pane.xlsx",
        R"(<worksheet><sheetViews><sheetView workbookViewId="0"><pivotSelection/></sheetView></sheetViews><sheetData/></worksheet>)",
        "pivotSelection");
    expect_metadata_failure("worksheet-metadata-duplicate-view.xlsx",
        R"(<worksheet><sheetViews><sheetView workbookViewId="0"/><sheetView workbookViewId="0"/></sheetViews><sheetData/></worksheet>)",
        "duplicate sheetView");
    expect_metadata_failure("worksheet-metadata-merge-count.xlsx",
        R"(<worksheet><sheetData/><mergeCells count="2"><mergeCell ref="A1:B1"/></mergeCells></worksheet>)",
        "count does not match");
    expect_metadata_failure("worksheet-metadata-merge-single.xlsx",
        R"(<worksheet><sheetData/><mergeCells><mergeCell ref="A1"/></mergeCells></worksheet>)",
        "multi-cell");
    expect_metadata_failure("worksheet-metadata-merge-overlap.xlsx",
        R"(<worksheet><sheetData/><mergeCells><mergeCell ref="A1:B2"/><mergeCell ref="B2:C3"/></mergeCells></worksheet>)",
        "overlap");
    expect_metadata_failure("worksheet-metadata-qname.xlsx",
        R"(<worksheet><sheetData/><mergeCells><mergeCell ref="A1:B1"></y:mergeCell></mergeCells></worksheet>)",
        "QName");
    expect_metadata_failure("worksheet-metadata-schema-order.xlsx",
        R"(<worksheet><sheetData/><mergeCells><mergeCell ref="A1:B1"/></mergeCells><autoFilter ref="A1"/></worksheet>)",
        "schema order");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_metadata()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("worksheet-metadata-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.freeze_panes(2, 1);
    sheet.set_auto_filter(fastxlsx::CellRange {1, 1, 5, 3});
    sheet.merge_cells(fastxlsx::CellRange {2, 1, 2, 2});
    sheet.append_row({fastxlsx::CellView::text("deflate")});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorksheetMetadataReadCallbacks callbacks;
    std::size_t frozen = 0;
    std::size_t filters = 0;
    std::size_t merged = 0;
    callbacks.on_frozen_pane = [&](const fastxlsx::WorksheetFrozenPaneView& value) {
        ++frozen;
        check(value.row_split == 2 && value.column_split == 1,
            "DEFLATE frozen pane projection mismatch");
    };
    callbacks.on_auto_filter = [&](const fastxlsx::WorksheetAutoFilterView& value) {
        ++filters;
        check(value.range.last_row == 5 && value.range.last_column == 3,
            "DEFLATE auto-filter projection mismatch");
    };
    callbacks.on_merged_cell = [&](const fastxlsx::WorksheetMergedCellView&) { ++merged; };
    const fastxlsx::WorksheetMetadataReadSummary summary =
        reader.read_worksheet_metadata("Data", callbacks);
    check(frozen == 1 && filters == 1 && merged == 1
            && summary.merged_cell_count == 1,
        "DEFLATE metadata traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_metadata_in_source_order();
        test_callback_failure_allows_retry();
        test_guardrails();
        test_rejects_unsupported_and_malformed_shapes();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_metadata();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
