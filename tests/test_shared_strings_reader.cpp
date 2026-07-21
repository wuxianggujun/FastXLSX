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
    bool failed = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = std::string_view(error.what()).find(expected_text) != std::string_view::npos;
    }
    check(failed, "expected FastXlsxError diagnostic was not observed");
}

struct FixtureOptions {
    bool include_relationship = true;
    bool external_relationship = false;
    bool duplicate_relationship = false;
    std::string relationship_target = "sharedStrings.xml";
    bool include_part = true;
    std::string part_path = "xl/sharedStrings.xml";
    std::string content_type =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml";
};

std::map<std::string, std::string> workbook_entries(
    std::string shared_strings_xml, const FixtureOptions& options = {})
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (options.include_part) {
        content_types += R"(<Override PartName="/)" + options.part_path
            + R"(" ContentType=")" + options.content_type + R"("/>)";
    }
    content_types += "</Types>";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (options.include_relationship) {
        workbook_relationships +=
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target=")"
            + options.relationship_target + "\"";
        if (options.external_relationship) {
            workbook_relationships += R"( TargetMode="External")";
        }
        workbook_relationships += "/>";
        if (options.duplicate_relationship) {
            workbook_relationships +=
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)";
        }
    }
    workbook_relationships += "</Relationships>";

    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData/></worksheet>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/worksheets/sheet1.xml", worksheet);
    if (options.include_part) {
        fastxlsx::test::insert_zip_entry(
            entries, options.part_path, std::move(shared_strings_xml));
    }
    return entries;
}

std::filesystem::path write_fixture(std::string_view name,
    std::string shared_strings_xml,
    const FixtureOptions& options = {})
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(
        path, workbook_entries(std::move(shared_strings_xml), options));
    return path;
}

void test_reader_projects_simple_items_in_index_order()
{
    const std::filesystem::path path = write_fixture(
        "shared-strings-reader-representative.xlsx",
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4">)"
        R"(<si><t>zero</t></si>)"
        R"(<si><t xml:space="preserve"> one &amp; &#x2603; </t></si>)"
        R"(<si><t/></si>)"
        R"(<si><t>three</t></si>)"
        R"(</sst>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<std::uint32_t> indexes;
    std::vector<std::string> values;
    fastxlsx::SharedStringReadCallbacks callbacks;
    callbacks.on_item = [&](const fastxlsx::SharedStringItemView& item) {
        indexes.push_back(item.index);
        values.emplace_back(item.text);
    };
    const fastxlsx::SharedStringReadSummary summary =
        reader.read_shared_strings(callbacks);

    check(indexes == std::vector<std::uint32_t>({0, 1, 2, 3}),
        "shared string index order mismatch");
    check(values == std::vector<std::string>({"zero",
                        std::string(" one & ") + "\xe2\x98\x83" + " ", "", "three"}),
        "shared string decoded text mismatch");
    check(summary.item_count == 4, "shared string item count mismatch");
    check(summary.peak_item_text_bytes == values[1].size(),
        "shared string peak item text mismatch");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_releases_entry_and_reader_can_retry()
{
    const std::filesystem::path path = write_fixture(
        "shared-strings-reader-callback-retry.xlsx",
        R"(<sst><si><t>first</t></si><si><t>second</t></si></sst>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::SharedStringReadCallbacks callbacks;
    callbacks.on_item = [](const fastxlsx::SharedStringItemView&) {
        throw CallbackFailure("caller stopped shared string traversal");
    };
    bool saw_exact_failure = false;
    try {
        (void)reader.read_shared_strings(callbacks);
    } catch (const CallbackFailure& error) {
        saw_exact_failure = std::string_view(error.what())
            == "caller stopped shared string traversal";
    }
    check(saw_exact_failure, "shared string callback exception should propagate unchanged");

    const fastxlsx::SharedStringReadSummary retry = reader.read_shared_strings();
    check(retry.item_count == 2,
        "shared string traversal should restart after callback failure");
}

void test_reader_move_transfers_shared_strings_state()
{
    const std::filesystem::path path = write_fixture(
        "shared-strings-reader-move.xlsx", R"(<sst><si><t>value</t></si></sst>)");
    fastxlsx::WorkbookReader source = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorkbookReader moved = std::move(source);

    expect_fastxlsx_error(
        [&] { (void)source.read_shared_strings(); }, "not open");
    check(moved.read_shared_strings().item_count == 1,
        "moved reader should retain sharedStrings traversal state");
}

void test_reader_enforces_memory_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "shared-strings-reader-guardrails.xlsx",
        R"(<sst><si><t>abcdefghijkl</t></si></sst>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::SharedStringReaderOptions item_options;
    item_options.max_xml_window_bytes = 256;
    item_options.max_item_text_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_shared_strings({}, item_options); },
        "max_item_text_bytes");

    fastxlsx::SharedStringReaderOptions window_options;
    window_options.max_xml_window_bytes = 8;
    window_options.max_item_text_bytes = 256;
    expect_fastxlsx_error(
        [&] { (void)reader.read_shared_strings({}, window_options); },
        "bounded input window");

    fastxlsx::SharedStringReaderOptions zero_options;
    zero_options.max_xml_window_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_shared_strings({}, zero_options); },
        "nonzero max_xml_window_bytes");

    zero_options.max_xml_window_bytes = 256;
    zero_options.max_item_text_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_shared_strings({}, zero_options); },
        "nonzero max_item_text_bytes");
}

void test_reader_handles_package_chunk_boundaries()
{
    std::string expected((1024U * 1024U) + 31U, 'x');
    const std::filesystem::path path = write_fixture(
        "shared-strings-reader-chunk-boundary.xlsx",
        "<sst><si><t>" + expected + "</t></si></sst>");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::SharedStringReaderOptions options;
    options.max_xml_window_bytes = 2U * 1024U * 1024U;
    options.max_item_text_bytes = 2U * 1024U * 1024U;
    std::size_t observed_size = 0;
    fastxlsx::SharedStringReadCallbacks callbacks;
    callbacks.on_item = [&](const fastxlsx::SharedStringItemView& item) {
        observed_size = item.text.size();
        check(item.text == expected,
            "shared string text crossing a package chunk boundary mismatch");
    };
    const fastxlsx::SharedStringReadSummary summary =
        reader.read_shared_strings(callbacks, options);
    check(summary.item_count == 1 && observed_size == expected.size(),
        "shared string package chunk boundary summary mismatch");
    check(summary.peak_item_text_bytes == expected.size(),
        "shared string package chunk boundary peak mismatch");
}

void test_reader_rejects_unsupported_or_malformed_item_shapes()
{
    const auto expect_failure = [](std::string_view file_name,
                                    std::string xml,
                                    std::string_view diagnostic) {
        const std::filesystem::path path =
            write_fixture(file_name, std::move(xml));
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
        expect_fastxlsx_error(
            [&] { (void)reader.read_shared_strings(); }, diagnostic);
    };

    expect_failure("shared-strings-reader-rich.xlsx",
        R"(<sst><si><r><t>rich</t></r></si></sst>)", "rich text runs");
    expect_failure("shared-strings-reader-phonetic.xlsx",
        R"(<sst><si><t>text</t><rPh sb="0" eb="1"><t>p</t></rPh></si></sst>)",
        "phonetic metadata");
    expect_failure("shared-strings-reader-extension.xlsx",
        R"(<sst><si><t>text</t></si><extLst><ext uri="x"/></extLst></sst>)",
        "extension metadata");
    expect_failure("shared-strings-reader-duplicate-text.xlsx",
        R"(<sst><si><t>one</t><t>two</t></si></sst>)",
        "exactly one simple text element");
    expect_failure("shared-strings-reader-missing-text.xlsx",
        R"(<sst><si></si></sst>)", "exactly one simple text element");
    expect_failure("shared-strings-reader-nested-text.xlsx",
        R"(<sst><si><t>one<foo/></t></si></sst>)", "nested markup");
    expect_failure("shared-strings-reader-mismatch.xlsx",
        R"(<sst><si><t>one</si></t></sst>)", "mismatched text boundary");
    expect_failure("shared-strings-reader-comment.xlsx",
        R"(<sst><si><!-- metadata --><t>one</t></si></sst>)",
        "unsupported markup inside an item");
    expect_failure("shared-strings-reader-entity.xlsx",
        R"(<sst><si><t>&unknown;</t></si></sst>)", "unknown XML entity");
    expect_failure("shared-strings-reader-root-attribute.xlsx",
        R"(<sst uniqueCount="not-a-count"><si><t>one</t></si></sst>)",
        "invalid uniqueCount attribute");
}

void test_reader_audits_relationship_target_and_content_type()
{
    FixtureOptions missing_relationship;
    missing_relationship.include_relationship = false;
    const std::filesystem::path no_relationship = write_fixture(
        "shared-strings-reader-no-relationship.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", missing_relationship);
    const fastxlsx::WorkbookReader no_relationship_reader =
        fastxlsx::WorkbookReader::open(no_relationship);
    expect_fastxlsx_error(
        [&] { (void)no_relationship_reader.read_shared_strings(); },
        "no sharedStrings relationship");

    FixtureOptions missing_target;
    missing_target.relationship_target = "missing.xml";
    const std::filesystem::path unknown_part = write_fixture(
        "shared-strings-reader-missing-target.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", missing_target);
    const fastxlsx::WorkbookReader unknown_part_reader =
        fastxlsx::WorkbookReader::open(unknown_part);
    expect_fastxlsx_error(
        [&] { (void)unknown_part_reader.read_shared_strings(); }, "unknown part");

    FixtureOptions wrong_content_type;
    wrong_content_type.content_type = "application/xml";
    const std::filesystem::path wrong_type = write_fixture(
        "shared-strings-reader-wrong-content-type.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", wrong_content_type);
    const fastxlsx::WorkbookReader wrong_type_reader =
        fastxlsx::WorkbookReader::open(wrong_type);
    expect_fastxlsx_error(
        [&] { (void)wrong_type_reader.read_shared_strings(); }, "wrong content type");

    FixtureOptions encoded_target;
    encoded_target.relationship_target = "shared%53trings.xml";
    const std::filesystem::path encoded = write_fixture(
        "shared-strings-reader-encoded-target.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", encoded_target);
    const fastxlsx::WorkbookReader encoded_reader = fastxlsx::WorkbookReader::open(encoded);
    check(encoded_reader.read_shared_strings().item_count == 1,
        "percent-encoded sharedStrings relationship target should resolve");

    FixtureOptions invalid_percent;
    invalid_percent.relationship_target = "sharedStrings%ZZ.xml";
    const std::filesystem::path invalid_percent_path = write_fixture(
        "shared-strings-reader-invalid-percent-target.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", invalid_percent);
    const fastxlsx::WorkbookReader invalid_percent_reader =
        fastxlsx::WorkbookReader::open(invalid_percent_path);
    expect_fastxlsx_error(
        [&] { (void)invalid_percent_reader.read_shared_strings(); },
        "invalid percent escape");

    FixtureOptions qualified_target;
    qualified_target.relationship_target = "sharedStrings.xml?version=1";
    const std::filesystem::path qualified_path = write_fixture(
        "shared-strings-reader-qualified-target.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", qualified_target);
    const fastxlsx::WorkbookReader qualified_reader =
        fastxlsx::WorkbookReader::open(qualified_path);
    expect_fastxlsx_error(
        [&] { (void)qualified_reader.read_shared_strings(); },
        "must be a package part");

    FixtureOptions external;
    external.external_relationship = true;
    const std::filesystem::path external_path = write_fixture(
        "shared-strings-reader-external-relationship.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", external);
    expect_fastxlsx_error(
        [&] { (void)fastxlsx::WorkbookReader::open(external_path); },
        "internal workbook relationships");

    FixtureOptions duplicate;
    duplicate.duplicate_relationship = true;
    const std::filesystem::path duplicate_path = write_fixture(
        "shared-strings-reader-duplicate-relationship.xlsx",
        R"(<sst><si><t>one</t></si></sst>)", duplicate);
    expect_fastxlsx_error(
        [&] { (void)fastxlsx::WorkbookReader::open(duplicate_path); },
        "duplicate workbook relationship type");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reader_reads_production_deflate_writer_output()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("shared-strings-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("first"),
        fastxlsx::CellView::text("second"), fastxlsx::CellView::text("first")});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<std::string> values;
    fastxlsx::SharedStringReadCallbacks callbacks;
    callbacks.on_item = [&](const fastxlsx::SharedStringItemView& item) {
        values.emplace_back(item.text);
    };
    const fastxlsx::SharedStringReadSummary summary =
        reader.read_shared_strings(callbacks);
    check(summary.item_count == 2,
        "DEFLATE sharedStrings unique item count mismatch");
    check(values == std::vector<std::string>({"first", "second"}),
        "DEFLATE sharedStrings projection mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_reader_projects_simple_items_in_index_order();
        test_callback_failure_releases_entry_and_reader_can_retry();
        test_reader_move_transfers_shared_strings_state();
        test_reader_enforces_memory_guardrails();
        test_reader_handles_package_chunk_boundaries();
        test_reader_rejects_unsupported_or_malformed_item_shapes();
        test_reader_audits_relationship_target_and_content_type();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reader_reads_production_deflate_writer_output();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
