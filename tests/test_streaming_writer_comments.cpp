#include <fastxlsx/fastxlsx.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, std::string_view message)
{
    if (!condition) {
        throw TestFailure(std::string(message));
    }
}

void check_contains(std::string_view text, std::string_view fragment, std::string_view message)
{
    check(text.find(fragment) != std::string_view::npos, message);
}

std::size_t count_occurrences(std::string_view text, std::string_view fragment)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(fragment, offset)) != std::string_view::npos) {
        ++count;
        offset += fragment.size();
    }
    return count;
}

template <typename Callback>
void expect_fastxlsx_error(Callback&& callback, std::string_view expected_text)
{
    std::string actual_text;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        actual_text = error.what();
    }
    check(std::string_view(actual_text).find(expected_text) != std::string_view::npos,
        "FastXlsxError diagnostic mismatch");
}

std::vector<fastxlsx::WorksheetCommentView> read_notes(
    const std::filesystem::path& path, std::string_view worksheet_name,
    fastxlsx::WorksheetCommentReadSummary& summary)
{
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetCommentView> notes;
    fastxlsx::WorksheetCommentReadCallbacks callbacks;
    callbacks.on_comment = [&](const fastxlsx::WorksheetCommentView& note) {
        notes.push_back(note);
    };
    summary = reader.read_worksheet_comments(worksheet_name, callbacks);
    return notes;
}

void test_writes_classic_note_parts_and_round_trips()
{
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-streaming-comments.xlsx");
    const std::string unicode_author = "Author \xE4\xBD\xA0\xE5\xA5\xBD";
    const std::string unicode_text = " second line \xE4\xB8\x96\xE7\x95\x8C & more ";
    const std::string unicode_text_xml =
        " second line \xE4\xB8\x96\xE7\x95\x8C &amp; more ";

    auto workbook = fastxlsx::WorkbookWriter::create(output);
    auto notes = workbook.add_worksheet("Notes");
    auto plain = workbook.add_worksheet("Plain");
    auto more_notes = workbook.add_worksheet("MoreNotes");

    notes.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Value")});
    notes.append_row({fastxlsx::CellView::text("One"), fastxlsx::CellView::number(1.0)});
    notes.append_row({fastxlsx::CellView::text("Two"), fastxlsx::CellView::number(2.0)});
    notes.add_external_hyperlink(2, 1, "https://example.com/note");
    notes.add_note(1, 1, "Alice & Bob", "Hello <note> & friend");
    notes.add_note(3, 3, unicode_author, unicode_text);
    notes.add_note(2, 2, "Alice & Bob", "Repeated author");

    fastxlsx::TableOptions table;
    table.name = "NotesTable";
    table.column_names = {"Name", "Value"};
    notes.add_table({1, 1, 3, 2}, std::move(table));

    plain.append_row({fastxlsx::CellView::text("No notes")});
    more_notes.add_note(1048576, 16384, "Max Cell", "Unwritten cell note");
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.contains("xl/comments1.xml"), "first comments part missing");
    check(entries.contains("xl/comments2.xml"), "second comments part missing");
    check(entries.contains("xl/drawings/vmlDrawing1.vml"), "first note VML part missing");
    check(entries.contains("xl/drawings/vmlDrawing2.vml"), "second note VML part missing");
    check(!entries.contains("xl/comments3.xml"), "plain worksheet should not consume a comments index");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "plain worksheet should not create relationships");

    const std::string& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Default Extension="vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)",
        "VML content type missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/comments1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"/>)",
        "first comments content type missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/comments2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"/>)",
        "second comments content type missing");

    const std::string& worksheet = entries.at("xl/worksheets/sheet1.xml");
    const std::size_t hyperlinks_position = worksheet.find("<hyperlinks>");
    const std::size_t legacy_drawing_position = worksheet.find(R"(<legacyDrawing r:id="rId2"/>)");
    const std::size_t table_parts_position = worksheet.find("<tableParts");
    check(hyperlinks_position < legacy_drawing_position
            && legacy_drawing_position < table_parts_position,
        "legacyDrawing suffix order mismatch");
    check_contains(worksheet, R"(<tablePart r:id="rId4"/>)",
        "table relationship id should follow VML and comments relationships");

    const std::string& relationships =
        entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(relationships, "<Relationship ") == 4,
        "worksheet relationship count mismatch");
    check_contains(relationships,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/note" TargetMode="External"/>)",
        "external hyperlink relationship mismatch");
    check_contains(relationships,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawing1.vml"/>)",
        "VML relationship mismatch");
    check_contains(relationships,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments1.xml"/>)",
        "comments relationship mismatch");
    check_contains(relationships,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship mismatch");

    const std::string& comments = entries.at("xl/comments1.xml");
    check_contains(comments,
        "<authors><author>Alice &amp; Bob</author><author>" + unicode_author
            + "</author></authors>",
        "author table should deduplicate in first-use order");
    check(count_occurrences(comments, "<author>") == 2, "author deduplication failed");
    check_contains(comments,
        R"(<comment ref="A1" authorId="0"><text><t>Hello &lt;note&gt; &amp; friend</t></text></comment>)",
        "first simple note XML mismatch");
    check_contains(comments,
        "<comment ref=\"C3\" authorId=\"1\"><text><t xml:space=\"preserve\">"
            + unicode_text_xml + "</t></text></comment>",
        "whitespace-preserving Unicode note XML mismatch");
    check_contains(comments,
        R"(<comment ref="B2" authorId="0"><text><t>Repeated author</t></text></comment>)",
        "reused author id mismatch");
    check(comments.find("<r>") == std::string::npos,
        "classic note writer should not emit rich runs");

    const std::string& vml = entries.at("xl/drawings/vmlDrawing1.vml");
    check(count_occurrences(vml, "<v:shape id=") == 3, "VML note shape count mismatch");
    check(count_occurrences(vml, "visibility:hidden") == 3,
        "all generated note shapes must be hidden");
    check(vml.find("<x:Visible") == std::string::npos,
        "hidden note VML should not request visible shapes");
    check_contains(vml, "<x:Row>0</x:Row><x:Column>0</x:Column>",
        "first VML note cell mismatch");
    check_contains(vml, "<x:Row>2</x:Row><x:Column>2</x:Column>",
        "second VML note cell mismatch");
    check_contains(entries.at("xl/drawings/vmlDrawing2.vml"),
        "<x:Row>1048575</x:Row><x:Column>16383</x:Column>",
        "maximum worksheet VML coordinate mismatch");

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "Notes", summary);
    check(summary.comment_count == 3 && summary.author_count == 2
            && summary.has_legacy_drawing && projected.size() == 3,
        "comment reader round-trip summary mismatch");
    check(projected[0].row == 1 && projected[0].column == 1
            && projected[0].author == "Alice & Bob"
            && projected[0].text == "Hello <note> & friend",
        "first comment reader round-trip value mismatch");
    check(projected[1].row == 3 && projected[1].column == 3
            && projected[1].author == unicode_author && projected[1].text == unicode_text,
        "Unicode comment reader round-trip value mismatch");
    check(projected[2].row == 2 && projected[2].column == 2
            && projected[2].author == "Alice & Bob",
        "comment reader source order mismatch");
}

void test_stored_note_round_trip()
{
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-streaming-comments-stored.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = fastxlsx::min_zip_compression_level;
    auto workbook = fastxlsx::WorkbookWriter::create(output, options);
    auto sheet = workbook.add_worksheet("StoredNotes");
    sheet.add_note(2, 4, "Stored", "Stored package note");
    workbook.close();

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "StoredNotes", summary);
    check(summary.comment_count == 1 && summary.author_count == 1
            && summary.has_legacy_drawing && projected.size() == 1,
        "stored classic note round-trip summary mismatch");
    check(projected[0].row == 2 && projected[0].column == 4
            && projected[0].author == "Stored" && projected[0].text == "Stored package note",
        "stored classic note round-trip value mismatch");
}

void test_note_validation_and_lifecycle()
{
    fastxlsx::WorksheetWriter detached;
    expect_fastxlsx_error(
        [&] { detached.add_note(1, 1, "Author", "Text"); }, "not attached");

    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-streaming-comments-validation.xlsx");
    auto workbook = fastxlsx::WorkbookWriter::create(output);
    auto sheet = workbook.add_worksheet("Validation");
    expect_fastxlsx_error(
        [&] { sheet.add_note(0, 1, "Author", "Text"); }, "1-based");
    expect_fastxlsx_error(
        [&] { sheet.add_note(1048577, 1, "Author", "Text"); }, "exceeds");
    expect_fastxlsx_error(
        [&] { sheet.add_note(1, 16385, "Author", "Text"); }, "exceeds");
    expect_fastxlsx_error(
        [&] { sheet.add_note(1, 1, "", "Text"); }, "author cannot be empty");
    expect_fastxlsx_error(
        [&] { sheet.add_note(1, 1, "Author", ""); }, "text cannot be empty");
    sheet.add_note(1, 1, "Author", "First");
    expect_fastxlsx_error(
        [&] { sheet.add_note(1, 1, "Other", "Duplicate"); }, "already has");
    workbook.close();
    check(fastxlsx::detail::testing_worksheet_temporary_resources_released(sheet),
        "successful close should release classic note construction state");
    expect_fastxlsx_error(
        [&] { sheet.add_note(2, 2, "Author", "Closed"); }, "after workbook close");

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(count_occurrences(entries.at("xl/comments1.xml"), "<comment ref=") == 1,
        "failed note calls should not pollute writer state");
}

void test_failed_close_preserves_note_state_for_retry()
{
    const std::filesystem::path output =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-comments-retry.xlsx";
    std::error_code error;
    std::filesystem::remove_all(output, error);
    error.clear();
    std::filesystem::create_directories(output, error);
    check(!error, "failed to create conflicting output directory for close retry");

    auto workbook = fastxlsx::WorkbookWriter::create(output);
    auto sheet = workbook.add_worksheet("RetryNotes");
    sheet.add_note(4, 2, "Retry", "Preserved after package failure");

    bool close_failed = false;
    try {
        workbook.close();
    } catch (const fastxlsx::FastXlsxError&) {
        close_failed = true;
    }
    check(close_failed, "close should fail while the output path is a directory");
    check(!fastxlsx::detail::testing_worksheet_temporary_resources_released(sheet),
        "failed close must retain classic note construction state");

    error.clear();
    std::filesystem::remove_all(output, error);
    check(!error, "failed to remove conflicting output directory for close retry");
    workbook.close();

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "RetryNotes", summary);
    check(projected.size() == 1 && projected[0].row == 4 && projected[0].column == 2
            && projected[0].author == "Retry"
            && projected[0].text == "Preserved after package failure",
        "close retry should serialize the retained classic note");
}

#if FASTXLSX_HAS_IMAGES
void test_note_relationships_compose_with_spreadsheet_drawing()
{
    const std::filesystem::path output =
        fastxlsx::test::artifact_path("fastxlsx-streaming-comments-with-image.xlsx");
    auto workbook = fastxlsx::WorkbookWriter::create(output);
    auto sheet = workbook.add_worksheet("Objects");
    sheet.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Value")});
    sheet.append_row({fastxlsx::CellView::text("One"), fastxlsx::CellView::number(1.0)});
    sheet.add_external_hyperlink(2, 1, "https://example.com/object");
    sheet.add_image(fastxlsx::test::tiny_png_bytes(), {1, 3, 2, 3});
    sheet.add_note(1, 1, "Author", "Image and note");
    fastxlsx::TableOptions table;
    table.name = "ObjectNoteTable";
    table.column_names = {"Name", "Value"};
    sheet.add_table({1, 1, 2, 2}, std::move(table));
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet,
        R"(<hyperlinks><hyperlink ref="A2" r:id="rId1"/></hyperlinks><drawing r:id="rId2"/><legacyDrawing r:id="rId3"/><tableParts count="1"><tablePart r:id="rId5"/></tableParts>)",
        "drawing, legacyDrawing, and tableParts suffix composition mismatch");

    const std::string& relationships =
        entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(relationships,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "spreadsheet drawing relationship mismatch");
    check_contains(relationships,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawing1.vml"/>)",
        "note VML relationship should follow spreadsheet drawing");
    check_contains(relationships,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments1.xml"/>)",
        "comments relationship should follow note VML");
    check_contains(relationships,
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship should follow classic note relationships");
}
#endif

} // namespace

int main()
{
    try {
        test_writes_classic_note_parts_and_round_trips();
        test_stored_note_round_trip();
        test_note_validation_and_lifecycle();
        test_failed_close_preserves_note_state_for_retry();
#if FASTXLSX_HAS_IMAGES
        test_note_relationships_compose_with_spreadsheet_drawing();
#endif
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
