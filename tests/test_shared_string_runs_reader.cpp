#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <cstdint>
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
    std::string actual = "no exception";
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        actual = error.what();
        if (std::string_view(actual).find(expected_text) != std::string_view::npos) {
            return;
        }
    }
    throw TestFailure("expected FastXlsxError containing '" + std::string(expected_text)
        + "', observed '" + actual + "'");
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

struct ObservedRun {
    std::uint32_t item_index = 0;
    std::uint32_t run_index = 0;
    fastxlsx::SharedStringRunKind kind = fastxlsx::SharedStringRunKind::SimpleText;
    std::string text;
    fastxlsx::SharedStringRunFormat format;
};

void test_reader_projects_simple_and_rich_runs_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "shared-string-runs-representative.xlsx",
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="3">)"
        R"(<si><t>simple &amp; &#x2603;</t></si>)"
        R"(<si><r><rPr><rFont val="Calibri"/><sz val="11.0"/><family val="2"/><scheme val="minor"/><color theme="1"/><b/><i val="0"/></rPr><t xml:space="preserve"> rich </t></r>)"
        R"(<r><rPr><i/><color rgb="FF00AA11"/></rPr><t>green</t></r></si>)"
        R"(<si><t/></si>)"
        R"(</sst>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<std::string> events;
    std::vector<ObservedRun> runs;
    fastxlsx::SharedStringRunReadCallbacks callbacks;
    callbacks.on_item_start = [&](const fastxlsx::SharedStringRunItemView& item) {
        events.push_back("start:" + std::to_string(item.index));
    };
    callbacks.on_run = [&](const fastxlsx::SharedStringRunView& run) {
        events.push_back("run:" + std::to_string(run.item_index) + ":"
            + std::to_string(run.run_index));
        runs.push_back({run.item_index, run.run_index, run.kind,
            std::string(run.text), run.format});
    };
    callbacks.on_item_end = [&](const fastxlsx::SharedStringRunItemView& item) {
        events.push_back("end:" + std::to_string(item.index));
    };

    const fastxlsx::SharedStringRunReadSummary summary =
        reader.read_shared_string_runs(callbacks);

    check(events == std::vector<std::string>({"start:0", "run:0:0", "end:0",
                        "start:1", "run:1:0", "run:1:1", "end:1", "start:2",
                        "run:2:0", "end:2"}),
        "shared string run callback order mismatch");
    check(runs.size() == 4, "shared string run count mismatch");
    check(runs[0].kind == fastxlsx::SharedStringRunKind::SimpleText
            && runs[0].text == std::string("simple & ") + "\xe2\x98\x83"
            && !runs[0].format.bold && !runs[0].format.italic
            && !runs[0].format.direct_argb_color.has_value(),
        "simple shared string run projection mismatch");
    check(runs[1].kind == fastxlsx::SharedStringRunKind::RichText
            && runs[1].text == " rich " && runs[1].format.bold
            && !runs[1].format.italic
            && !runs[1].format.direct_argb_color.has_value(),
        "default-metadata rich run projection mismatch");
    check(runs[2].kind == fastxlsx::SharedStringRunKind::RichText
            && runs[2].text == "green" && !runs[2].format.bold
            && runs[2].format.italic
            && runs[2].format.direct_argb_color == 0xFF00AA11U,
        "formatted rich run projection mismatch");
    check(runs[3].kind == fastxlsx::SharedStringRunKind::SimpleText
            && runs[3].text.empty(),
        "empty simple run projection mismatch");
    check(summary.item_count == 3 && summary.run_count == 4,
        "shared string run summary count mismatch");
    check(summary.peak_runs_per_item == 2,
        "shared string run summary peak count mismatch");
    check(summary.peak_run_text_bytes == runs[0].text.size()
            && summary.peak_item_text_bytes == runs[0].text.size(),
        "shared string run text peak mismatch");
    check(summary.peak_xml_nesting_depth == 4,
        "shared string run nesting peak mismatch");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template <typename ConfigureCallbacks>
void expect_callback_failure_and_retry(
    const fastxlsx::WorkbookReader& reader, ConfigureCallbacks&& configure)
{
    fastxlsx::SharedStringRunReadCallbacks callbacks;
    configure(callbacks);
    bool saw_exact_failure = false;
    try {
        (void)reader.read_shared_string_runs(callbacks);
    } catch (const CallbackFailure& error) {
        saw_exact_failure = std::string_view(error.what()) == "caller stopped traversal";
    }
    check(saw_exact_failure,
        "shared string run callback exception should propagate unchanged");
    const fastxlsx::SharedStringRunReadSummary retry =
        reader.read_shared_string_runs();
    check(retry.item_count == 2 && retry.run_count == 3,
        "shared string run traversal should restart after callback failure");
}

void test_callback_failures_release_entry_and_reader_can_retry()
{
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(write_fixture(
        "shared-string-runs-callback-retry.xlsx",
        R"(<sst><si><t>first</t></si><si><r><t>a</t></r><r><t>b</t></r></si></sst>)"));

    expect_callback_failure_and_retry(reader,
        [](fastxlsx::SharedStringRunReadCallbacks& callbacks) {
            callbacks.on_item_start = [](const fastxlsx::SharedStringRunItemView&) {
                throw CallbackFailure("caller stopped traversal");
            };
        });
    expect_callback_failure_and_retry(reader,
        [](fastxlsx::SharedStringRunReadCallbacks& callbacks) {
            callbacks.on_run = [](const fastxlsx::SharedStringRunView&) {
                throw CallbackFailure("caller stopped traversal");
            };
        });
    expect_callback_failure_and_retry(reader,
        [](fastxlsx::SharedStringRunReadCallbacks& callbacks) {
            callbacks.on_item_end = [](const fastxlsx::SharedStringRunItemView&) {
                throw CallbackFailure("caller stopped traversal");
            };
        });
}

void test_reader_move_transfers_shared_string_run_state()
{
    const std::filesystem::path path = write_fixture(
        "shared-string-runs-move.xlsx", R"(<sst><si><t>value</t></si></sst>)");
    fastxlsx::WorkbookReader source = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorkbookReader moved = std::move(source);

    expect_fastxlsx_error(
        [&] { (void)source.read_shared_string_runs(); }, "not open");
    check(moved.read_shared_string_runs().run_count == 1,
        "moved reader should retain shared string run traversal state");
}

void test_reader_enforces_memory_and_count_guardrails()
{
    const fastxlsx::WorkbookReader item_reader = fastxlsx::WorkbookReader::open(
        write_fixture("shared-string-runs-item-limit.xlsx",
            R"(<sst><si><r><t>abc</t></r><r><t>def</t></r></si></sst>)"));

    fastxlsx::SharedStringRunReaderOptions options;
    options.max_item_text_bytes = 5;
    expect_fastxlsx_error(
        [&] { (void)item_reader.read_shared_string_runs({}, options); },
        "max_item_text_bytes");

    options = {};
    options.max_run_text_bytes = 2;
    expect_fastxlsx_error(
        [&] { (void)item_reader.read_shared_string_runs({}, options); },
        "max_run_text_bytes");

    options = {};
    options.max_runs_per_item = 1;
    expect_fastxlsx_error(
        [&] { (void)item_reader.read_shared_string_runs({}, options); },
        "max_runs_per_item");

    const fastxlsx::WorkbookReader depth_reader = fastxlsx::WorkbookReader::open(
        write_fixture("shared-string-runs-depth-limit.xlsx",
            R"(<sst><si><r><rPr><b></b></rPr><t>x</t></r></si></sst>)"));
    options = {};
    options.max_xml_nesting_depth = 4;
    expect_fastxlsx_error(
        [&] { (void)depth_reader.read_shared_string_runs({}, options); },
        "max_xml_nesting_depth");

    const fastxlsx::WorkbookReader window_reader = fastxlsx::WorkbookReader::open(
        write_fixture("shared-string-runs-window-limit.xlsx",
            R"(<sst><si><t xml:space="preserve">x</t></si></sst>)"));
    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)window_reader.read_shared_string_runs({}, options); },
        "bounded input window");

    const auto expect_zero_option = [&](auto set_zero, std::string_view diagnostic) {
        fastxlsx::SharedStringRunReaderOptions zero_options;
        set_zero(zero_options);
        expect_fastxlsx_error(
            [&] { (void)item_reader.read_shared_string_runs({}, zero_options); },
            diagnostic);
    };
    expect_zero_option([](auto& value) { value.max_xml_window_bytes = 0; },
        "nonzero max_xml_window_bytes");
    expect_zero_option([](auto& value) { value.max_item_text_bytes = 0; },
        "nonzero max_item_text_bytes");
    expect_zero_option([](auto& value) { value.max_run_text_bytes = 0; },
        "nonzero max_run_text_bytes");
    expect_zero_option([](auto& value) { value.max_runs_per_item = 0; },
        "nonzero max_runs_per_item");
    expect_zero_option([](auto& value) { value.max_xml_nesting_depth = 0; },
        "nonzero max_xml_nesting_depth");
}

void test_reader_handles_package_chunk_boundaries()
{
    const std::string expected((1024U * 1024U) + 31U, 'x');
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(
        write_fixture("shared-string-runs-chunk-boundary.xlsx",
            "<sst><si><r><t>" + expected + "</t></r></si></sst>"));

    fastxlsx::SharedStringRunReaderOptions options;
    options.max_xml_window_bytes = 2U * 1024U * 1024U;
    options.max_item_text_bytes = 2U * 1024U * 1024U;
    options.max_run_text_bytes = 2U * 1024U * 1024U;
    std::string copied;
    fastxlsx::SharedStringRunReadCallbacks callbacks;
    callbacks.on_run = [&](const fastxlsx::SharedStringRunView& run) {
        copied.assign(run.text);
    };
    const fastxlsx::SharedStringRunReadSummary summary =
        reader.read_shared_string_runs(callbacks, options);
    check(copied == expected && summary.peak_run_text_bytes == expected.size()
            && summary.peak_item_text_bytes == expected.size(),
        "shared string run package chunk boundary mismatch");
}

void test_reader_rejects_unsupported_or_malformed_run_shapes()
{
    const auto expect_failure = [](std::string_view file_name,
                                    std::string xml,
                                    std::string_view diagnostic) {
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(
            write_fixture(file_name, std::move(xml)));
        expect_fastxlsx_error(
            [&] { (void)reader.read_shared_string_runs(); }, diagnostic);
    };

    expect_failure("shared-string-runs-phonetic.xlsx",
        R"(<sst><si><t>x</t><rPh sb="0" eb="1"><t>p</t></rPh></si></sst>)",
        "phonetic metadata");
    expect_failure("shared-string-runs-extension.xlsx",
        R"(<sst><si><t>x</t></si><extLst><ext uri="x"/></extLst></sst>)",
        "extension metadata");
    expect_failure("shared-string-runs-simple-rich.xlsx",
        R"(<sst><si><t>x</t><r><t>y</t></r></si></sst>)", "mixed");
    expect_failure("shared-string-runs-rich-simple.xlsx",
        R"(<sst><si><r><t>x</t></r><t>y</t></si></sst>)", "mixed rich and simple");
    expect_failure("shared-string-runs-empty-item.xlsx",
        R"(<sst><si></si></sst>)", "text or rich runs");
    expect_failure("shared-string-runs-empty-si.xlsx",
        R"(<sst><si/></sst>)", "nonempty si");
    expect_failure("shared-string-runs-no-text.xlsx",
        R"(<sst><si><r><rPr><b/></rPr></r></si></sst>)", "exactly one text");
    expect_failure("shared-string-runs-duplicate-text.xlsx",
        R"(<sst><si><r><t>x</t><t>y</t></r></si></sst>)", "exactly one text");
    expect_failure("shared-string-runs-late-properties.xlsx",
        R"(<sst><si><r><t>x</t><rPr><b/></rPr></r></si></sst>)",
        "misplaced run properties");
    expect_failure("shared-string-runs-underline.xlsx",
        R"(<sst><si><r><rPr><u/></rPr><t>x</t></r></si></sst>)",
        "unsupported run property");
    expect_failure("shared-string-runs-strike.xlsx",
        R"(<sst><si><r><rPr><strike/></rPr><t>x</t></r></si></sst>)",
        "unsupported run property");
    expect_failure("shared-string-runs-font.xlsx",
        R"(<sst><si><r><rPr><rFont val="Arial"/></rPr><t>x</t></r></si></sst>)",
        "non-default run font");
    expect_failure("shared-string-runs-size.xlsx",
        R"(<sst><si><r><rPr><sz val="12"/></rPr><t>x</t></r></si></sst>)",
        "non-default run size");
    expect_failure("shared-string-runs-family.xlsx",
        R"(<sst><si><r><rPr><family val="3"/></rPr><t>x</t></r></si></sst>)",
        "non-default run family");
    expect_failure("shared-string-runs-scheme.xlsx",
        R"(<sst><si><r><rPr><scheme val="major"/></rPr><t>x</t></r></si></sst>)",
        "non-default run scheme");
    expect_failure("shared-string-runs-theme.xlsx",
        R"(<sst><si><r><rPr><color theme="2"/></rPr><t>x</t></r></si></sst>)",
        "non-default run theme");
    expect_failure("shared-string-runs-tint.xlsx",
        R"(<sst><si><r><rPr><color rgb="FF000000" tint="0.5"/></rPr><t>x</t></r></si></sst>)",
        "unsupported run color metadata");
    expect_failure("shared-string-runs-color-shape.xlsx",
        R"(<sst><si><r><rPr><color rgb="FF000000" theme="1"/></rPr><t>x</t></r></si></sst>)",
        "one run rgb or theme");
    expect_failure("shared-string-runs-color-value.xlsx",
        R"(<sst><si><r><rPr><color rgb="FF00"/></rPr><t>x</t></r></si></sst>)",
        "invalid run ARGB");
    expect_failure("shared-string-runs-nested-text.xlsx",
        R"(<sst><si><r><t>x<foo/></t></r></si></sst>)", "nested markup");
    expect_failure("shared-string-runs-comment.xlsx",
        R"(<sst><si><!-- metadata --><t>x</t></si></sst>)",
        "unsupported markup inside an item");
    expect_failure("shared-string-runs-mismatch.xlsx",
        R"(<sst><si><r><t>x</t></x:r></si></sst>)", "mismatched XML boundary");
    expect_failure("shared-string-runs-qname.xlsx",
        R"(<a:b:sst><si><t>x</t></si></a:b:sst>)", "qualified XML element name");
    expect_failure("shared-string-runs-root-count.xlsx",
        R"(<sst uniqueCount="not-a-count"><si><t>x</t></si></sst>)",
        "invalid uniqueCount attribute");
}

void test_reader_audits_relationship_target_and_content_type()
{
    const std::string xml = R"(<sst><si><t>one</t></si></sst>)";

    FixtureOptions missing_relationship;
    missing_relationship.include_relationship = false;
    const fastxlsx::WorkbookReader no_relationship_reader =
        fastxlsx::WorkbookReader::open(write_fixture(
            "shared-string-runs-no-relationship.xlsx", xml, missing_relationship));
    expect_fastxlsx_error(
        [&] { (void)no_relationship_reader.read_shared_string_runs(); },
        "no sharedStrings relationship");

    FixtureOptions missing_target;
    missing_target.relationship_target = "missing.xml";
    const fastxlsx::WorkbookReader missing_target_reader =
        fastxlsx::WorkbookReader::open(write_fixture(
            "shared-string-runs-missing-target.xlsx", xml, missing_target));
    expect_fastxlsx_error(
        [&] { (void)missing_target_reader.read_shared_string_runs(); }, "unknown part");

    FixtureOptions wrong_content_type;
    wrong_content_type.content_type = "application/xml";
    const fastxlsx::WorkbookReader wrong_type_reader =
        fastxlsx::WorkbookReader::open(write_fixture(
            "shared-string-runs-wrong-type.xlsx", xml, wrong_content_type));
    expect_fastxlsx_error(
        [&] { (void)wrong_type_reader.read_shared_string_runs(); },
        "wrong content type");

    FixtureOptions encoded_target;
    encoded_target.relationship_target = "shared%53trings.xml";
    const fastxlsx::WorkbookReader encoded_reader = fastxlsx::WorkbookReader::open(
        write_fixture("shared-string-runs-encoded-target.xlsx", xml, encoded_target));
    check(encoded_reader.read_shared_string_runs().item_count == 1,
        "percent-encoded sharedStrings target should resolve for run traversal");

    FixtureOptions invalid_percent;
    invalid_percent.relationship_target = "sharedStrings%ZZ.xml";
    const fastxlsx::WorkbookReader invalid_percent_reader =
        fastxlsx::WorkbookReader::open(write_fixture(
            "shared-string-runs-invalid-percent.xlsx", xml, invalid_percent));
    expect_fastxlsx_error(
        [&] { (void)invalid_percent_reader.read_shared_string_runs(); },
        "invalid percent escape");

    FixtureOptions external;
    external.external_relationship = true;
    expect_fastxlsx_error(
        [&] {
            (void)fastxlsx::WorkbookReader::open(write_fixture(
                "shared-string-runs-external.xlsx", xml, external));
        },
        "internal workbook relationships");

    FixtureOptions duplicate;
    duplicate.duplicate_relationship = true;
    expect_fastxlsx_error(
        [&] {
            (void)fastxlsx::WorkbookReader::open(write_fixture(
                "shared-string-runs-duplicate-relationship.xlsx", xml, duplicate));
        },
        "duplicate workbook relationship type");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reader_reads_production_deflate_writer_output_as_simple_runs()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("shared-string-runs-deflate.xlsx");
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
    fastxlsx::SharedStringRunReadCallbacks callbacks;
    callbacks.on_run = [&](const fastxlsx::SharedStringRunView& run) {
        check(run.kind == fastxlsx::SharedStringRunKind::SimpleText
                && !run.format.bold && !run.format.italic
                && !run.format.direct_argb_color.has_value(),
            "writer shared string should project as an unformatted simple run");
        values.emplace_back(run.text);
    };
    const fastxlsx::SharedStringRunReadSummary summary =
        reader.read_shared_string_runs(callbacks);
    check(summary.item_count == 2 && summary.run_count == 2
            && values == std::vector<std::string>({"first", "second"}),
        "DEFLATE writer shared string run projection mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_reader_projects_simple_and_rich_runs_in_source_order();
        test_callback_failures_release_entry_and_reader_can_retry();
        test_reader_move_transfers_shared_string_run_state();
        test_reader_enforces_memory_and_count_guardrails();
        test_reader_handles_package_chunk_boundaries();
        test_reader_rejects_unsupported_or_malformed_run_shapes();
        test_reader_audits_relationship_target_and_content_type();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reader_reads_production_deflate_writer_output_as_simple_runs();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
