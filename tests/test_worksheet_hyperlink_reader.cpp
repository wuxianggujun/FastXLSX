#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
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
        matched = std::string_view(error.what()).find(expected_text)
            != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: "
            + std::string(expected_text));
    }
}

std::map<std::string, std::string> workbook_entries(
    std::string worksheet_xml,
    std::optional<std::string> worksheet_relationships = std::nullopt)
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
    if (worksheet_relationships.has_value()) {
        fastxlsx::test::insert_zip_entry(entries,
            "xl/worksheets/_rels/sheet1.xml.rels",
            std::move(*worksheet_relationships));
    }
    return entries;
}

std::filesystem::path write_fixture(std::string_view name,
    std::string worksheet_xml,
    std::optional<std::string> worksheet_relationships = std::nullopt)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(path,
        workbook_entries(
            std::move(worksheet_xml), std::move(worksheet_relationships)));
    return path;
}

std::string representative_worksheet_xml()
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<x:sheetData/>)"
        R"(<x:dataValidations/>)"
        R"(<x:hyperlinks>)"
        R"(<x:hyperlink ref="$A$1" location="&apos;Other &amp; QA&apos;!A1" display="Go &amp; &quot;read&quot;" tooltip="Tip &apos;internal&apos;"/>)"
        R"(<x:hyperlink ref="C2:D3" rel:id="rId7" display="Web &lt;link&gt;"></x:hyperlink>)"
        R"(</x:hyperlinks>)"
        R"(<x:printOptions/>)"
        R"(</x:worksheet>)";
}

std::string representative_relationships()
{
    return R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId7" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/?a=1&amp;b=2" TargetMode="External"/>)"
        R"(<Relationship Id="unused" Type="urn:example:unused" Target="../unused.xml"/>)"
        R"(</Relationships>)";
}

void test_projects_owning_internal_and_external_links()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-hyperlink-reader-representative.xlsx",
        representative_worksheet_xml(), representative_relationships());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetHyperlinkView> values;
    fastxlsx::WorksheetHyperlinkReadCallbacks callbacks;
    callbacks.on_hyperlink = [&](const fastxlsx::WorksheetHyperlinkView& value) {
        values.push_back(value);
    };
    const fastxlsx::WorksheetHyperlinkReadSummary summary =
        reader.read_worksheet_hyperlinks("Data", callbacks);

    check(values.size() == 2 && values[0].index == 0 && values[1].index == 1,
        "hyperlink source indexes are not zero-based");
    check(values[0].kind == fastxlsx::WorksheetHyperlinkKind::Internal
            && values[0].range.first_row == 1
            && values[0].range.first_column == 1
            && values[0].range.last_row == 1
            && values[0].range.last_column == 1
            && values[0].location == "'Other & QA'!A1"
            && values[0].external_target.empty()
            && values[0].options.display == "Go & \"read\""
            && values[0].options.tooltip == "Tip 'internal'",
        "internal hyperlink owning projection mismatch");
    check(values[1].kind == fastxlsx::WorksheetHyperlinkKind::External
            && values[1].range.first_row == 2
            && values[1].range.first_column == 3
            && values[1].range.last_row == 3
            && values[1].range.last_column == 4
            && values[1].location.empty()
            && values[1].external_target == "https://example.invalid/?a=1&b=2"
            && values[1].options.display == "Web <link>",
        "external hyperlink relationship projection mismatch");
    check(summary.hyperlink_count == 2
            && summary.internal_hyperlink_count == 1
            && summary.external_hyperlink_count == 1
            && summary.peak_reference_bytes >= 5
            && summary.peak_relationship_id_bytes == 4
            && summary.peak_target_text_bytes == 32
            && summary.peak_metadata_text_bytes >= 14
            && summary.peak_retained_range_count == 2
            && summary.peak_xml_nesting_depth >= 2,
        "hyperlink summary telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "hyperlink reader changed the source package");
}

void test_internal_link_does_not_require_relationships()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-hyperlink-reader-internal-no-rels.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1:B2" location="Other!A1"/></hyperlinks></worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::size_t count = 0;
    fastxlsx::WorksheetHyperlinkReadCallbacks callbacks;
    callbacks.on_hyperlink = [&](const fastxlsx::WorksheetHyperlinkView& value) {
        ++count;
        check(value.kind == fastxlsx::WorksheetHyperlinkKind::Internal
                && value.range.last_row == 2 && value.range.last_column == 2,
            "internal range projection without .rels mismatch");
    };
    const auto summary = reader.read_worksheet_hyperlinks("Data", callbacks);
    check(count == 1 && summary.internal_hyperlink_count == 1,
        "internal hyperlink should not require worksheet relationships");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-hyperlink-reader-retry.xlsx",
        representative_worksheet_xml(), representative_relationships());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetHyperlinkReadCallbacks throwing;
    throwing.on_hyperlink = [](const fastxlsx::WorksheetHyperlinkView&) {
        throw CallbackFailure("hyperlink callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_hyperlinks("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "hyperlink callback stopped traversal";
    }
    check(saw_exact_exception,
        "hyperlink callback exception should propagate unchanged");

    std::size_t count = 0;
    fastxlsx::WorksheetHyperlinkReadCallbacks retry;
    retry.on_hyperlink =
        [&](const fastxlsx::WorksheetHyperlinkView&) { ++count; };
    const auto summary = reader.read_worksheet_hyperlinks("Data", retry);
    check(count == 2 && summary.hyperlink_count == 2,
        "hyperlink reader should retry after callback failure");
}

void test_absent_and_empty_container_are_clean()
{
    const std::filesystem::path absent = write_fixture(
        "worksheet-hyperlink-reader-absent.xlsx",
        R"(<worksheet><sheetData/></worksheet>)");
    const fastxlsx::WorkbookReader absent_reader =
        fastxlsx::WorkbookReader::open(absent);
    check(absent_reader.read_worksheet_hyperlinks("Data").hyperlink_count == 0,
        "absent hyperlinks should be a clean empty result");

    const std::filesystem::path empty = write_fixture(
        "worksheet-hyperlink-reader-empty.xlsx",
        R"(<worksheet><sheetData/><hyperlinks/></worksheet>)");
    const fastxlsx::WorkbookReader empty_reader =
        fastxlsx::WorkbookReader::open(empty);
    check(empty_reader.read_worksheet_hyperlinks("Data").hyperlink_count == 0,
        "self-closing hyperlinks should be a clean empty result");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-hyperlink-reader-guardrails.xlsx",
        representative_worksheet_xml(), representative_relationships());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetHyperlinkReaderOptions options;
    options.max_reference_bytes = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_reference_bytes");
    options = {};
    options.max_relationship_id_bytes = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_relationship_id_bytes");
    options = {};
    options.max_target_text_bytes = 4;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_target_text_bytes");
    options = {};
    options.max_target_text_bytes = 20;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "external hyperlink target exceeds");
    options = {};
    options.max_metadata_text_bytes = 3;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_metadata_text_bytes");
    options = {};
    options.max_hyperlink_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_hyperlink_count");
    options = {};
    options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "bounded input window");
    options = {};
    options.max_target_text_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data", {}, options); },
        "nonzero max_target_text_bytes");
}

void expect_hyperlink_failure(std::string_view name, std::string xml,
    std::string_view diagnostic,
    std::optional<std::string> relationships = std::nullopt)
{
    const std::filesystem::path path = write_fixture(
        name, std::move(xml), std::move(relationships));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_hyperlinks("Data"); }, diagnostic);
}

void test_relationship_failures()
{
    const std::string external_xml =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData/><hyperlinks><hyperlink ref="A1" r:id="rId1"/></hyperlinks></worksheet>)";
    expect_hyperlink_failure("worksheet-hyperlink-reader-missing-rels.xlsx",
        external_xml, "requires worksheet relationships");
    expect_hyperlink_failure("worksheet-hyperlink-reader-missing-id.xlsx",
        external_xml, "relationship id is missing",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="other" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid" TargetMode="External"/></Relationships>)");
    expect_hyperlink_failure("worksheet-hyperlink-reader-wrong-type.xlsx",
        external_xml, "wrong type",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="urn:not-hyperlink" Target="https://example.invalid" TargetMode="External"/></Relationships>)");
    expect_hyperlink_failure("worksheet-hyperlink-reader-internal-mode.xlsx",
        external_xml, "TargetMode=External",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="relative/item.xml"/></Relationships>)");
}

void test_rejects_unsupported_and_malformed_shapes()
{
    expect_hyperlink_failure("worksheet-hyperlink-reader-missing-ref.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink location="Other!A1"/></hyperlinks></worksheet>)",
        "requires ref");
    expect_hyperlink_failure("worksheet-hyperlink-reader-invalid-ref.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A0" location="Other!A1"/></hyperlinks></worksheet>)",
        "valid A1");
    expect_hyperlink_failure("worksheet-hyperlink-reader-missing-target.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1"/></hyperlinks></worksheet>)",
        "exactly one");
    expect_hyperlink_failure("worksheet-hyperlink-reader-mixed-target.xlsx",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData/><hyperlinks><hyperlink ref="A1" location="Other!A1" r:id="rId1"/></hyperlinks></worksheet>)",
        "exactly one");
    expect_hyperlink_failure("worksheet-hyperlink-reader-empty-location.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1" location=""/></hyperlinks></worksheet>)",
        "location cannot be empty");
    expect_hyperlink_failure("worksheet-hyperlink-reader-container-attr.xlsx",
        R"(<worksheet><sheetData/><hyperlinks count="1"><hyperlink ref="A1" location="Other!A1"/></hyperlinks></worksheet>)",
        "hyperlinks has an unsupported attribute");
    expect_hyperlink_failure("worksheet-hyperlink-reader-link-attr.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1" location="Other!A1" history="1"/></hyperlinks></worksheet>)",
        "hyperlink has an unsupported attribute");
    expect_hyperlink_failure("worksheet-hyperlink-reader-child.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1" location="Other!A1"><ext/></hyperlink></hyperlinks></worksheet>)",
        "unsupported child");
    expect_hyperlink_failure("worksheet-hyperlink-reader-duplicate-container.xlsx",
        R"(<worksheet><sheetData/><hyperlinks/><hyperlinks/></worksheet>)",
        "duplicate or nested");
    expect_hyperlink_failure("worksheet-hyperlink-reader-overlap.xlsx",
        R"(<worksheet><sheetData/><hyperlinks><hyperlink ref="A1:B2" location="One!A1"/><hyperlink ref="B2:C3" location="Two!A1"/></hyperlinks></worksheet>)",
        "overlap or duplicate");
    expect_hyperlink_failure("worksheet-hyperlink-reader-schema.xlsx",
        R"(<worksheet><sheetData/><hyperlinks/><dataValidations/></worksheet>)",
        "schema order");
    expect_hyperlink_failure("worksheet-hyperlink-reader-qname.xlsx",
        R"(<x:worksheet xmlns:x="urn:main"><x:sheetData/><x:hyperlinks><x:hyperlink ref="A1" location="Other!A1"/></y:hyperlinks></x:worksheet>)",
        "QName");
    expect_hyperlink_failure("worksheet-hyperlink-reader-id-namespace.xlsx",
        R"(<worksheet xmlns:r="urn:not-relationships"><sheetData/><hyperlinks><hyperlink ref="A1" r:id="rId1"/></hyperlinks></worksheet>)",
        "relationship id namespace");
    expect_hyperlink_failure("worksheet-hyperlink-reader-duplicate-id.xlsx",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData/><hyperlinks><hyperlink ref="A1" r:id="rId1" rel:id="rId1"/></hyperlinks></worksheet>)",
        "duplicate relationship id attributes",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid" TargetMode="External"/></Relationships>)");
}

void test_foreign_extension_names_do_not_alias_hyperlinks()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-hyperlink-reader-foreign-extension.xlsx",
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:z="urn:test"><x:sheetData/><x:hyperlinks><x:hyperlink ref="A1" location="Other!A1"/></x:hyperlinks><x:extLst><x:ext uri="test"><z:hyperlinks><z:hyperlink ref="A1" location="Ignored!A1"/></z:hyperlinks></x:ext></x:extLst></x:worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::size_t count = 0;
    fastxlsx::WorksheetHyperlinkReadCallbacks callbacks;
    callbacks.on_hyperlink =
        [&](const fastxlsx::WorksheetHyperlinkView&) { ++count; };
    const auto summary = reader.read_worksheet_hyperlinks("Data", callbacks);
    check(count == 1 && summary.hyperlink_count == 1,
        "foreign extension local names must not alias worksheet hyperlinks");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_hyperlinks()
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(
        "worksheet-hyperlink-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("deflate")});
    sheet.add_external_hyperlink(
        1, 1, "https://example.invalid/deflate");
    sheet.add_internal_hyperlink(1, 2, "Data!A1");
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetHyperlinkKind> kinds;
    fastxlsx::WorksheetHyperlinkReadCallbacks callbacks;
    callbacks.on_hyperlink = [&](const fastxlsx::WorksheetHyperlinkView& value) {
        kinds.push_back(value.kind);
    };
    const auto summary = reader.read_worksheet_hyperlinks("Data", callbacks);
    check(kinds.size() == 2
            && kinds[0] == fastxlsx::WorksheetHyperlinkKind::External
            && kinds[1] == fastxlsx::WorksheetHyperlinkKind::Internal
            && summary.external_hyperlink_count == 1
            && summary.internal_hyperlink_count == 1,
        "DEFLATE hyperlink traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_owning_internal_and_external_links();
        test_internal_link_does_not_require_relationships();
        test_callback_failure_allows_retry();
        test_absent_and_empty_container_are_clean();
        test_guardrails();
        test_relationship_failures();
        test_rejects_unsupported_and_malformed_shapes();
        test_foreign_extension_names_do_not_alias_hyperlinks();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_hyperlinks();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "All worksheet hyperlink reader tests passed\n";
    return 0;
}
