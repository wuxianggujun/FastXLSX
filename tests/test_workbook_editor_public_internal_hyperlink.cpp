#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

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

void fail_after_internal_hyperlink_staging()
{
    throw fastxlsx::FastXlsxError("injected internal hyperlink staging failure");
}

std::size_t count_occurrences(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while (true) {
        const std::size_t found = text.find(needle, offset);
        if (found == std::string_view::npos) {
            return count;
        }
        ++count;
        offset = found + needle.size();
    }
}

std::filesystem::path write_source_with_external_and_internal_hyperlinks(
    std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("source")});
    data.add_external_hyperlink(1, 1, "https://example.invalid/source");
    data.add_internal_hyperlink(1, 2, "Untouched!A1");
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

void test_internal_hyperlink_insert_append_and_escape()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-source.xlsx");
    std::map<std::string, std::string> source_entries =
        fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/opaque.bin", "internal hyperlink unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::HyperlinkOptions options;
    options.display = "Display & \"quoted\"";
    options.tooltip = "Tip 'one'";
    editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {1, 1},
        "'Untouched & <One>'!A1", options);
    editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {2, 2}, "Untouched!B2");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().internal_hyperlink_count == 2,
        "internal hyperlink edits should expose a planned worksheet count");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<hyperlinks><hyperlink ref="A1" location="&apos;Untouched &amp; &lt;One&gt;&apos;!A1" display="Display &amp; &quot;quoted&quot;" tooltip="Tip &apos;one&apos;"/><hyperlink ref="B2" location="Untouched!B2"/></hyperlinks>)",
        "internal hyperlink insertion should escape attributes and append in order");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "internal hyperlinks should not create worksheet relationships");
    check(entries.at("custom/opaque.bin") == "internal hyperlink unknown entry",
        "internal hyperlink edits should preserve unknown entries");

    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-reopened-output.xlsx");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    reopened.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {3, 3}, "Untouched!C3");
    reopened.save_as(reopened_output);
    const auto reopened_entries = fastxlsx::test::read_zip_entries(reopened_output);
    check(count_occurrences(reopened_entries.at("xl/worksheets/sheet1.xml"), "<hyperlink ")
            == 3,
        "reopened internal hyperlink metadata should append to the existing container");
}

void test_internal_hyperlink_preserves_existing_relationships_and_self_closing_container()
{
    const std::filesystem::path source = write_source_with_external_and_internal_hyperlinks(
        "fastxlsx-workbook-editor-internal-hyperlink-existing-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-existing-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {1, 3}, "Untouched!C3");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<hyperlink ref="A1" r:id="rId1"/>)",
        "existing external hyperlink XML should remain unchanged");
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<hyperlink ref="B1" location="Untouched!A1"/>)",
        "existing internal hyperlink XML should remain unchanged");
    check_contains(entries.at("xl/worksheets/sheet1.xml"),
        R"(<hyperlink ref="C1" location="Untouched!C3"/>)",
        "new internal hyperlink should append after existing metadata");
    check_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        R"(Target="https://example.invalid/source")",
        "existing external hyperlink relationship should be preserved");
    check(entries.at("xl/worksheets/_rels/sheet1.xml.rels")
            .find("Target=\"Untouched!C3\"")
            == std::string::npos,
        "internal hyperlink location should not be added to worksheet relationships");

    const std::filesystem::path self_closing_source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-self-closing-source.xlsx");
    auto self_closing_entries = fastxlsx::test::read_zip_entries(self_closing_source);
    replace_first_or_throw(self_closing_entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>", "</sheetData><hyperlinks/>");
    fastxlsx::test::write_stored_zip_entries(self_closing_source, self_closing_entries);
    const std::filesystem::path self_closing_output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-self-closing-output.xlsx");
    fastxlsx::WorkbookEditor self_closing_editor =
        fastxlsx::WorkbookEditor::open(self_closing_source);
    self_closing_editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {4, 4}, "Untouched!D4");
    self_closing_editor.save_as(self_closing_output);
    check_contains(fastxlsx::test::read_zip_entries(self_closing_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<hyperlinks><hyperlink ref="D4" location="Untouched!D4"/></hyperlinks>)",
        "self-closing hyperlinks metadata should expand before appending");
}

void test_internal_hyperlink_failure_state_and_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-failure-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {1, 1}, "Untouched!A1");
    const auto before = editor.pending_worksheet_edits();
    check(threw_fastxlsx_error([&] {
        editor.add_internal_hyperlink(
            "Data", fastxlsx::WorksheetCellReference {1, 1}, "Untouched!A2");
    }), "duplicate internal hyperlink target should be rejected");
    check(editor.pending_worksheet_edits().size() == before.size()
            && editor.pending_worksheet_edits().front().internal_hyperlink_count == 1,
        "duplicate hyperlink rejection should preserve public diagnostics");
    check(threw_fastxlsx_error([&] {
        editor.add_internal_hyperlink(
            "Data", fastxlsx::WorksheetCellReference {0, 1}, "Untouched!A3");
    }), "invalid internal hyperlink coordinate should be rejected");
    check(threw_fastxlsx_error([&] {
        editor.add_internal_hyperlink(
            "Data", fastxlsx::WorksheetCellReference {2, 1}, "");
    }), "empty internal hyperlink location should be rejected");

    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_internal_hyperlink_staging);
        check(threw_fastxlsx_error([&] {
            editor.add_internal_hyperlink(
                "Data", fastxlsx::WorksheetCellReference {2, 2}, "Untouched!B2");
        }), "injected hyperlink staging failure should escape as FastXlsxError");
    }
    check(editor.pending_worksheet_edits().front().internal_hyperlink_count == 1,
        "staged hyperlink failure should not publish a second hyperlink diagnostic");
    editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {2, 2}, "Untouched!B2");
    check(!editor.last_edit_error().has_value(),
        "successful hyperlink retry should clear the previous diagnostic");

    const std::filesystem::path retry_output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-retry-output.xlsx");
    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-internal-hyperlink-missing")
            / "parent" / "output.xlsx");
    }), "hyperlink save should fail when the output parent is missing");
    check(editor.has_unsaved_changes() && editor.pending_change_count() == 2,
        "failed hyperlink save should retain retry state");
    editor.save_as(retry_output);
    check(count_occurrences(fastxlsx::test::read_zip_entries(retry_output)
            .at("xl/worksheets/sheet1.xml"), "<hyperlink ") == 2,
        "hyperlink save retry should emit both staged links");
}

void test_internal_hyperlink_range_and_suffix_guards()
{
    const std::filesystem::path range_source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-range-source.xlsx");
    auto range_entries = fastxlsx::test::read_zip_entries(range_source);
    replace_first_or_throw(range_entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>",
        R"(</sheetData><hyperlinks><hyperlink ref="A1:B2" location="Untouched!A1"/></hyperlinks>)");
    fastxlsx::test::write_stored_zip_entries(range_source, range_entries);
    fastxlsx::WorkbookEditor range_editor = fastxlsx::WorkbookEditor::open(range_source);
    check(threw_fastxlsx_error([&] {
        range_editor.add_internal_hyperlink(
            "Data", fastxlsx::WorksheetCellReference {2, 2}, "Untouched!B2");
    }), "internal hyperlink should reject a target covered by an existing range ref");
    check(!range_editor.has_pending_changes() && !range_editor.has_unsaved_changes(),
        "range-overlap rejection should not publish editor state");

    const std::filesystem::path suffix_source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-suffix-source.xlsx");
    auto suffix_entries = fastxlsx::test::read_zip_entries(suffix_source);
    replace_first_or_throw(suffix_entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>",
        R"(</sheetData><pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)");
    fastxlsx::test::write_stored_zip_entries(suffix_source, suffix_entries);
    const std::filesystem::path suffix_output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-suffix-output.xlsx");
    fastxlsx::WorkbookEditor suffix_editor = fastxlsx::WorkbookEditor::open(suffix_source);
    suffix_editor.add_internal_hyperlink(
        "Data", fastxlsx::WorksheetCellReference {3, 3}, "Untouched!C3");
    suffix_editor.save_as(suffix_output);
    check_contains(fastxlsx::test::read_zip_entries(suffix_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<hyperlinks><hyperlink ref="C3" location="Untouched!C3"/></hyperlinks><pageMargins )",
        "internal hyperlink container should be inserted before later schema metadata");

    const std::filesystem::path unknown_source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-unknown-suffix-source.xlsx");
    auto unknown_entries = fastxlsx::test::read_zip_entries(unknown_source);
    replace_first_or_throw(unknown_entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>", "</sheetData><futureMetadata/>");
    fastxlsx::test::write_stored_zip_entries(unknown_source, unknown_entries);
    fastxlsx::WorkbookEditor unknown_editor = fastxlsx::WorkbookEditor::open(unknown_source);
    check(threw_fastxlsx_error([&] {
        unknown_editor.add_internal_hyperlink(
            "Data", fastxlsx::WorksheetCellReference {4, 4}, "Untouched!D4");
    }), "unknown worksheet suffix metadata should fail before hyperlink staging");
    check(!unknown_editor.has_pending_changes() && !unknown_editor.has_unsaved_changes(),
        "unknown suffix rejection should preserve editor state");
}

void test_internal_hyperlink_targets_renamed_added_worksheet()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-internal-hyperlink-added-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-internal-hyperlink-added-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_worksheet("Added");
    editor.add_internal_hyperlink(
        "Added", fastxlsx::WorksheetCellReference {1, 1}, "Data!A1");
    editor.rename_sheet("Added", "Renamed Added");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().planned_name == "Renamed Added"
            && summaries.front().internal_hyperlink_count == 1,
        "renaming an added worksheet should migrate hyperlink diagnostics");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<hyperlinks><hyperlink ref="A1" location="Data!A1"/></hyperlinks>)",
        "internal hyperlink should target a worksheet added in the same editor");
    check_contains(entries.at("xl/workbook.xml"), R"(name="Renamed Added")",
        "added worksheet rename should compose with hyperlink staging");
}

} // namespace

int main()
{
    try {
        test_internal_hyperlink_insert_append_and_escape();
        test_internal_hyperlink_preserves_existing_relationships_and_self_closing_container();
        test_internal_hyperlink_failure_state_and_retry();
        test_internal_hyperlink_range_and_suffix_guards();
        test_internal_hyperlink_targets_renamed_added_worksheet();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d internal hyperlink check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All WorkbookEditor internal hyperlink tests passed\n");
    return 0;
}
