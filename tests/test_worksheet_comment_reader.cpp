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

constexpr std::string_view package_relationship_namespace =
    "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr std::string_view comments_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
constexpr std::string_view threaded_comment_relationship_type =
    "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment";
constexpr std::string_view vml_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";
constexpr std::string_view comments_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml";
constexpr std::string_view vml_content_type =
    "application/vnd.openxmlformats-officedocument.vmlDrawing";

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, std::string_view message)
{
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

template <typename Callback>
void expect_fastxlsx_error(Callback&& callback, std::string_view expected_text)
{
    bool matched = false;
    std::string actual_text;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        actual_text = error.what();
        matched = std::string_view(actual_text).find(expected_text)
            != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: "
            + std::string(expected_text) + "; actual: " + actual_text);
    }
}

std::string worksheet_xml(
    std::string_view legacy_drawing = R"(<x:legacyDrawing rel:id="rIdVml"/>)")
{
    return std::string(
               R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
               R"(<x:dimension ref="A1"/><x:sheetData/>)")
        + std::string(legacy_drawing) + R"(</x:worksheet>)";
}

std::string comments_relationships(
    std::string_view comments_target = "../comments%31.xml",
    std::string_view comments_type = comments_relationship_type,
    std::string_view comments_target_mode = {},
    bool include_vml = true,
    std::string_view vml_target = "../drawings/vmlDrawing%31.vml",
    std::string_view vml_type = vml_relationship_type,
    std::string_view vml_target_mode = {})
{
    std::string xml = "<Relationships xmlns=\""
        + std::string(package_relationship_namespace) + "\">";
    xml += "<Relationship Id=\"rIdComments\" Type=\"";
    xml += comments_type;
    xml += "\" Target=\"";
    xml += comments_target;
    xml += '"';
    if (!comments_target_mode.empty()) {
        xml += " TargetMode=\"";
        xml += comments_target_mode;
        xml += '"';
    }
    xml += "/>";
    if (include_vml) {
        xml += "<Relationship Id=\"rIdVml\" Type=\"";
        xml += vml_type;
        xml += "\" Target=\"";
        xml += vml_target;
        xml += '"';
        if (!vml_target_mode.empty()) {
            xml += " TargetMode=\"";
            xml += vml_target_mode;
            xml += '"';
        }
        xml += "/>";
    }
    xml += "</Relationships>";
    return xml;
}

std::string comments_document(
    std::string_view authors, std::string_view comments)
{
    return std::string(
               R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<c:comments xmlns:c="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><c:authors>)")
        + std::string(authors) + R"(</c:authors><c:commentList>)"
        + std::string(comments) + R"(</c:commentList></c:comments>)";
}

std::string representative_comments_xml()
{
    return comments_document(
        R"(<c:author>Alice &amp; Bob</c:author><c:author/>)",
        R"(<c:comment ref="A1" authorId="0" shapeId="42"><c:text><c:t xml:space="preserve">Hello &lt;note&gt; &amp; friend</c:t></c:text></c:comment>)"
        R"(<c:comment ref="XFD1048576" authorId="1"><c:text><c:t/></c:text></c:comment>)");
}

struct CommentFixture {
    std::string worksheet = worksheet_xml();
    std::string worksheet_relationships = comments_relationships();
    std::optional<std::string> comments = representative_comments_xml();
    std::string comments_entry_name = "xl/comments1.xml";
    std::string comments_part_content_type = std::string(comments_content_type);
    bool declare_comments_part = true;
    std::optional<std::string> vml =
        R"(<xml xmlns:v="urn:schemas-microsoft-com:vml"><v:shape id="_x0000_s1025"/></xml>)";
    std::string vml_entry_name = "xl/drawings/vmlDrawing1.vml";
    std::string vml_part_content_type = std::string(vml_content_type);
    bool declare_vml_part = true;
};

std::map<std::string, std::string> workbook_entries(CommentFixture fixture)
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (fixture.declare_comments_part) {
        content_types += "<Override PartName=\"/" + fixture.comments_entry_name
            + "\" ContentType=\"" + fixture.comments_part_content_type + "\"/>";
    }
    if (fixture.declare_vml_part) {
        content_types += "<Override PartName=\"/" + fixture.vml_entry_name
            + "\" ContentType=\"" + fixture.vml_part_content_type + "\"/>";
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
        entries, "xl/worksheets/sheet1.xml", std::move(fixture.worksheet));
    if (!fixture.worksheet_relationships.empty()) {
        fastxlsx::test::insert_zip_entry(entries,
            "xl/worksheets/_rels/sheet1.xml.rels",
            std::move(fixture.worksheet_relationships));
    }
    if (fixture.comments.has_value()) {
        fastxlsx::test::insert_zip_entry(entries, fixture.comments_entry_name,
            std::move(*fixture.comments));
    }
    if (fixture.vml.has_value()) {
        fastxlsx::test::insert_zip_entry(
            entries, fixture.vml_entry_name, std::move(*fixture.vml));
    }
    return entries;
}

std::filesystem::path write_fixture(
    std::string_view name, CommentFixture fixture = {})
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(
        path, workbook_entries(std::move(fixture)));
    return path;
}

void expect_fixture_error(std::string_view name, CommentFixture fixture,
    std::string_view expected_text)
{
    const std::filesystem::path path = write_fixture(name, std::move(fixture));
    expect_fastxlsx_error(
        [&] {
            const fastxlsx::WorkbookReader reader =
                fastxlsx::WorkbookReader::open(path);
            (void)reader.read_worksheet_comments("Data");
        },
        expected_text);
}

void test_projects_comments_and_audits_legacy_drawing()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-comment-reader-representative.xlsx");
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetCommentView> comments;
    fastxlsx::WorksheetCommentReadCallbacks callbacks;
    callbacks.on_comment = [&](const fastxlsx::WorksheetCommentView& value) {
        comments.push_back(value);
    };
    const fastxlsx::WorksheetCommentReadSummary summary =
        reader.read_worksheet_comments("Data", callbacks);

    check(comments.size() == 2 && comments[0].index == 0
            && comments[1].index == 1,
        "comment source-order projection mismatch");
    check(comments[0].row == 1 && comments[0].column == 1
            && comments[0].author == "Alice & Bob"
            && comments[0].text == "Hello <note> & friend",
        "first comment projection mismatch");
    check(comments[1].row == 1048576 && comments[1].column == 16384
            && comments[1].author.empty() && comments[1].text.empty(),
        "boundary/empty comment projection mismatch");
    check(summary.comment_count == 2 && summary.author_count == 2
            && summary.comment_with_shape_id_count == 1
            && summary.has_legacy_drawing,
        "comment summary counts mismatch");
    check(summary.total_author_bytes == 11
            && summary.peak_author_bytes == 11
            && summary.peak_comment_text_bytes == 21
            && summary.peak_cell_reference_bytes == 10
            && summary.peak_author_id_bytes == 1
            && summary.peak_shape_id_bytes == 2,
        "comment text/reference telemetry mismatch");
    check(summary.peak_relationship_id_bytes == 6
            && summary.peak_relationship_target_bytes >= 17
            && summary.peak_retained_author_count == 2
            && summary.peak_retained_comment_reference_count == 2
            && summary.peak_xml_nesting_depth >= 3,
        "comment relationship/retained telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "comment reader changed the source package");
}

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-comment-reader-retry.xlsx");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetCommentReadCallbacks throwing;
    throwing.on_comment = [](const fastxlsx::WorksheetCommentView&) {
        throw CallbackFailure("comment callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_comments("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "comment callback stopped traversal";
    }
    check(saw_exact_exception,
        "comment callback exception should propagate unchanged");

    std::size_t callback_count = 0;
    fastxlsx::WorksheetCommentReadCallbacks retry;
    retry.on_comment = [&](const fastxlsx::WorksheetCommentView&) {
        ++callback_count;
    };
    const auto summary = reader.read_worksheet_comments("Data", retry);
    check(callback_count == 2 && summary.comment_count == 2,
        "comment reader should retry from fresh package entries");
}

void test_absent_and_optional_legacy_drawing_are_clean()
{
    CommentFixture absent;
    absent.worksheet = worksheet_xml({});
    absent.worksheet_relationships.clear();
    absent.comments.reset();
    absent.declare_comments_part = false;
    absent.vml.reset();
    absent.declare_vml_part = false;
    const std::filesystem::path absent_path = write_fixture(
        "worksheet-comment-reader-absent.xlsx", std::move(absent));
    const auto absent_reader = fastxlsx::WorkbookReader::open(absent_path);
    const auto absent_summary = absent_reader.read_worksheet_comments("Data");
    check(absent_summary.comment_count == 0
            && absent_summary.author_count == 0
            && !absent_summary.has_legacy_drawing,
        "absent comments relationship should return a clean empty summary");

    CommentFixture no_vml;
    no_vml.worksheet = worksheet_xml({});
    no_vml.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, false);
    no_vml.vml.reset();
    no_vml.declare_vml_part = false;
    const std::filesystem::path no_vml_path = write_fixture(
        "worksheet-comment-reader-no-vml.xlsx", std::move(no_vml));
    const auto no_vml_reader = fastxlsx::WorkbookReader::open(no_vml_path);
    const auto no_vml_summary = no_vml_reader.read_worksheet_comments("Data");
    check(no_vml_summary.comment_count == 2
            && !no_vml_summary.has_legacy_drawing,
        "classic comments should not require a legacyDrawing reference");

    CommentFixture unrelated;
    unrelated.worksheet = worksheet_xml({});
    unrelated.worksheet_relationships = comments_relationships(
        "../comments1.xml", "urn:not-comments", {}, false);
    unrelated.vml.reset();
    unrelated.declare_vml_part = false;
    const std::filesystem::path unrelated_path = write_fixture(
        "worksheet-comment-reader-unrelated-relationship.xlsx",
        std::move(unrelated));
    const auto unrelated_reader = fastxlsx::WorkbookReader::open(unrelated_path);
    check(unrelated_reader.read_worksheet_comments("Data").comment_count == 0,
        "unrelated worksheet relationships must not be inferred as comments");
}

void test_guardrails_and_zero_options()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-comment-reader-guardrails.xlsx");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetCommentReaderOptions options;
    options.max_xml_window_bytes = 32;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "bounded input window");
    options = {};
    options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_comment_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_comment_count");
    options = {};
    options.max_author_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_author_count");
    options = {};
    options.max_author_bytes = 10;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_author_bytes");
    options = {};
    options.max_total_author_bytes = 10;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_total_author_bytes");
    options = {};
    options.max_comment_text_bytes = 5;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_comment_text_bytes");
    options = {};
    options.max_cell_reference_bytes = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_cell_reference_bytes");
    options = {};
    options.max_relationship_id_bytes = 5;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_relationship_id_bytes");
    options = {};
    options.max_relationship_target_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_relationship_target_bytes");
    options = {};
    options.max_shape_id_bytes = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
        "max_shape_id_bytes");

    CommentFixture long_author_id;
    const std::string marker = "authorId=\"0\"";
    const std::size_t marker_offset = long_author_id.comments->find(marker);
    check(marker_offset != std::string::npos,
        "authorId guardrail fixture marker is missing");
    long_author_id.comments->replace(
        marker_offset, marker.size(), "authorId=\"00\"");
    const std::filesystem::path long_author_id_path = write_fixture(
        "worksheet-comment-reader-author-id-guardrail.xlsx",
        std::move(long_author_id));
    const fastxlsx::WorkbookReader long_author_id_reader =
        fastxlsx::WorkbookReader::open(long_author_id_path);
    options = {};
    options.max_author_id_bytes = 1;
    expect_fastxlsx_error(
        [&] {
            (void)long_author_id_reader.read_worksheet_comments(
                "Data", {}, options);
        },
        "max_author_id_bytes");

    using OptionMember = std::size_t
        fastxlsx::WorksheetCommentReaderOptions::*;
    const std::vector<std::pair<OptionMember, std::string_view>> zero_options = {
        {&fastxlsx::WorksheetCommentReaderOptions::max_xml_window_bytes,
            "nonzero max_xml_window_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_xml_nesting_depth,
            "nonzero max_xml_nesting_depth"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_comment_count,
            "nonzero max_comment_count"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_author_count,
            "nonzero max_author_count"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_author_bytes,
            "nonzero max_author_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_total_author_bytes,
            "nonzero max_total_author_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_comment_text_bytes,
            "nonzero max_comment_text_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_cell_reference_bytes,
            "nonzero max_cell_reference_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_author_id_bytes,
            "nonzero max_author_id_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_relationship_id_bytes,
            "nonzero max_relationship_id_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_relationship_target_bytes,
            "nonzero max_relationship_target_bytes"},
        {&fastxlsx::WorksheetCommentReaderOptions::max_shape_id_bytes,
            "nonzero max_shape_id_bytes"},
    };
    for (const auto& [member, diagnostic] : zero_options) {
        options = {};
        options.*member = 0;
        expect_fastxlsx_error(
            [&] { (void)reader.read_worksheet_comments("Data", {}, options); },
            diagnostic);
    }
}

void test_comments_relationship_and_content_type_audit()
{
    CommentFixture duplicate;
    duplicate.worksheet_relationships =
        std::string("<Relationships xmlns=\"")
        + std::string(package_relationship_namespace) + "\">"
        + "<Relationship Id=\"rIdComments1\" Type=\""
        + std::string(comments_relationship_type)
        + "\" Target=\"../comments1.xml\"/>"
          "<Relationship Id=\"rIdComments2\" Type=\""
        + std::string(comments_relationship_type)
        + "\" Target=\"../comments2.xml\"/>"
          "</Relationships>";
    duplicate.worksheet = worksheet_xml({});
    duplicate.vml.reset();
    duplicate.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-duplicate-relationship.xlsx",
        std::move(duplicate), "duplicate comments relationships");

    CommentFixture external;
    external.worksheet = worksheet_xml({});
    external.worksheet_relationships = comments_relationships(
        "https://example.invalid/comments.xml", comments_relationship_type,
        "External", false);
    external.vml.reset();
    external.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-external-comments.xlsx",
        std::move(external), "must be internal");

    CommentFixture unknown;
    unknown.worksheet = worksheet_xml({});
    unknown.worksheet_relationships = comments_relationships(
        "../missing-comments.xml", comments_relationship_type, {}, false);
    unknown.vml.reset();
    unknown.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-unknown-comments.xlsx",
        std::move(unknown), "unknown part");

    CommentFixture wrong_content_type;
    wrong_content_type.worksheet = worksheet_xml({});
    wrong_content_type.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, false);
    wrong_content_type.comments_part_content_type = "application/xml";
    wrong_content_type.vml.reset();
    wrong_content_type.declare_vml_part = false;
    expect_fixture_error(
        "worksheet-comment-reader-wrong-comments-content-type.xlsx",
        std::move(wrong_content_type), "wrong content type");

    CommentFixture missing_entry;
    missing_entry.worksheet = worksheet_xml({});
    missing_entry.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, false);
    missing_entry.comments.reset();
    missing_entry.vml.reset();
    missing_entry.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-missing-comments-entry.xlsx",
        std::move(missing_entry), "unknown part");

    CommentFixture invalid_percent;
    invalid_percent.worksheet = worksheet_xml({});
    invalid_percent.worksheet_relationships = comments_relationships(
        "../comments%XZ.xml", comments_relationship_type, {}, false);
    invalid_percent.vml.reset();
    invalid_percent.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-invalid-percent.xlsx",
        std::move(invalid_percent), "percent encoding");

    CommentFixture query;
    query.worksheet = worksheet_xml({});
    query.worksheet_relationships = comments_relationships(
        "../comments1.xml?query", comments_relationship_type, {}, false);
    query.vml.reset();
    query.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-query-target.xlsx",
        std::move(query), "query or fragment");
}

void test_rejects_threaded_comments()
{
    CommentFixture fixture;
    fixture.worksheet_relationships =
        std::string("<Relationships xmlns=\"")
        + std::string(package_relationship_namespace) + "\">"
        + "<Relationship Id=\"rIdComments\" Type=\""
        + std::string(comments_relationship_type)
        + "\" Target=\"../comments1.xml\"/>"
          "<Relationship Id=\"rIdThreaded\" Type=\""
        + std::string(threaded_comment_relationship_type)
        + "\" Target=\"../threadedComments/threadedComment1.xml\"/>"
          "</Relationships>";
    fixture.worksheet = worksheet_xml({});
    fixture.vml.reset();
    fixture.declare_vml_part = false;
    expect_fixture_error("worksheet-comment-reader-threaded.xlsx",
        std::move(fixture), "threaded comments or persons");
}

void test_legacy_drawing_relationship_audit()
{
    CommentFixture missing_id;
    missing_id.worksheet = worksheet_xml(
        R"(<x:legacyDrawing rel:id="rIdMissing"/>)");
    expect_fixture_error("worksheet-comment-reader-vml-missing-id.xlsx",
        std::move(missing_id), "relationship id is missing");

    CommentFixture wrong_type;
    wrong_type.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, true,
        "../drawings/vmlDrawing1.vml", "urn:not-vml");
    expect_fixture_error("worksheet-comment-reader-vml-wrong-type.xlsx",
        std::move(wrong_type), "wrong type");

    CommentFixture external;
    external.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, true,
        "https://example.invalid/vml.xml", vml_relationship_type, "External");
    expect_fixture_error("worksheet-comment-reader-vml-external.xlsx",
        std::move(external), "must be internal");

    CommentFixture unknown;
    unknown.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, true,
        "../drawings/missing.vml");
    expect_fixture_error("worksheet-comment-reader-vml-unknown.xlsx",
        std::move(unknown), "unknown part");

    CommentFixture wrong_content_type;
    wrong_content_type.vml_part_content_type = "application/xml";
    expect_fixture_error("worksheet-comment-reader-vml-content-type.xlsx",
        std::move(wrong_content_type), "wrong content type");

    CommentFixture missing_entry;
    missing_entry.vml.reset();
    expect_fixture_error("worksheet-comment-reader-vml-missing-entry.xlsx",
        std::move(missing_entry), "unknown part");

    CommentFixture invalid_percent;
    invalid_percent.worksheet_relationships = comments_relationships(
        "../comments1.xml", comments_relationship_type, {}, true,
        "../drawings/vml%XZ.vml");
    expect_fixture_error("worksheet-comment-reader-vml-invalid-percent.xlsx",
        std::move(invalid_percent), "percent encoding");

    CommentFixture duplicate_reference;
    duplicate_reference.worksheet = worksheet_xml(
        R"(<x:legacyDrawing rel:id="rIdVml"/><x:legacyDrawing rel:id="rIdVml"/>)");
    expect_fixture_error("worksheet-comment-reader-vml-duplicate.xlsx",
        std::move(duplicate_reference), "duplicate or nested legacyDrawing");
}

void test_rejects_lossy_or_malformed_comment_shapes()
{
    struct ShapeCase {
        std::string name;
        std::string comments;
        std::string expected;
    };
    const std::string author = R"(<c:author>Alice</c:author>)";
    const std::vector<ShapeCase> cases = {
        {"rich",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0"><c:text><c:r><c:t>rich</c:t></c:r></c:text></c:comment>)"),
            "rich text runs"},
        {"phonetic",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0"><c:text><c:rPh><c:t>x</c:t></c:rPh></c:text></c:comment>)"),
            "phonetic metadata"},
        {"extension",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0"><c:text><c:extLst/></c:text></c:comment>)"),
            "extension metadata"},
        {"duplicate-ref",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0"><c:text><c:t>one</c:t></c:text></c:comment><c:comment ref="A1" authorId="0"><c:text><c:t>two</c:t></c:text></c:comment>)"),
            "duplicate cell reference"},
        {"invalid-author-id",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="1"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "outside the author table"},
        {"invalid-ref",
            comments_document(author,
                R"(<c:comment ref="a1" authorId="0"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "valid uppercase A1 cell reference"},
        {"invalid-shape-id",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0" shapeId="shape"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "invalid shapeId"},
        {"large-shape-id",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0" shapeId="4294967296"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "shapeId exceeds uint32"},
        {"unsupported-comment-metadata",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0" visible="1"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "unsupported comment metadata"},
        {"nested-author",
            comments_document(R"(<c:author><c:t>Alice</c:t></c:author>)",
                R"(<c:comment ref="A1" authorId="0"><c:text><c:t>x</c:t></c:text></c:comment>)"),
            "unsupported child element"},
        {"nested-namespace",
            comments_document(author,
                R"(<c:comment ref="A1" authorId="0"><c:text><c:t xmlns:q="urn:q">x</c:t></c:text></c:comment>)"),
            "nested namespace declarations"},
        {"schema-order",
            R"(<c:comments xmlns:c="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><c:commentList><c:comment ref="A1" authorId="0"><c:text><c:t>x</c:t></c:text></c:comment></c:commentList><c:authors><c:author>Alice</c:author></c:authors></c:comments>)",
            "out of schema order"},
        {"missing-namespace",
            R"(<comments><authors><author>Alice</author></authors><commentList><comment ref="A1" authorId="0"><text><t>x</t></text></comment></commentList></comments>)",
            "namespace"},
    };

    for (const ShapeCase& shape_case : cases) {
        CommentFixture fixture;
        fixture.comments = shape_case.comments;
        expect_fixture_error(
            "worksheet-comment-reader-shape-" + shape_case.name + ".xlsx",
            std::move(fixture), shape_case.expected);
    }
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_comments()
{
    const std::filesystem::path source = write_fixture(
        "worksheet-comment-reader-deflate-source.xlsx");
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "worksheet-comment-reader-deflate.xlsx");
    std::error_code remove_error;
    std::filesystem::remove(output, remove_error);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditorSaveOptions options;
    options.zip_compression_level = 1;
    editor.save_as(output, options);

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(output);
    std::vector<fastxlsx::WorksheetCommentView> comments;
    fastxlsx::WorksheetCommentReadCallbacks callbacks;
    callbacks.on_comment = [&](const fastxlsx::WorksheetCommentView& value) {
        comments.push_back(value);
    };
    const auto summary = reader.read_worksheet_comments("Data", callbacks);
    check(summary.comment_count == 2 && summary.author_count == 2
            && summary.has_legacy_drawing && comments.size() == 2
            && comments[0].text == "Hello <note> & friend",
        "DEFLATE comments traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_comments_and_audits_legacy_drawing();
        test_callback_failure_allows_retry();
        test_absent_and_optional_legacy_drawing_are_clean();
        test_guardrails_and_zero_options();
        test_comments_relationship_and_content_type_audit();
        test_rejects_threaded_comments();
        test_legacy_drawing_relationship_audit();
        test_rejects_lossy_or_malformed_comment_shapes();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_comments();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "All worksheet comment reader tests passed\n";
    return 0;
}
