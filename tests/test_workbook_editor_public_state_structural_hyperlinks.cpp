#include "../src/package_editor.hpp"
#include "test_workbook_editor_public_state_shifts_support.hpp"

namespace {

class ScopedStructuralHyperlinkStagedHook {
public:
    explicit ScopedStructuralHyperlinkStagedHook(
        fastxlsx::detail::PackageEditorWorksheetPartReplacementStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            hook);
    }

    ~ScopedStructuralHyperlinkStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            nullptr);
    }

    ScopedStructuralHyperlinkStagedHook(
        const ScopedStructuralHyperlinkStagedHook&) = delete;
    ScopedStructuralHyperlinkStagedHook& operator=(
        const ScopedStructuralHyperlinkStagedHook&) = delete;
};

void fail_after_structural_hyperlink_staging()
{
    throw fastxlsx::FastXlsxError(
        "injected structural hyperlink staging failure");
}

void test_insertions_compose_source_and_same_session_hyperlinks()
{
    fastxlsx::HyperlinkOptions external_options;
    external_options.display = "External <display>";
    external_options.tooltip = "External \"tip\"";
    fastxlsx::HyperlinkOptions internal_options;
    internal_options.display = "Internal & display";
    internal_options.tooltip = "Internal 'tip'";
    const std::array<StructuralHyperlinkFixture, 3> source_hyperlinks {{
        {{2, 2, 4, 3}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!A1", internal_options},
        {{1, 5, 2, 6}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/source-shift", external_options},
        {{6, 1, 7, 1}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!A2", {}},
    }};
    const std::filesystem::path source = write_two_sheet_source_with_hyperlinks(
        "fastxlsx-workbook-editor-structural-hyperlink-insert-source.xlsx",
        source_hyperlinks);
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace(
        "custom/structural-hyperlink.bin", "preserve-hyperlink-insert");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);
    source_entries = fastxlsx::test::read_zip_entries(source);
    const std::filesystem::path missing_output = artifact(
        "missing-structural-hyperlink-insert-parent/output.xlsx");
    std::error_code ignored;
    std::filesystem::remove_all(missing_output.parent_path(), ignored);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-structural-hyperlink-insert-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::HyperlinkOptions patch_internal_options;
    patch_internal_options.display = "Patch internal";
    patch_internal_options.tooltip = "Patch internal tip";
    fastxlsx::HyperlinkOptions patch_external_options;
    patch_external_options.display = "Patch external";
    patch_external_options.tooltip = "Patch external tip";
    editor.add_internal_hyperlink("Data", {4, 8}, "Untouched!H4",
        patch_internal_options);
    editor.add_external_hyperlink("Data", {5, 10},
        "https://example.invalid/patch", patch_external_options);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.insert_rows(3, 2);
    sheet.insert_columns(3, 1);

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1
            && summaries.front().internal_hyperlink_count == 1
            && summaries.front().external_hyperlink_count == 1
            && summaries.front().materialized_dirty,
        "structural hyperlink insertion should retain Patch diagnostics and dirty state");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 3,
        "hyperlink insertion should combine two Patch edits with one dirty session");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_output); }),
        "hyperlink insertion save should fail for a missing output parent");
    check(sheet.has_pending_changes() && editor.has_unsaved_changes()
            && editor.unsaved_change_count() == 3,
        "failed hyperlink insertion save should preserve retry state");
    editor.save_as(output);

    // Streaming emits source external links before source internal links;
    // same-session Patch links retain their append order after that source data.
    const std::array<StructuralHyperlinkFixture, 5> expected_hyperlinks {{
        {{1, 6, 2, 7}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/source-shift", external_options},
        {{2, 2, 6, 4}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!A1", internal_options},
        {{8, 1, 9, 1}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!A2", {}},
        {{6, 9, 6, 9}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!H4", patch_internal_options},
        {{7, 11, 7, 11}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/patch", patch_external_options},
    }};
    check_hyperlink_views_equal(read_worksheet_hyperlink_views(output),
        expected_hyperlinks,
        "source and same-session hyperlink insertion output");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("custom/structural-hyperlink.bin")
            == "preserve-hyperlink-insert",
        "hyperlink insertion should preserve unknown package entries");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "hyperlink insertion should preserve the untouched worksheet");
    check_contains(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "https://example.invalid/source-shift",
        "hyperlink insertion should preserve the source external relationship");
    check_contains(output_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "https://example.invalid/patch",
        "hyperlink insertion should preserve the same-session relationship");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "hyperlink insertion save should not modify the source package");
}

void test_prefixed_empty_store_staging_failure_retries()
{
    const std::array<StructuralHyperlinkFixture, 1> source_hyperlinks {{
        {{2, 2, 2, 2}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/prefixed", {}},
    }};
    const std::filesystem::path source = write_two_sheet_source_with_hyperlinks(
        "fastxlsx-workbook-editor-structural-hyperlink-prefixed-source.xlsx",
        source_hyperlinks);
    auto entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/worksheets/sheet1.xml") =
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><x:sheetData></x:sheetData><x:hyperlinks><x:hyperlink tooltip="Tip &amp; more" rel:id='rId1' ref='B2:C3' display="Display &quot;one&quot;"/></x:hyperlinks></x:worksheet>)";
    fastxlsx::test::write_stored_zip_entries(source, entries);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-structural-hyperlink-prefixed-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(sheet.cell_count() == 0 && !sheet.has_pending_changes(),
        "prefixed hyperlink fixture should materialize an empty CellStore");
    {
        const ScopedStructuralHyperlinkStagedHook hook(
            fail_after_structural_hyperlink_staging);
        check(threw_fastxlsx_error([&] { sheet.insert_rows(3, 1); }),
            "injected hyperlink staging failure should surface publicly");
    }
    check(!sheet.has_pending_changes() && !editor.has_pending_changes()
            && !editor.has_unsaved_changes() && sheet.cell_count() == 0,
        "hyperlink staging failure should preserve package and empty CellStore state");
    check(editor.last_edit_error().has_value(),
        "hyperlink staging failure should retain a public diagnostic");

    sheet.insert_rows(3, 1);
    sheet.insert_columns(2, 1);
    check(sheet.has_pending_changes() && !editor.last_edit_error().has_value(),
        "hyperlink staging retry should succeed exactly once");
    editor.save_as(output);

    const std::string worksheet_xml =
        fastxlsx::test::read_zip_entries(output).at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "<x:sheetData xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">",
        "prefixed empty CellStore output should retain the SpreadsheetML namespace");
    check_contains(worksheet_xml,
        "<x:hyperlink tooltip=\"Tip &amp; more\" rel:id='rId1' "
        "ref='C2:D4' display=\"Display &quot;one&quot;\"/>",
        "hyperlink rewrite should preserve QName, attribute order, quotes, and options");
    fastxlsx::HyperlinkOptions expected_options;
    expected_options.display = "Display \"one\"";
    expected_options.tooltip = "Tip & more";
    const std::array<StructuralHyperlinkFixture, 1> expected_hyperlinks {{
        {{2, 3, 4, 4}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/prefixed", expected_options},
    }};
    check_hyperlink_views_equal(read_worksheet_hyperlink_views(output),
        expected_hyperlinks, "prefixed empty CellStore hyperlink output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(reopened_sheet.cell_count() == 0 && !reopened_sheet.has_pending_changes(),
        "prefixed hyperlink output should reopen as a clean empty CellStore");
}

void test_deletions_translate_hyperlinks_and_manage_relationships()
{
    fastxlsx::HyperlinkOptions clipped_options;
    clipped_options.display = "Clipped internal";
    clipped_options.tooltip = "Keep & clip";
    fastxlsx::HyperlinkOptions shifted_options;
    shifted_options.display = "Shifted external";
    shifted_options.tooltip = "Keep relationship";
    fastxlsx::HyperlinkOptions shared_survivor_options;
    shared_survivor_options.display = "Shared survivor";
    shared_survivor_options.tooltip = "Reuse rId4";
    fastxlsx::HyperlinkOptions single_cell_options;
    single_cell_options.display = "Single cell";
    const std::array<StructuralHyperlinkFixture, 8> source_hyperlinks {{
        {{2, 2, 5, 4}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!B2", clipped_options},
        {{1, 6, 2, 7}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/column-shift", shifted_options},
        {{7, 1, 8, 1}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/row-shift", {}},
        {{3, 8, 4, 9}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!H3", {}},
        {{3, 11, 4, 12}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/orphan", {}},
        {{3, 14, 4, 15}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/shared", {}},
        {{6, 17, 7, 18}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/discarded-shared-target",
            shared_survivor_options},
        {{2, 20, 3, 20}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!T2", single_cell_options},
    }};
    const std::filesystem::path source = write_two_sheet_source_with_hyperlinks(
        "fastxlsx-workbook-editor-structural-hyperlink-delete-source.xlsx",
        source_hyperlinks);
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    replace_first_or_throw(source_entries.at("xl/worksheets/sheet1.xml"),
        R"(ref="Q6:R7" r:id="rId5")",
        R"(ref="Q6:R7" r:id="rId4")");
    replace_first_or_throw(
        source_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.invalid/discarded-shared-target" TargetMode="External"/>)",
        "");
    source_entries.emplace(
        "custom/structural-hyperlink.bin", "preserve-hyperlink-delete");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);
    source_entries = fastxlsx::test::read_zip_entries(source);
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-structural-hyperlink-delete-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.delete_rows(3, 2);
    sheet.delete_columns(3, 1);
    editor.save_as(output);

    const std::array<StructuralHyperlinkFixture, 5> expected_hyperlinks {{
        {{1, 5, 2, 6}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/column-shift", shifted_options},
        {{5, 1, 6, 1}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/row-shift", {}},
        {{4, 16, 5, 17}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/shared", shared_survivor_options},
        {{2, 2, 3, 3}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!B2", clipped_options},
        {{2, 19, 2, 19}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!T2", single_cell_options},
    }};
    check_hyperlink_views_equal(read_worksheet_hyperlink_views(output),
        expected_hyperlinks, "hyperlink row/column deletion output");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string& output_relationships =
        output_entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_not_contains(output_relationships, "Id=\"rId3\"",
        "fully removed external hyperlink should remove its exclusive relationship");
    check_contains(output_relationships, "Id=\"rId4\"",
        "a surviving hyperlink should retain its shared relationship id");
    check_not_contains(output_relationships, "Id=\"rId5\"",
        "shared-id fixture should not recreate the discarded relationship");
    check(output_entries.at("custom/structural-hyperlink.bin")
            == "preserve-hyperlink-delete",
        "hyperlink deletion should preserve unknown package entries");
    check(output_entries.at("xl/worksheets/sheet2.xml")
            == source_entries.at("xl/worksheets/sheet2.xml"),
        "hyperlink deletion should preserve the untouched worksheet");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "hyperlink deletion save should not modify the source package");
}

void test_removal_guards_and_final_container_cleanup()
{
    const std::array<StructuralHyperlinkFixture, 1> final_hyperlink {{
        {{2, 2, 3, 3}, fastxlsx::WorksheetHyperlinkKind::External,
            "https://example.invalid/final", {}},
    }};
    const std::filesystem::path final_source = write_two_sheet_source_with_hyperlinks(
        "fastxlsx-workbook-editor-structural-hyperlink-final-source.xlsx",
        final_hyperlink);
    const std::filesystem::path final_output = artifact(
        "fastxlsx-workbook-editor-structural-hyperlink-final-output.xlsx");
    fastxlsx::WorkbookEditor final_editor =
        fastxlsx::WorkbookEditor::open(final_source);
    fastxlsx::WorksheetEditor final_sheet = final_editor.worksheet("Data");
    final_sheet.delete_rows(2, 2);
    final_editor.save_as(final_output);
    const auto final_entries = fastxlsx::test::read_zip_entries(final_output);
    check(read_worksheet_hyperlink_views(final_output).empty(),
        "deleting the final hyperlink should leave no reader projection");
    check_not_contains(final_entries.at("xl/worksheets/sheet1.xml"),
        "<hyperlinks",
        "deleting the final hyperlink should remove its complete container");
    check_not_contains(final_entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        "https://example.invalid/final",
        "deleting the final external hyperlink should remove its relationship");

    const std::filesystem::path owner_source = write_two_sheet_source_with_hyperlinks(
        "fastxlsx-workbook-editor-structural-hyperlink-owner-source.xlsx",
        final_hyperlink);
    auto owner_entries = fastxlsx::test::read_zip_entries(owner_source);
    replace_first_or_throw(owner_entries.at("xl/worksheets/sheet1.xml"),
        "</hyperlinks>", "</hyperlinks><drawing r:id=\"rId1\"/>");
    fastxlsx::test::write_stored_zip_entries(owner_source, owner_entries);
    owner_entries = fastxlsx::test::read_zip_entries(owner_source);
    fastxlsx::WorkbookEditor owner_editor =
        fastxlsx::WorkbookEditor::open(owner_source);
    fastxlsx::WorksheetEditor owner_sheet = owner_editor.worksheet("Data");
    check(threw_fastxlsx_error([&] { owner_sheet.delete_rows(2, 2); }),
        "a non-hyperlink element reusing the id should reject relationship removal");
    check(!owner_sheet.has_pending_changes() && !owner_editor.has_pending_changes()
            && !owner_editor.has_unsaved_changes()
            && owner_sheet.get_cell("A2").text_value() == "placeholder-a2",
        "ambiguous hyperlink ownership should preserve cells and public state");
    owner_sheet.delete_rows(10, 1);
    const std::filesystem::path owner_output = artifact(
        "fastxlsx-workbook-editor-structural-hyperlink-owner-output.xlsx");
    owner_editor.save_as(owner_output);
    check(fastxlsx::test::read_zip_entries(owner_output) == owner_entries,
        "clean retry after ownership rejection should preserve every part");

    const std::array<StructuralHyperlinkFixture, 2> duplicate_hyperlinks {{
        {{1, 1, 2, 2}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!A1", {}},
        {{2, 3, 3, 4}, fastxlsx::WorksheetHyperlinkKind::Internal,
            "Untouched!C2", {}},
    }};
    const std::filesystem::path duplicate_source =
        write_two_sheet_source_with_hyperlinks(
            "fastxlsx-workbook-editor-structural-hyperlink-duplicate-source.xlsx",
            duplicate_hyperlinks);
    auto duplicate_entries = fastxlsx::test::read_zip_entries(duplicate_source);
    replace_first_or_throw(duplicate_entries.at("xl/worksheets/sheet1.xml"),
        "ref=\"C2:D3\"", "ref=\"A1:B2\"");
    fastxlsx::test::write_stored_zip_entries(duplicate_source, duplicate_entries);
    fastxlsx::WorkbookEditor duplicate_editor =
        fastxlsx::WorkbookEditor::open(duplicate_source);
    fastxlsx::WorksheetEditor duplicate_sheet = duplicate_editor.worksheet("Data");
    check(threw_fastxlsx_error([&] { duplicate_sheet.delete_rows(2, 1); }),
        "duplicate hyperlink refs should reject structural deletion");
    check(!duplicate_sheet.has_pending_changes()
            && !duplicate_editor.has_pending_changes()
            && !duplicate_editor.has_unsaved_changes(),
        "duplicate hyperlink rejection should preserve public state");

    const std::array<StructuralHyperlinkFixture, 1> overflow_hyperlinks {{
        {{1048575, 1, 1048576, 1},
            fastxlsx::WorksheetHyperlinkKind::Internal, "Data!A1", {}},
    }};
    const std::filesystem::path overflow_source =
        write_two_sheet_source_with_hyperlinks(
            "fastxlsx-workbook-editor-structural-hyperlink-overflow-source.xlsx",
            overflow_hyperlinks);
    fastxlsx::WorkbookEditor overflow_editor =
        fastxlsx::WorkbookEditor::open(overflow_source);
    fastxlsx::WorksheetEditor overflow_sheet = overflow_editor.worksheet("Data");
    check(threw_fastxlsx_error(
        [&] { overflow_sheet.insert_rows(1048576, 1); }),
        "hyperlink insertion past the row limit should fail");
    check(!overflow_sheet.has_pending_changes()
            && !overflow_editor.has_pending_changes()
            && overflow_sheet.get_cell("A1").text_value() == "placeholder-a1",
        "hyperlink overflow rejection should preserve cells and public state");
}

} // namespace

int main()
{
    try {
        test_insertions_compose_source_and_same_session_hyperlinks();
        test_prefixed_empty_store_staging_failure_retries();
        test_deletions_translate_hyperlinks_and_manage_relationships();
        test_removal_guards_and_final_container_cleanup();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d structural hyperlink check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor structural hyperlink tests passed\n");
    return 0;
}
