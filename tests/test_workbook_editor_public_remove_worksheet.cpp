#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

class ScopedWorksheetRemoveStagedHook {
public:
    explicit ScopedWorksheetRemoveStagedHook(
        fastxlsx::detail::PackageEditorWorksheetRemoveStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_remove_staged_hook(hook);
    }

    ~ScopedWorksheetRemoveStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_remove_staged_hook(nullptr);
    }

    ScopedWorksheetRemoveStagedHook(const ScopedWorksheetRemoveStagedHook&) = delete;
    ScopedWorksheetRemoveStagedHook& operator=(const ScopedWorksheetRemoveStagedHook&) = delete;
};

void fail_worksheet_remove_after_staging()
{
    throw fastxlsx::FastXlsxError("injected worksheet removal failure after staging");
}

std::filesystem::path write_source_with_unknown_entry(std::string_view name)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);
    entries.emplace("custom/opaque.bin", "worksheet removal unknown entry bytes");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_one_sheet_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto sheet = writer.add_worksheet("Only");
    sheet.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

void test_remove_existing_worksheet_and_reopen()
{
    const std::filesystem::path source = write_source_with_unknown_entry(
        "fastxlsx-workbook-editor-remove-worksheet-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-remove-worksheet-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> source_names = editor.source_worksheet_names();
    editor.remove_worksheet("Data");

    check(editor.source_worksheet_names() == source_names,
        "worksheet removal should preserve the immutable source catalog");
    check(editor.worksheet_names() == std::vector<std::string> {"Untouched"},
        "worksheet removal should update the planned catalog");
    check(!editor.has_worksheet("Data") && editor.has_worksheet("Untouched"),
        "worksheet removal should update planned worksheet lookup");
    check(editor.pending_change_count() == 1 && editor.has_unsaved_changes(),
        "worksheet removal should advance pending and unsaved state");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().removed
            && summaries.front().source_name == "Data"
            && summaries.front().planned_name == "Data",
        "worksheet removal should expose a removed edit summary");

    const auto catalog = editor.worksheet_catalog();
    check(catalog.size() == 1 && catalog.front().source_name == "Untouched"
            && catalog.front().planned_name == "Untouched" && !catalog.front().added,
        "worksheet removal should retain the surviving catalog entry");

    editor.save_as(output);
    check(!editor.has_unsaved_changes(),
        "successful worksheet removal save should advance the save watermark");

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.at("custom/opaque.bin") == "worksheet removal unknown entry bytes",
        "worksheet removal should preserve unknown source entries");
    check(!entries.contains("xl/worksheets/sheet1.xml"),
        "worksheet removal should omit the removed worksheet part");
    check_not_contains(entries.at("xl/workbook.xml"), R"(name="Data")",
        "worksheet removal should remove the workbook catalog entry");
    check_not_contains(entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="worksheets/sheet1.xml")",
        "worksheet removal should remove the workbook relationship");
    check_not_contains(entries.at("[Content_Types].xml"),
        R"(PartName="/xl/worksheets/sheet1.xml")",
        "worksheet removal should remove the worksheet content type override");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.worksheet_names() == std::vector<std::string> {"Untouched"},
        "saved worksheet removal should reopen through the public catalog");
    check(reopened.source_worksheet_names() == reopened.worksheet_names(),
        "reopened worksheet removal should become the new source catalog");
}

void test_remove_generated_worksheet_before_save()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-remove-generated-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-remove-generated-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_worksheet("Transient");
    editor.remove_worksheet("Transient");
    check(editor.worksheet_names() == std::vector<std::string> {"Data", "Untouched"},
        "generated worksheet removal should restore the planned catalog");
    check(editor.pending_change_count() == 2 && editor.has_unsaved_changes(),
        "add followed by remove should retain both public edit events");

    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(!entries.contains("xl/worksheets/sheet3.xml"),
        "generated worksheet removed before save should not be emitted");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.worksheet_names() == std::vector<std::string> {"Data", "Untouched"},
        "generated worksheet removal should survive save and reopen");
}

void test_remove_worksheet_failure_guards_and_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-remove-worksheet-guards-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-remove-worksheet-guards-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const WorkbookEditorPublicCatalogSnapshot before =
        workbook_editor_public_catalog_snapshot(editor);
    {
        ScopedWorksheetRemoveStagedHook hook(fail_worksheet_remove_after_staging);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "injected worksheet removal failure should escape as FastXlsxError");
    }
    check_workbook_editor_public_catalog_preserved(
        editor, before, "failed worksheet removal before commit");
    check(editor.pending_change_count() == 0 && !editor.has_unsaved_changes()
            && editor.last_edit_error().has_value(),
        "failed worksheet removal should preserve state and record a diagnostic");

    editor.remove_worksheet("Data");
    check(!editor.last_edit_error().has_value(),
        "successful worksheet removal retry should clear the prior diagnostic");
    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-remove-worksheet-missing")
            / "missing" / "output.xlsx");
    }), "worksheet removal save should fail for a missing output parent");
    check(!editor.has_worksheet("Data") && editor.has_unsaved_changes(),
        "failed worksheet removal save should retain the planned removal for retry");
    editor.save_as(output);
    check(fastxlsx::WorkbookEditor::open(output).worksheet_names()
            == std::vector<std::string> {"Untouched"},
        "worksheet removal should survive a failed save followed by retry");
}

void test_remove_worksheet_rejects_unsupported_dependencies()
{
    {
        const std::filesystem::path source = write_one_sheet_source(
            "fastxlsx-workbook-editor-remove-last-visible-source.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Only"); }),
            "worksheet removal should reject the last visible worksheet");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-defined-names-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        replace_first_or_throw(entries.at("xl/workbook.xml"), "</sheets>",
            R"(</sheets><definedNames><definedName name="Report">Data!A1</definedName></definedNames>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject definedName metadata");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-selected-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        replace_first_or_throw(entries.at("xl/worksheets/sheet1.xml"), "<sheetData>",
            R"(<sheetViews><sheetView tabSelected="1"/></sheetViews><sheetData>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject selected-tab metadata");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-active-view-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        replace_first_or_throw(entries.at("xl/workbook.xml"), "<sheets>",
            R"(<bookViews><workbookView activeTab="0"/></bookViews><sheets>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject workbook active-view metadata");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-owned-relationship-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        entries.emplace("xl/drawings/drawing1.xml",
            R"(<?xml version="1.0" encoding="UTF-8"?><xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"/>)");
        entries.emplace("xl/worksheets/_rels/sheet1.xml.rels",
            R"(<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/></Relationships>)");
        replace_first_or_throw(entries.at("[Content_Types].xml"), "</Types>",
            R"(<Override PartName="/xl/drawings/drawing1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/></Types>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject worksheet-owned linked parts");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-inbound-relationship-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        entries.emplace("xl/worksheets/_rels/sheet2.xml.rels",
            R"(<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdInbound" Type="urn:fastxlsx:test-inbound" Target="sheet1.xml"/></Relationships>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject non-workbook inbound relationships");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-formula-source.xlsx");
        std::map<std::string, std::string> entries =
            fastxlsx::test::read_zip_entries(source);
        replace_first_or_throw(entries.at("xl/worksheets/sheet2.xml"), "</sheetData>",
            R"(<row r="2"><c r="A2"><f>Data!A1</f><v>0</v></c></row></sheetData>)");
        fastxlsx::test::write_stored_zip_entries(source, entries);
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject formula references to the target");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-materialized-source.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        (void)editor.worksheet("Data");
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject an existing materialized handle");
    }

    {
        const std::filesystem::path source = write_two_sheet_source(
            "fastxlsx-workbook-editor-remove-pending-source.xlsx");
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("pending")} });
        check(threw_fastxlsx_error([&] { editor.remove_worksheet("Data"); }),
            "worksheet removal should reject queued worksheet payload edits");
    }
}

} // namespace

int main()
{
    try {
        test_remove_existing_worksheet_and_reopen();
        test_remove_generated_worksheet_before_save();
        test_remove_worksheet_failure_guards_and_retry();
        test_remove_worksheet_rejects_unsupported_dependencies();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor remove-worksheet check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor remove-worksheet tests passed\n");
    return 0;
}
