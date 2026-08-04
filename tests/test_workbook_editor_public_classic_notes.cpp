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
