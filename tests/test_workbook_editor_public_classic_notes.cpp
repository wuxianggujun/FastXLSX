#include <fastxlsx/fastxlsx.hpp>

#include "../src/package_editor.hpp"

#include <algorithm>
#include <system_error>

#include "test_workbook_editor_facade_common.hpp"

constexpr std::string_view comments_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments";
constexpr std::string_view threaded_comment_relationship_type =
    "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment";
constexpr std::string_view vml_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing";
constexpr std::string_view comments_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml";
constexpr std::string_view threaded_comments_content_type =
    "application/vnd.ms-excel.threadedcomments+xml";
constexpr std::string_view vml_content_type =
    "application/vnd.openxmlformats-officedocument.vmlDrawing";

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

void fail_after_classic_note_staging()
{
    throw fastxlsx::FastXlsxError("injected classic note staging failure");
}

std::size_t count_occurrences(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

std::vector<fastxlsx::WorksheetCommentView> read_notes(
    const std::filesystem::path& path,
    std::string_view sheet_name,
    fastxlsx::WorksheetCommentReadSummary& summary)
{
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetCommentView> notes;
    fastxlsx::WorksheetCommentReadCallbacks callbacks;
    callbacks.on_comment = [&](const fastxlsx::WorksheetCommentView& note) {
        notes.push_back(note);
    };
    summary = reader.read_worksheet_comments(sheet_name, callbacks);
    return notes;
}

const fastxlsx::WorkbookEditorWorksheetEditSummary* find_summary(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& summaries,
    std::string_view planned_name)
{
    const auto found = std::find_if(summaries.begin(), summaries.end(),
        [planned_name](const auto& summary) {
            return summary.planned_name == planned_name;
        });
    return found == summaries.end() ? nullptr : &*found;
}

std::filesystem::path write_relationship_preservation_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row(
        {fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Value")});
    data.append_row(
        {fastxlsx::CellView::text("One"), fastxlsx::CellView::number(1.0)});
    data.add_external_hyperlink(1, 1, "https://example.invalid/source");
    fastxlsx::TableOptions table;
    table.name = "PatchNoteTable";
    table.column_names = {"Name", "Value"};
    data.add_table({1, 1, 2, 2}, std::move(table));
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep-me")});
    writer.close();

    auto entries = fastxlsx::test::read_zip_entries(path);
    fastxlsx::test::insert_zip_entry(
        entries, "custom/opaque.bin", "classic note unknown entry");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_source_owned_note_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("source note")});
    data.add_note(1, 1, "Source", "Owned by the source package");
    writer.close();
    return path;
}

std::filesystem::path write_canonical_note_edit_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("source payload")});
    data.add_external_hyperlink(1, 1, "https://example.invalid/note-edit");
    data.add_note(1, 1, "Alice", "First source note");
    data.add_note(2, 2, "Bob", "Second source note");
    data.add_note(3, 3, "Alice", "Third source note");
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("preserve me")});
    writer.close();

    auto entries = fastxlsx::test::read_zip_entries(path);
    fastxlsx::test::insert_zip_entry(
        entries, "custom/source-note-edit.bin", "preserve source note unknown entry");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_threaded_comment_relationship_source(std::string_view name)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    auto entries = fastxlsx::test::read_zip_entries(path);
    replace_first_or_throw(entries.at("[Content_Types].xml"), "</Types>",
        "<Override PartName=\"/xl/threadedComments/threadedComment1.xml\" "
        "ContentType=\"" + std::string(threaded_comments_content_type)
            + "\"/></Types>");
    fastxlsx::test::insert_zip_entry(entries,
        "xl/worksheets/_rels/sheet1.xml.rels",
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rIdThreaded\" Type=\""
            + std::string(threaded_comment_relationship_type)
            + "\" Target=\"../threadedComments/threadedComment1.xml\"/>"
              "</Relationships>");
    fastxlsx::test::insert_zip_entry(entries,
        "xl/threadedComments/threadedComment1.xml",
        "<ThreadedComments xmlns=\"http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments\"/>");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_header_footer_vml_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("header footer VML")});
    data.add_external_hyperlink(1, 1, "https://example.invalid/vml-source");
    writer.close();

    auto entries = fastxlsx::test::read_zip_entries(path);
    replace_first_or_throw(entries.at("[Content_Types].xml"), "</Types>",
        "<Default Extension=\"vml\" ContentType=\"" + std::string(vml_content_type)
            + "\"/></Types>");
    replace_first_or_throw(
        entries.at("xl/worksheets/_rels/sheet1.xml.rels"), "</Relationships>",
        "<Relationship Id=\"rId2\" Type=\"" + std::string(vml_relationship_type)
            + "\" Target=\"../drawings/vmlDrawing9.vml\"/></Relationships>");
    replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"), "</worksheet>",
        "<legacyDrawingHF r:id=\"rId2\"/></worksheet>");
    fastxlsx::test::insert_zip_entry(entries,
        "xl/drawings/vmlDrawing9.vml",
        "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\"/>");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

void test_adds_notes_preserves_package_and_round_trips()
{
    const std::filesystem::path source = write_relationship_preservation_source(
        "fastxlsx-workbook-editor-classic-note-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-classic-note-output.xlsx");
    const std::string unicode_author = "Author \xE4\xBD\xA0\xE5\xA5\xBD";
    const std::string unicode_text =
        " leading \xE4\xB8\x96\xE7\x95\x8C & <note> trailing ";

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_note("Data", {1, 1}, "Alice & Bob", "Hello <note> & friend");
    editor.add_note("Data", {1048576, 16384}, unicode_author, unicode_text);
    editor.add_note("Data", {2, 2}, "Alice & Bob", "Repeated author");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().classic_note_count == 3,
        "classic note calls should expose the queued note count");
    check(editor.pending_change_count() == 3 && editor.unsaved_change_count() == 3,
        "classic note calls should advance pending and unsaved counts");
    editor.save_as(output);
    check(editor.has_pending_changes() && !editor.has_unsaved_changes(),
        "successful note save should retain staged Patch state but clear unsaved state");

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.at("custom/opaque.bin") == "classic note unknown entry",
        "classic note insertion should preserve unknown entries");
    check(entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "classic note insertion should preserve the unrelated worksheet payload");
    check(entries.contains("xl/comments1.xml")
            && entries.contains("xl/drawings/vmlDrawing1.vml"),
        "classic note insertion should generate comments and VML parts");

    const std::string& worksheet = entries.at("xl/worksheets/sheet1.xml");
    const std::size_t hyperlinks_position = worksheet.find("<hyperlinks>");
    const std::size_t legacy_drawing_position =
        worksheet.find("<legacyDrawing r:id=\"rId3\"/>");
    const std::size_t table_parts_position = worksheet.find("<tableParts");
    check(hyperlinks_position < legacy_drawing_position
            && legacy_drawing_position < table_parts_position,
        "Patch legacyDrawing should be inserted at the schema-safe suffix boundary");
    check_contains(worksheet, R"(<c r="A1" t="inlineStr"><is><t>Name</t></is></c>)",
        "classic note insertion should preserve existing cell payloads");
    check_not_contains(worksheet, "XFD1048576",
        "a note on an unwritten maximum coordinate should not create a cell");

    const std::string& relationships =
        entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(relationships,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/source" TargetMode="External"/>)",
        "classic note insertion should preserve an unrelated external relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "classic note insertion should preserve the existing table relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawing1.vml"/>)",
        "classic note insertion should append the VML relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments1.xml"/>)",
        "classic note insertion should append the comments relationship");

    const std::string& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Default Extension="vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)",
        "classic note insertion should register the VML default content type");
    check_contains(content_types,
        R"(<Override PartName="/xl/comments1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"/>)",
        "classic note insertion should register the comments override");

    const std::string& comments = entries.at("xl/comments1.xml");
    check(count_occurrences(comments, "<author>") == 2,
        "classic note authors should deduplicate in first-use order");
    check_contains(comments,
        "<authors><author>Alice &amp; Bob</author><author>" + unicode_author
            + "</author></authors>",
        "classic note author table should preserve first-use order and UTF-8");
    check_contains(comments,
        R"(<comment ref="A1" authorId="0"><text><t>Hello &lt;note&gt; &amp; friend</t></text></comment>)",
        "classic note text should be XML escaped");
    check_contains(comments,
        "<comment ref=\"XFD1048576\" authorId=\"1\"><text><t xml:space=\"preserve\">",
        "classic note whitespace should request xml:space preservation");
    check_contains(comments,
        R"(<comment ref="B2" authorId="0"><text><t>Repeated author</t></text></comment>)",
        "same-session note regeneration should retain earlier notes in order");

    const std::string& vml = entries.at("xl/drawings/vmlDrawing1.vml");
    check(count_occurrences(vml, "<v:shape id=") == 3,
        "classic note VML should contain one hidden shape per note");
    check(count_occurrences(vml, "visibility:hidden") == 3,
        "classic note VML shapes should remain hidden");
    check_contains(vml,
        "<x:Row>1048575</x:Row><x:Column>16383</x:Column>",
        "classic note VML should project maximum coordinates as zero-based values");

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "Data", summary);
    check(summary.comment_count == 3 && summary.author_count == 2
            && summary.has_legacy_drawing && projected.size() == 3,
        "stored Patch classic notes should reopen through the bounded reader");
    check(projected[0].author == "Alice & Bob"
            && projected[0].text == "Hello <note> & friend",
        "stored Patch classic note first value mismatch");
    check(projected[1].row == 1048576 && projected[1].column == 16384
            && projected[1].author == unicode_author && projected[1].text == unicode_text,
        "stored Patch classic note UTF-8/max-coordinate value mismatch");
}

void test_validation_transaction_failure_and_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-classic-note-retry-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.add_note("Data", {0, 1}, "Author", "Text");
    }), "classic note should reject zero row coordinates");
    check(threw_fastxlsx_error([&] {
        editor.add_note("Data", {1, 16385}, "Author", "Text");
    }), "classic note should reject columns beyond Excel limits");
    check(threw_fastxlsx_error([&] {
        editor.add_note("Data", {1, 1}, "", "Text");
    }), "classic note should reject an empty author");
    check(threw_fastxlsx_error([&] {
        editor.add_note("Data", {1, 1}, "Author", "");
    }), "classic note should reject empty text");
    check(threw_fastxlsx_error([&] {
        editor.add_note("Missing", {1, 1}, "Author", "Text");
    }), "classic note should reject a missing planned worksheet");
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes(),
        "invalid classic note calls should not publish package state");

    editor.add_note("Data", {1, 1}, "Author", "First");
    const auto before_duplicate = editor.pending_worksheet_edits();
    check(threw_fastxlsx_error([&] {
        editor.add_note("Data", {1, 1}, "Other", "Duplicate");
    }), "classic note should reject a duplicate queued cell");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), before_duplicate)
            && editor.pending_change_count() == 1,
        "duplicate classic note rejection should preserve public state");

    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_classic_note_staging);
        check(threw_fastxlsx_error([&] {
            editor.add_note("Data", {2, 2}, "Author", "Second");
        }), "injected classic note staging failure should escape as FastXlsxError");
    }
    check(editor.pending_worksheet_edits().front().classic_note_count == 1
            && editor.pending_change_count() == 1
            && editor.unsaved_change_count() == 1,
        "classic note staging failure should preserve package/public state");
    check(editor.last_edit_error().has_value(),
        "classic note staging failure should record a public diagnostic");

    editor.add_note("Data", {2, 2}, "Author", "Second");
    check(editor.pending_worksheet_edits().front().classic_note_count == 2
            && !editor.last_edit_error().has_value(),
        "classic note retry should regenerate both notes and clear the diagnostic");
    check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
        "worksheet removal should reject queued classic notes");
    check(editor.pending_worksheet_edits().front().classic_note_count == 2,
        "rejected worksheet removal should retain queued classic notes");

    const std::filesystem::path missing_parent = artifact(
        "fastxlsx-workbook-editor-classic-note-missing-parent");
    std::error_code error;
    std::filesystem::remove_all(missing_parent, error);
    check(!error, "failed to prepare classic note missing-parent save fixture");
    check(threw_fastxlsx_error([&] {
        editor.save_as(missing_parent / "child" / "output.xlsx");
    }), "classic note save should fail when the output parent is missing");
    check(editor.has_unsaved_changes() && editor.unsaved_change_count() == 2,
        "failed classic note save should retain the unsaved watermark");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-classic-note-retry-output.xlsx");
    editor.save_as(output);
    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "Data", summary);
    check(projected.size() == 2 && projected[0].text == "First"
            && projected[1].text == "Second",
        "classic note save retry should write the retained generated parts");
}

void test_same_session_relationship_replacement_composition()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-classic-note-composition-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-classic-note-composition-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_external_hyperlink("Data", {1, 1},
        "https://example.invalid/before-note");
    editor.add_note("Data", {2, 2}, "Patch", "Composed relationship state");
    editor.add_external_hyperlink("Data", {3, 3},
        "https://example.invalid/after-note");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1
            && summaries.front().external_hyperlink_count == 2
            && summaries.front().classic_note_count == 1,
        "same-session hyperlink/note composition should retain public diagnostics");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& relationships =
        entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(relationships,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/before-note" TargetMode="External"/>)",
        "classic note insertion should preserve an earlier staged hyperlink relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawing1.vml"/>)",
        "same-session note composition should retain the generated VML relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments" Target="../comments1.xml"/>)",
        "same-session note composition should retain the generated comments relationship");
    check_contains(relationships,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/after-note" TargetMode="External"/>)",
        "a later staged hyperlink should preserve generated note relationships");

    const std::string& worksheet = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet, R"(<hyperlink ref="A1" r:id="rId1"/>)",
        "earlier same-session hyperlink XML should be preserved");
    check_contains(worksheet, R"(<hyperlink ref="C3" r:id="rId4"/>)",
        "later same-session hyperlink XML should be appended");
    check(worksheet.find("</hyperlinks>")
            < worksheet.find(R"(<legacyDrawing r:id="rId2"/>)"),
        "same-session hyperlink/note composition should preserve worksheet suffix order");

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "Data", summary);
    check(projected.size() == 1
            && projected.front().text == "Composed relationship state",
        "same-session hyperlink/note output should reopen through the note reader");
}

void test_rejects_source_owned_comment_threaded_and_vml_state()
{
    const std::filesystem::path classic_source = write_source_owned_note_source(
        "fastxlsx-workbook-editor-classic-note-source-owned.xlsx");
    fastxlsx::WorkbookEditor classic_editor =
        fastxlsx::WorkbookEditor::open(classic_source);
    check(threw_fastxlsx_error([&] {
        classic_editor.add_note("Data", {2, 2}, "Patch", "Rejected");
    }), "Patch classic notes should reject source-owned comments/VML state");
    check(!classic_editor.has_pending_changes() && !classic_editor.has_unsaved_changes(),
        "source-owned classic note rejection should preserve clean editor state");

    const std::filesystem::path threaded_source =
        write_threaded_comment_relationship_source(
            "fastxlsx-workbook-editor-classic-note-threaded.xlsx");
    fastxlsx::WorkbookEditor threaded_editor =
        fastxlsx::WorkbookEditor::open(threaded_source);
    check(threw_fastxlsx_error([&] {
        threaded_editor.add_note("Data", {2, 2}, "Patch", "Rejected");
    }), "Patch classic notes should reject worksheet-local threaded comments");
    check(!threaded_editor.has_pending_changes(),
        "threaded comment rejection should not stage generated parts");

    const std::filesystem::path vml_source = write_header_footer_vml_source(
        "fastxlsx-workbook-editor-classic-note-header-footer-vml.xlsx");
    fastxlsx::WorkbookEditor vml_editor = fastxlsx::WorkbookEditor::open(vml_source);
    check(threw_fastxlsx_error([&] {
        vml_editor.add_note("Data", {2, 2}, "Patch", "Rejected");
    }), "Patch classic notes should reject source-owned header/footer VML");
    check(!vml_editor.has_pending_changes(),
        "source-owned VML rejection should preserve clean editor state");
}

void test_updates_removes_and_composes_canonical_source_notes()
{
    const std::filesystem::path source =
        write_canonical_note_edit_source("fastxlsx-workbook-editor-classic-note-source-edit.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-classic-note-source-edit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.update_note("Data", {2, 2}, "Updated & Author", " updated <text> ");
    editor.remove_note("Data", {1, 1});
    editor.add_note("Data", {4, 4}, "Patch", "Added after source takeover");
    editor.update_note("Data", {4, 4}, "Patch 2", "Updated added note");
    editor.rename_sheet("Data", "Renamed Data");

    const auto summaries = editor.pending_worksheet_edits();
    const auto* summary = find_summary(summaries, "Renamed Data");
    check(summary != nullptr && summary->renamed && summary->classic_note_count == 3
            && summary->classic_note_addition_count == 1 && summary->classic_note_update_count == 2
            && summary->classic_note_removal_count == 1,
        "source note diagnostics should retain final/add/update/remove counts "
        "across rename");
    check(editor.pending_change_count() == 5 && editor.unsaved_change_count() == 5,
        "source note mutations plus rename should advance public watermarks");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.at("custom/source-note-edit.bin") == "preserve source note unknown entry",
        "source note edit should preserve unknown package entries");
    check(entries.at("xl/worksheets/sheet2.xml") == source_entries.at("xl/worksheets/sheet2.xml"),
        "source note edit should preserve unrelated worksheet payloads");
    check_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "https://example.invalid/note-edit",
        "source note edit should preserve unrelated worksheet relationships");

    const std::string& comments = entries.at("xl/comments1.xml");
    check_not_contains(
        comments, R"(ref="A1")", "source note removal should remove the target comment");
    check_contains(comments,
        R"(<comment ref="B2" authorId="0"><text><t xml:space="preserve"> updated &lt;text&gt; </t></text></comment>)",
        "source note update should replace and escape author/text in retained "
        "order");
    check_contains(comments,
        R"(<comment ref="C3" authorId="1"><text><t>Third source note</t></text></comment>)",
        "source note edit should retain later source notes");
    check_contains(comments,
        R"(<comment ref="D4" authorId="2"><text><t>Updated added note</t></text></comment>)",
        "same-session add/update should compose after source ownership transfer");
    check_contains(comments,
        "<authors><author>Updated &amp; "
        "Author</author><author>Alice</author><author>Patch 2</author></authors>",
        "source note regeneration should rebuild authors in final first-use "
        "order");

    const std::string& vml = entries.at("xl/drawings/vmlDrawing1.vml");
    check(count_occurrences(vml, "<v:shape id=") == 3,
        "source note regeneration should retain one VML shape per final note");
    check_contains(vml, "<x:Row>1</x:Row><x:Column>1</x:Column>",
        "updated source note should retain its VML coordinate");
    check_contains(vml, "<x:Row>3</x:Row><x:Column>3</x:Column>",
        "same-session added note should receive a canonical VML coordinate");

    fastxlsx::WorksheetCommentReadSummary read_summary;
    const auto notes = read_notes(output, "Renamed Data", read_summary);
    check(notes.size() == 3 && notes[0].row == 2 && notes[0].column == 2
            && notes[0].author == "Updated & Author" && notes[0].text == " updated <text> "
            && notes[1].row == 3 && notes[2].row == 4,
        "source note edits should reopen in final retained order");
}

void test_update_noop_missing_targets_and_canonical_rejection()
{
    const std::filesystem::path source =
        write_source_owned_note_source("fastxlsx-workbook-editor-classic-note-source-noop.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.remove_note("Data", {2, 2});
    }),
        "classic note removal should reject a missing target");
    check(editor.last_edit_error().has_value() && !editor.has_pending_changes(),
        "missing source note removal should preserve clean state and record an "
        "error");
    editor.update_note("Data", {1, 1}, "Source", "Owned by the source package");
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes()
            && !editor.last_edit_error().has_value(),
        "identical source note update should be a clean no-op and clear the "
        "error");
    check(threw_fastxlsx_error([&] {
        editor.update_note("Data", {2, 2}, "Missing", "Missing");
    }),
        "classic note update should reject a missing target");
    check(threw_fastxlsx_error([&] {
        editor.update_note("Data", {1, 1}, "", "Invalid");
    }),
        "classic note update should reject an empty author");
    check(!editor.has_pending_changes(), "invalid source note updates should not publish state");

    auto comments_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(comments_entries.at("xl/comments1.xml"), "</comments>", " </comments>");
    const std::filesystem::path noncanonical_comments =
        artifact("fastxlsx-workbook-editor-classic-note-noncanonical-comments.xlsx");
    fastxlsx::test::write_stored_zip_entries(noncanonical_comments, comments_entries);
    fastxlsx::WorkbookEditor comments_editor =
        fastxlsx::WorkbookEditor::open(noncanonical_comments);
    check(threw_fastxlsx_error([&] {
        comments_editor.update_note("Data", {1, 1}, "Source", "Changed");
    }),
        "source note update should reject non-canonical comments bytes");
    check(!comments_editor.has_pending_changes(),
        "non-canonical comments rejection should preserve clean state");

    auto vml_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(
        vml_entries.at("xl/drawings/vmlDrawing1.vml"), "visibility:hidden", "visibility:visible");
    const std::filesystem::path noncanonical_vml =
        artifact("fastxlsx-workbook-editor-classic-note-noncanonical-vml.xlsx");
    fastxlsx::test::write_stored_zip_entries(noncanonical_vml, vml_entries);
    fastxlsx::WorkbookEditor vml_editor = fastxlsx::WorkbookEditor::open(noncanonical_vml);
    check(threw_fastxlsx_error([&] {
        vml_editor.remove_note("Data", {1, 1});
    }),
        "source note removal should reject non-canonical VML bytes");
    check(!vml_editor.has_pending_changes(),
        "non-canonical VML rejection should preserve clean state");

    const std::filesystem::path shared_source =
        artifact("fastxlsx-workbook-editor-classic-note-shared-parts.xlsx");
    fastxlsx::WorkbookWriter shared_writer = fastxlsx::WorkbookWriter::create(shared_source);
    auto shared_data = shared_writer.add_worksheet("Data");
    shared_data.add_note(1, 1, "Data", "Target note");
    auto shared_other = shared_writer.add_worksheet("Other");
    shared_other.add_note(1, 1, "Other", "Shared target");
    shared_writer.close();
    auto shared_entries = fastxlsx::test::read_zip_entries(shared_source);
    replace_first_or_throw(shared_entries.at("xl/worksheets/_rels/sheet2.xml.rels"),
        "../comments2.xml", "../%63omments1.xml#shared");
    replace_first_or_throw(shared_entries.at("xl/worksheets/_rels/sheet2.xml.rels"),
        "../drawings/vmlDrawing2.vml", "../drawings/%76mlDrawing1.vml?shared=1");
    fastxlsx::test::write_stored_zip_entries(shared_source, shared_entries);
    fastxlsx::WorkbookEditor shared_editor = fastxlsx::WorkbookEditor::open(shared_source);
    check(threw_fastxlsx_error([&] {
        shared_editor.update_note("Data", {1, 1}, "Data", "Rejected shared edit");
    }),
        "source note update should reject URI-aliased comments/VML parts shared by another worksheet");
    check(!shared_editor.has_pending_changes(),
        "shared classic note part rejection should preserve clean state");

    auto root_shared_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(root_shared_entries.at("_rels/.rels"), "</Relationships>",
        R"(<Relationship Id="rIdSharedClassicNote" Type="urn:fastxlsx:test:shared-classic-note" Target="xl/%63omments1.xml#shared"/></Relationships>)");
    const std::filesystem::path root_shared_source =
        artifact("fastxlsx-workbook-editor-classic-note-root-shared-part.xlsx");
    fastxlsx::test::write_stored_zip_entries(root_shared_source, root_shared_entries);
    fastxlsx::WorkbookEditor root_shared_editor =
        fastxlsx::WorkbookEditor::open(root_shared_source);
    check(threw_fastxlsx_error([&] {
        root_shared_editor.remove_note("Data", {1, 1});
    }),
        "source note removal should reject URI-aliased comments/VML parts referenced by the package root");
    check(!root_shared_editor.has_pending_changes(),
        "package-root classic note part rejection should preserve clean state");
}

void test_final_note_removal_cleanup_and_retry()
{
    const std::filesystem::path source =
        write_source_owned_note_source("fastxlsx-workbook-editor-classic-note-final-remove.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-classic-note-final-remove-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_classic_note_staging);
        check(threw_fastxlsx_error([&] {
            editor.remove_note("Data", {1, 1});
        }),
            "injected final note removal failure should escape");
    }
    check(!editor.has_pending_changes() && !editor.has_unsaved_changes(),
        "failed final note removal should preserve clean package/public state");

    editor.remove_note("Data", {1, 1});
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().classic_note_count == 0
            && summaries.front().classic_note_addition_count == 0
            && summaries.front().classic_note_update_count == 0
            && summaries.front().classic_note_removal_count == 1,
        "final note removal should retain a zero-final-count diagnostic");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(!entries.contains("xl/comments1.xml") && !entries.contains("xl/drawings/vmlDrawing1.vml"),
        "final note removal should omit owned comments/VML parts");
    check_not_contains(entries.at("xl/worksheets/sheet1.xml"), "<legacyDrawing",
        "final note removal should remove worksheet legacyDrawing metadata");
    check_not_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        std::string(comments_relationship_type),
        "final note removal should remove the comments relationship");
    check_not_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        std::string(vml_relationship_type),
        "final note removal should remove the VML relationship");
    check_not_contains(entries.at("[Content_Types].xml"), std::string(comments_content_type),
        "final note removal should remove the comments content type");
    check_not_contains(entries.at("[Content_Types].xml"), std::string(vml_content_type),
        "final note removal should remove an otherwise-unused VML default");

    fastxlsx::WorksheetCommentReadSummary empty_summary;
    check(read_notes(output, "Data", empty_summary).empty() && empty_summary.comment_count == 0
            && !empty_summary.has_legacy_drawing,
        "final note removal output should reopen without classic notes");

    editor.add_note("Data", {2, 2}, "Again", "Added after final removal");
    editor.update_note("Data", {2, 2}, "Again", "Updated after final removal");
    const std::filesystem::path composed_output =
        artifact("fastxlsx-workbook-editor-classic-note-final-remove-compose-output.xlsx");
    editor.save_as(composed_output);
    fastxlsx::WorksheetCommentReadSummary composed_summary;
    const auto composed_notes = read_notes(composed_output, "Data", composed_summary);
    check(
        composed_notes.size() == 1 && composed_notes.front().text == "Updated after final removal",
        "same-session add/update should work after removing the final source "
        "note");
    const auto composed_edits = editor.pending_worksheet_edits();
    check(composed_edits.front().classic_note_count == 1
            && composed_edits.front().classic_note_addition_count == 1
            && composed_edits.front().classic_note_update_count == 1
            && composed_edits.front().classic_note_removal_count == 1,
        "post-removal composition should retain cumulative note diagnostics");
}

void test_final_note_removal_preserves_other_vml_parts()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-classic-note-other-vml-source.xlsx");
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
    auto data = writer.add_worksheet("Data");
    data.add_note(1, 1, "Data", "Remove me");
    auto other = writer.add_worksheet("Other");
    other.add_note(2, 2, "Other", "Preserve me");
    writer.close();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.remove_note("Data", {1, 1});
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-classic-note-other-vml-output.xlsx");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(!entries.contains("xl/comments1.xml") && !entries.contains("xl/drawings/vmlDrawing1.vml")
            && entries.contains("xl/comments2.xml")
            && entries.contains("xl/drawings/vmlDrawing2.vml"),
        "final note removal should omit only the target worksheet note parts");
    check_contains(entries.at("[Content_Types].xml"), std::string(vml_content_type),
        "final note removal should retain the VML default for another VML part");
    fastxlsx::WorksheetCommentReadSummary summary;
    const auto notes = read_notes(output, "Other", summary);
    check(notes.size() == 1 && notes.front().text == "Preserve me",
        "final note removal should preserve another worksheet classic note");
}

void test_part_numbering_rename_and_added_worksheet()
{
    const std::filesystem::path source = artifact(
        "fastxlsx-workbook-editor-classic-note-numbering-source.xlsx");
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("plain")});
    auto existing_notes = writer.add_worksheet("ExistingNotes");
    existing_notes.add_note(1, 1, "Source", "Consumes index one");
    writer.close();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_note("Data", {2, 2}, "Patch", "Uses index two");
    editor.rename_sheet("Data", "Renamed Data");
    editor.add_note("Renamed Data", {4, 4}, "Patch", "Added after rename");
    editor.add_worksheet("Added");
    editor.add_note("Added", {3, 3}, "Patch", "Uses index three");
    editor.rename_sheet("Added", "Renamed Added");

    const auto summaries = editor.pending_worksheet_edits();
    const auto* renamed_data = find_summary(summaries, "Renamed Data");
    const auto* renamed_added = find_summary(summaries, "Renamed Added");
    check(renamed_data != nullptr && renamed_data->renamed
            && renamed_data->classic_note_count == 2,
        "classic note diagnostics should migrate with a planned source-sheet rename");
    check(renamed_added != nullptr && renamed_added->added
            && renamed_added->classic_note_count == 1,
        "classic note diagnostics should migrate with an added-sheet rename");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-classic-note-numbering-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.contains("xl/comments1.xml") && entries.contains("xl/comments2.xml")
            && entries.contains("xl/comments3.xml")
            && !entries.contains("xl/comments4.xml"),
        "classic note Patch should allocate compact comments part numbers");
    check(entries.contains("xl/drawings/vmlDrawing1.vml")
            && entries.contains("xl/drawings/vmlDrawing2.vml")
            && entries.contains("xl/drawings/vmlDrawing3.vml")
            && !entries.contains("xl/drawings/vmlDrawing4.vml"),
        "classic note Patch should allocate compact VML part numbers");
    check_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        R"(Target="../comments2.xml")",
        "source worksheet Patch note should use comments index two");
    check_contains(entries.at("xl/worksheets/_rels/sheet3.xml.rels"),
        R"(Target="../comments3.xml")",
        "same-session added worksheet Patch note should use comments index three");
    check_contains(entries.at("xl/workbook.xml"), R"(name="Renamed Data")",
        "classic note save should compose with a source-sheet rename");
    check_contains(entries.at("xl/workbook.xml"), R"(name="Renamed Added")",
        "classic note save should compose with an added-sheet rename");

    fastxlsx::WorksheetCommentReadSummary data_summary;
    const auto data_notes = read_notes(output, "Renamed Data", data_summary);
    fastxlsx::WorksheetCommentReadSummary added_summary;
    const auto added_notes = read_notes(output, "Renamed Added", added_summary);
    check(data_notes.size() == 2 && data_notes.front().text == "Uses index two"
            && data_notes.back().text == "Added after rename",
        "renamed source worksheet classic notes should regenerate and reopen");
    check(added_notes.size() == 1 && added_notes.front().text == "Uses index three",
        "renamed added worksheet classic note should reopen");
}

void test_production_deflate_round_trip()
{
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-classic-note-deflate-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-classic-note-deflate-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_note("Data", {4, 2}, "Deflate", "Production backend note");
    fastxlsx::WorkbookEditorSaveOptions options;
    options.zip_compression_level = 6;
    editor.save_as(output, options);

    fastxlsx::WorksheetCommentReadSummary summary;
    const auto projected = read_notes(output, "Data", summary);
    check(projected.size() == 1 && projected.front().row == 4
            && projected.front().column == 2
            && projected.front().text == "Production backend note",
        "DEFLATE Patch classic note should reopen through the bounded reader");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data"),
        "DEFLATE Patch classic note output should reopen through WorkbookEditor");
    reopened.update_note("Data", {4, 2}, "Deflate", "Updated canonical source note");
    const std::filesystem::path updated_output =
        artifact("fastxlsx-workbook-editor-classic-note-deflate-source-edit-output.xlsx");
    reopened.save_as(updated_output, options);
    fastxlsx::WorksheetCommentReadSummary updated_summary;
    const auto updated_notes = read_notes(updated_output, "Data", updated_summary);
    check(
        updated_notes.size() == 1 && updated_notes.front().text == "Updated canonical source note",
        "DEFLATE canonical source note should support transactional update and "
        "reopen");
#endif
}

} // namespace

int main()
{
    try {
        test_adds_notes_preserves_package_and_round_trips();
        test_validation_transaction_failure_and_retry();
        test_same_session_relationship_replacement_composition();
        test_rejects_source_owned_comment_threaded_and_vml_state();
        test_updates_removes_and_composes_canonical_source_notes();
        test_update_noop_missing_targets_and_canonical_rejection();
        test_final_note_removal_cleanup_and_retry();
        test_final_note_removal_preserves_other_vml_parts();
        test_part_numbering_rename_and_added_worksheet();
        test_production_deflate_round_trip();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor classic note check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All WorkbookEditor classic note tests passed\n");
    return 0;
}
