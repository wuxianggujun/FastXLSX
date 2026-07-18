#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

class ScopedWorksheetAddStagedHook {
public:
    explicit ScopedWorksheetAddStagedHook(
        fastxlsx::detail::PackageEditorWorksheetAddStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_add_staged_hook(hook);
    }

    ~ScopedWorksheetAddStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_add_staged_hook(nullptr);
    }

    ScopedWorksheetAddStagedHook(const ScopedWorksheetAddStagedHook&) = delete;
    ScopedWorksheetAddStagedHook& operator=(const ScopedWorksheetAddStagedHook&) = delete;
};

void fail_worksheet_add_after_staging()
{
    throw fastxlsx::FastXlsxError("injected worksheet add failure after staging");
}

std::filesystem::path write_source_with_unknown_entry(std::string_view name)
{
    const std::filesystem::path path = write_two_sheet_source(name);
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);
    entries.emplace("custom/opaque.bin", "worksheet add unknown entry bytes");
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

void test_add_worksheet_composes_with_patch_edits_and_reopen()
{
    const std::filesystem::path source = write_source_with_unknown_entry(
        "fastxlsx-workbook-editor-add-worksheet-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-add-worksheet-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> source_names = editor.source_worksheet_names();

    editor.add_worksheet("New & Sheet");
    editor.add_worksheet("Second Added");
    editor.rename_sheet("Second Added", "Renamed Added");
    editor.replace_sheet_data("New & Sheet",
        {{fastxlsx::CellValue::text("created"), fastxlsx::CellValue::number(42.0)}});
    editor.replace_cells("Renamed Added",
        {{fastxlsx::WorksheetCellReference {2, 2}, fastxlsx::CellValue::boolean(true)}},
        fastxlsx::CellPatchMissingCellPolicy::Insert);

    check(editor.source_worksheet_names() == source_names,
        "worksheet add should preserve the immutable source catalog");
    check(editor.worksheet_names()
            == std::vector<std::string> {"Data", "Untouched", "New & Sheet", "Renamed Added"},
        "worksheet add and rename should update planned catalog order");
    check(editor.has_worksheet("New & Sheet") && editor.has_worksheet("Renamed Added"),
        "worksheet add should expose new planned names");
    check(!editor.has_source_worksheet("New & Sheet"),
        "worksheet add should not expose a generated sheet as a source sheet");
    check(editor.pending_change_count() == 5 && editor.has_unsaved_changes(),
        "worksheet add and follow-up edits should advance public pending state");

    const auto catalog = editor.worksheet_catalog();
    check(catalog.size() == 4 && catalog[2].source_name.empty()
            && catalog[2].planned_name == "New & Sheet" && catalog[2].added
            && !catalog[2].renamed,
        "worksheet catalog should explicitly identify the first added sheet");
    check(catalog.size() == 4 && catalog[3].source_name.empty()
            && catalog[3].planned_name == "Renamed Added" && catalog[3].added
            && !catalog[3].renamed,
        "renaming an added sheet should retain added catalog identity");

    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 2,
        "worksheet add should surface both generated sheets in edit summaries");
    if (summaries.size() == 2) {
        check(summaries[0].added && summaries[0].sheet_data_replaced,
            "first added sheet summary should include its sheetData replacement");
        check(summaries[1].added && summaries[1].targeted_cells_replaced,
            "second added sheet summary should include its point upsert");
    }

    check(threw_fastxlsx_error([&] { (void)editor.worksheet("New & Sheet"); }),
        "newly added sheet should require save and reopen before in-memory materialization");
    check(editor.source_formula_reference_audits().empty(),
        "source formula audit should skip generated worksheets");

    editor.save_as(output);
    check(!editor.has_unsaved_changes(),
        "successful worksheet-add save should advance the save watermark");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("custom/opaque.bin") == "worksheet add unknown entry bytes",
        "worksheet add should preserve unknown source entries");
    check(output_entries.contains("xl/worksheets/sheet3.xml")
            && output_entries.contains("xl/worksheets/sheet4.xml"),
        "worksheet add should create unique generated worksheet parts");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="New &amp; Sheet")",
        "worksheet add should XML-escape the catalog name");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed Added")",
        "renaming an added worksheet should update planned workbook XML");
    check_contains(output_entries.at("xl/workbook.xml"), R"(sheetId="3")",
        "first generated worksheet should use an unused sheetId");
    check_contains(output_entries.at("xl/workbook.xml"), R"(sheetId="4")",
        "second generated worksheet should use a distinct sheetId");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="worksheets/sheet3.xml")",
        "workbook relationships should target the first generated worksheet");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="worksheets/sheet4.xml")",
        "workbook relationships should target the second generated worksheet");
    check_contains(output_entries.at("[Content_Types].xml"),
        R"(PartName="/xl/worksheets/sheet3.xml")",
        "content types should register the first generated worksheet");
    check_contains(output_entries.at("xl/worksheets/sheet3.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>created</t></is></c>)",
        "same-session sheetData replacement should populate an added worksheet");
    check_contains(output_entries.at("xl/worksheets/sheet4.xml"),
        R"(<c r="B2" t="b"><v>1</v></c>)",
        "same-session point upsert should populate an added worksheet");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.worksheet_names()
            == std::vector<std::string> {"Data", "Untouched", "New & Sheet", "Renamed Added"},
        "saved worksheet additions should reopen through the public catalog");
    check(reopened.source_worksheet_names() == reopened.worksheet_names(),
        "reopened generated worksheets should become part of the new source catalog");
    check(reopened.worksheet("New & Sheet").cell_count() == 2,
        "saved and reopened added worksheet should support in-memory materialization");
}

void test_add_worksheet_failure_preserves_transaction_state()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-add-worksheet-failure-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const WorkbookEditorPublicCatalogSnapshot catalog_before =
        workbook_editor_public_catalog_snapshot(editor);

    check(threw_fastxlsx_error([&] { editor.add_worksheet("data"); }),
        "worksheet add should reject ASCII case-insensitive duplicate names");
    check(threw_fastxlsx_error([&] { editor.add_worksheet("Bad/Name"); }),
        "worksheet add should reject invalid name characters");
    check(threw_fastxlsx_error([&] { editor.add_worksheet(std::string(32, 'A')); }),
        "worksheet add should reject overlong names");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before, "rejected worksheet additions");
    check(editor.pending_change_count() == 0 && !editor.has_unsaved_changes(),
        "rejected worksheet additions should not advance pending state");

    {
        ScopedWorksheetAddStagedHook hook(fail_worksheet_add_after_staging);
        check(threw_fastxlsx_error([&] { editor.add_worksheet("Staged Failure"); }),
            "injected worksheet add failure should escape as FastXlsxError");
    }
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before, "failed worksheet add before commit");
    check(editor.pending_change_count() == 0 && !editor.has_unsaved_changes(),
        "failed worksheet add before commit should preserve pending state");
    check(editor.last_edit_error().has_value(),
        "failed worksheet add should record a public diagnostic");

    editor.add_worksheet("Retry Added");
    check(editor.worksheet_names()
            == std::vector<std::string> {"Data", "Untouched", "Retry Added"},
        "worksheet add should remain usable after a staged failure");
    check(!editor.last_edit_error().has_value(),
        "successful worksheet add retry should clear the prior diagnostic");
}

void test_add_worksheet_save_failure_retains_retry_state()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-add-worksheet-save-retry-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-add-worksheet-missing-parent") / "output.xlsx";
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-add-worksheet-save-retry-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_worksheet("Retry Sheet");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "worksheet add save should fail for a missing output parent");
    check(editor.has_worksheet("Retry Sheet") && editor.pending_change_count() == 1
            && editor.has_unsaved_changes(),
        "failed save should retain worksheet add state for retry");

    editor.save_as(output);
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Retry Sheet"),
        "worksheet add should survive a failed save followed by retry");
}

} // namespace

int main()
{
    try {
        test_add_worksheet_composes_with_patch_edits_and_reopen();
        test_add_worksheet_failure_preserves_transaction_state();
        test_add_worksheet_save_failure_retains_retry_state();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor add-worksheet check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor add-worksheet tests passed\n");
    return 0;
}
