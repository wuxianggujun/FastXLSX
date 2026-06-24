#include "test_workbook_editor_facade_common.hpp"

void test_pending_change_diagnostics_track_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-pending-source.xlsx");

    fastxlsx::WorkbookEditor clean_editor = fastxlsx::WorkbookEditor::open(source);
    check_workbook_editor_public_clean_state(clean_editor, "newly opened editor");
    check(!clean_editor.has_pending_replacement("Data"),
        "newly opened editor should report no pending replacement for Data");

    check(threw_fastxlsx_error([&] {
        clean_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    }), "rejected replace_sheet_data should throw FastXlsxError");
    check_workbook_editor_public_no_pending_state(
        clean_editor, "rejected replace_sheet_data");
    check(clean_editor.pending_replacement_worksheet_names().empty(),
        "rejected replace_sheet_data should not add pending replacement names");

    check(threw_fastxlsx_error([&] { clean_editor.rename_sheet("Data", "Bad/Name"); }),
        "rejected rename_sheet should throw FastXlsxError");
    check_workbook_editor_public_no_pending_state(
        clean_editor, "rejected rename_sheet");

    clean_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(clean_editor.has_pending_changes(),
        "successful replace_sheet_data should mark the editor dirty");
    check(clean_editor.pending_change_count() > 0,
        "successful replace_sheet_data should expose a coarse pending count");
    check(clean_editor.pending_replacement_cell_count() == 1,
        "successful replace_sheet_data should expose final queued replacement cells");
    {
        const std::vector<std::string> pending_names =
            clean_editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "successful replace_sheet_data should expose the pending replacement sheet");
        check(clean_editor.has_pending_replacement("Data"),
            "successful replace_sheet_data should mark Data as pending replacement");
        check(!clean_editor.has_pending_replacement("Untouched"),
            "successful replace_sheet_data should not mark untouched sheets");
    }
    check(clean_editor.estimated_pending_replacement_memory_usage() > 0,
        "successful replace_sheet_data should expose estimated replacement memory");

    fastxlsx::WorkbookEditor rename_editor = fastxlsx::WorkbookEditor::open(source);
    rename_editor.rename_sheet("Data", "Renamed");
    check(rename_editor.has_pending_changes(),
        "successful rename_sheet should mark the editor dirty");
    check(rename_editor.pending_change_count() > 0,
        "successful rename_sheet should expose a coarse pending count");
    check_workbook_editor_no_replacement_diagnostics(
        rename_editor, "successful rename_sheet");
    check(!rename_editor.has_pending_replacement("Renamed"),
        "rename_sheet should not mark the renamed sheet as data-replaced");
}

void test_last_edit_error_tracks_failed_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-last-error-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.last_edit_error().has_value(),
        "newly opened editor should report no last edit error");

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed replace_sheet_data should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown replace_sheet_data diagnostic");
            check_contains(*last_error, "current planned catalog",
                "last edit error should preserve public planned-catalog context");
        }
    }
    check(failed, "missing sheet replacement should fail");

    check_workbook_editor_public_no_pending_state(
        editor, "failed replace_sheet_data");
    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.rename_sheet("Data", "Report");
    check(!editor.last_edit_error().has_value(),
        "successful rename_sheet should clear last edit error");

    failed = false;
    try {
        editor.rename_sheet("Report", "Bad/Name");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename_sheet should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown rename_sheet diagnostic");
            check_contains(*last_error, "Bad/Name",
                "last edit error should include the rejected sheet name");
        }
    }
    check(failed, "invalid rename should fail");

    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.replace_sheet_data("Report", {{fastxlsx::CellValue::number(2.0)}});
    check(!editor.last_edit_error().has_value(),
        "successful replace_sheet_data should clear last edit error");
}

void test_pending_worksheet_edit_summaries_track_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-edit-summary-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.pending_worksheet_edits().empty(),
        "newly opened editor should report no pending worksheet edit summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "single replacement should report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replacement summary should report the source sheet name");
            check(summary.planned_name == "Data",
                "replacement summary should report the current planned sheet name");
            check(!summary.renamed,
                "replacement-only summary should not be marked as renamed");
            check(summary.sheet_data_replaced,
                "replacement-only summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replacement summary should report final queued cell count");
            check(summary.estimated_replacement_memory_usage > 0,
                "replacement summary should report estimated replacement memory");
        }
    }

    editor.rename_sheet("Data", "Report");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replace+rename should still report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replace+rename summary should keep the source sheet name");
            check(summary.planned_name == "Report",
                "replace+rename summary should report the planned sheet name");
            check(summary.renamed,
                "replace+rename summary should be marked as renamed");
            check(summary.sheet_data_replaced,
                "replace+rename summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replace+rename summary should preserve queued replacement cells");
            check(summary.estimated_replacement_memory_usage > 0,
                "replace+rename summary should preserve replacement memory");
        }
    }

    editor.replace_sheet_data("Report",
        {{fastxlsx::CellValue::number(3.0), fastxlsx::CellValue::number(4.0)},
            {fastxlsx::CellValue::number(5.0)}});
    editor.rename_sheet("Untouched", "Archive");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "replacement+rename and rename-only edits should report two summaries");
        if (summaries.size() == 2) {
            const auto& replaced = summaries[0];
            check(replaced.source_name == "Data",
                "summaries should follow source workbook sheet order");
            check(replaced.planned_name == "Report",
                "updated replacement summary should keep the planned name");
            check(replaced.renamed,
                "updated replacement summary should stay marked as renamed");
            check(replaced.sheet_data_replaced,
                "updated replacement summary should report replacement");
            check(replaced.replacement_cell_count == 3,
                "updated replacement summary should report final replacement cells");
            check(replaced.estimated_replacement_memory_usage > 0,
                "updated replacement summary should keep replacement memory");

            const auto& renamed_only = summaries[1];
            check(renamed_only.source_name == "Untouched",
                "rename-only summary should report the source sheet name");
            check(renamed_only.planned_name == "Archive",
                "rename-only summary should report the planned sheet name");
            check(renamed_only.renamed,
                "rename-only summary should be marked as renamed");
            check(!renamed_only.sheet_data_replaced,
                "rename-only summary should not report sheetData replacement");
            check(renamed_only.replacement_cell_count == 0,
                "rename-only summary should report zero replacement cells");
            check(renamed_only.estimated_replacement_memory_usage == 0,
                "rename-only summary should report zero replacement memory");
        }
    }

    editor.replace_sheet_data("Archive", {{fastxlsx::CellValue::text("archived")}});
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 2,
            "two replaced sheets should report two pending replacement names");
        if (pending_names.size() == 2) {
            check(pending_names[0] == "Report",
                "pending replacement names should follow current planned catalog order");
            check(pending_names[1] == "Archive",
                "pending replacement names should include renamed second sheet in order");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "two replaced renamed sheets should still report two summaries");
        if (summaries.size() == 2) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Report",
                "first summary should keep source-order Data -> Report mapping");
            check(summaries[0].sheet_data_replaced,
                "first summary should remain marked as replaced");
            check(summaries[1].source_name == "Untouched" &&
                    summaries[1].planned_name == "Archive",
                "second summary should keep source-order Untouched -> Archive mapping");
            check(summaries[1].renamed,
                "second summary should remain marked as renamed");
            check(summaries[1].sheet_data_replaced,
                "second summary should now report replacement after planned-name edit");
            check(summaries[1].replacement_cell_count == 1,
                "second summary should report its final replacement cell count");
            check(summaries[1].estimated_replacement_memory_usage > 0,
                "second summary should report its replacement memory estimate");
        }
    }
}

void test_replace_sheet_data_source_read_failure_preserves_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-source-read-failure.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-read-failure-output.xlsx");

    const std::string original_source_bytes = fastxlsx::test::read_file(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    std::string corrupted_source_bytes = original_source_bytes;
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    corrupt_zip_entry_crc_metadata(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#else
    corrupt_zip_entry_payload(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#endif
    write_binary_file(source, corrupted_source_bytes);

    bool failed = false;
    try {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(5.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message,
            "current worksheet input for worksheet sheetData replacement output",
            "WorkbookEditor should preserve the internal sheetData current-input boundary");
        check_contains(message, "xl/worksheets/sheet1.xml",
            "WorkbookEditor should preserve the corrupt worksheet entry context");
        check_not_contains(message, "sheetData replacement XML",
            "WorkbookEditor should not mislabel source-entry failures as "
            "replacement payload failures");
#ifndef FASTXLSX_TEST_HAS_MINIZIP_NG
        check_contains(message, "CRC mismatch",
            "WorkbookEditor stored source read failure should preserve the ZIP CRC error");
        check_contains(message, "expected ",
            "WorkbookEditor stored CRC failure should report the expected CRC");
        check_contains(message, "actual ",
            "WorkbookEditor stored CRC failure should report the actual CRC");
#else
        if (message.find("CRC mismatch") != std::string::npos) {
            check_contains(message, "expected ",
                "WorkbookEditor CRC failure should report the expected CRC");
            check_contains(message, "actual ",
                "WorkbookEditor CRC failure should report the actual CRC");
        }
#endif
    }

    check(failed,
        "replace_sheet_data should fail when the source worksheet entry cannot be read");
    check_workbook_editor_public_no_pending_state(editor, "source read failure");
    check_workbook_editor_no_replacement_payload_size_diagnostics(
        editor, "source read failure");
    check(editor.has_worksheet("Data"),
        "source read failure should not disturb the opened source sheet catalog");

    write_binary_file(source, original_source_bytes);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(6.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>6</v>)",
        "editor should remain usable after a source read failure once the source is restored");
}

void check_clean_replace_sheet_data_failure_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    check_workbook_editor_public_no_pending_state(editor, scenario);
    check_workbook_editor_no_replacement_payload_size_diagnostics(editor, scenario);
    check(editor.has_worksheet("Data"),
        std::string(scenario) + " should not disturb the opened source sheet catalog");
}

void check_sheet_data_current_input_facade_error(
    const std::string& message, std::string_view scenario)
{
    check_contains(message,
        "current worksheet input for worksheet sheetData replacement output",
        std::string(scenario)
            + " should preserve the internal sheetData current-input boundary");
    check_not_contains(message, "sheetData replacement XML",
        std::string(scenario)
            + " should not be mislabeled as replacement payload input");
}

void test_replace_sheet_data_source_xml_failures_preserve_public_state()
{
    const std::filesystem::path missing_source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source-sheetdata.xlsx");
    const std::filesystem::path missing_output =
        artifact("fastxlsx-workbook-editor-missing-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(missing_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet><dimension ref="A1"/></worksheet>)");

    fastxlsx::WorkbookEditor missing_editor = fastxlsx::WorkbookEditor::open(missing_source);
    bool failed = false;
    try {
        missing_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "missing source sheetData failure");
        check_contains(message, "worksheet XML sheetData element is missing",
            "missing source sheetData failure should explain the missing target");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source worksheet has no sheetData");
    check_clean_replace_sheet_data_failure_state(
        missing_editor, "missing source sheetData failure");
    missing_editor.rename_sheet("Data", "MissingChecked");
    missing_editor.save_as(missing_output);
    check_contains(fastxlsx::test::read_zip_entries(missing_output).at("xl/workbook.xml"),
        R"(name="MissingChecked")",
        "editor should remain usable for a valid catalog edit after missing source sheetData");

    const std::filesystem::path malformed_source =
        write_two_sheet_source("fastxlsx-workbook-editor-malformed-source-sheetdata.xlsx");
    const std::filesystem::path malformed_output =
        artifact("fastxlsx-workbook-editor-malformed-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(malformed_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)");

    fastxlsx::WorkbookEditor malformed_editor =
        fastxlsx::WorkbookEditor::open(malformed_source);
    failed = false;
    try {
        malformed_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(8.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "malformed source sheetData failure");
        check_contains(message, "worksheet event reader found an invalid worksheet boundary",
            "malformed source sheetData failure should preserve event-reader diagnostics");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source sheetData is malformed");
    check_clean_replace_sheet_data_failure_state(
        malformed_editor, "malformed source sheetData failure");
    malformed_editor.rename_sheet("Data", "MalformedChecked");
    malformed_editor.save_as(malformed_output);
    check_contains(fastxlsx::test::read_zip_entries(malformed_output).at("xl/workbook.xml"),
        R"(name="MalformedChecked")",
        "editor should remain usable for a valid catalog edit after malformed source sheetData");
}

void test_replacement_guardrails_and_payload_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-guardrails-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-guardrails-output.xlsx");

    fastxlsx::WorkbookEditorOptions max_cell_options;
    max_cell_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor max_cell_editor =
        fastxlsx::WorkbookEditor::open(source, max_cell_options);

    check(threw_fastxlsx_error([&] {
        max_cell_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    }), "replace_sheet_data should enforce max_replacement_cells before commit");
    check_clean_replace_sheet_data_failure_state(
        max_cell_editor, "max_replacement_cells failure");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(3.0)}});
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "valid guarded replacement should record one queued cell");
    const std::size_t first_memory =
        max_cell_editor.estimated_pending_replacement_memory_usage();
    check(first_memory > 0,
        "valid guarded replacement should record non-zero estimated memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(4.0)}});
    check(max_cell_editor.pending_change_count() == 2,
        "repeated same-sheet replacement should still count public edit calls");
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "repeated same-sheet replacement should report only final queued cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() > 0,
        "repeated same-sheet replacement should keep final estimated memory");

    max_cell_editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "guarded replacement output should use the final queued payload");
    check_not_contains(worksheet_xml, R"(<v>3</v>)",
        "guarded replacement output should drop stale same-sheet payload");

    fastxlsx::WorkbookEditorOptions memory_options;
    memory_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(source, memory_options);
    check(threw_fastxlsx_error([&] {
        memory_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "replace_sheet_data should enforce replacement_memory_budget_bytes before commit");
    check_clean_replace_sheet_data_failure_state(memory_editor, "memory budget failure");
}

void test_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message, "current planned catalog",
            "missing sheet failure should name the planned catalog lookup");
        check_contains(message, "Missing",
            "missing sheet failure should include the requested sheet name");
    }
    check(failed, "replacing a missing sheet should throw FastXlsxError");
    check_clean_replace_sheet_data_failure_state(editor, "missing sheet failure");

    fastxlsx::WorkbookEditorOptions guard_options;
    guard_options.max_replacement_cells = 0;
    fastxlsx::WorkbookEditor guarded_editor =
        fastxlsx::WorkbookEditor::open(source, guard_options);
    failed = false;
    try {
        guarded_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(2.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "current planned catalog",
            "missing sheet preflight should run before replacement payload guardrails");
    }
    check(failed,
        "guarded missing sheet replacement should throw FastXlsxError");
    check_clean_replace_sheet_data_failure_state(
        guarded_editor, "guarded missing sheet failure");

    // The editor must remain usable after a rejected edit.
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>7</v></c>)",
        "editor should still apply a valid edit after a rejected one");
}

void test_replace_sheet_data_failure_diagnostics_include_context()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-sheet-data-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-sheet-data-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)},
                {fastxlsx::CellValue::text("third")}});
        check(false, "missing-sheet replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_sheet_data() failed",
            "missing-sheet diagnostic should name the public API");
        check_contains(message, "Missing",
            "missing-sheet diagnostic should include the requested sheet");
        check_contains(message, "with 2 rows and 3 cells",
            "missing-sheet diagnostic should include the input shape");
        check_contains(message, "current planned catalog",
            "missing-sheet diagnostic should preserve the root cause");
        check(editor.last_edit_error().has_value() && *editor.last_edit_error() == message,
            "missing-sheet last_edit_error should match the thrown diagnostic");
    }
    check_workbook_editor_public_no_pending_state(
        editor, "missing-sheet diagnostic failure");
    check(editor.pending_replacement_cell_count() == 0,
        "missing-sheet diagnostic failure should not record replacement cells");

    fastxlsx::WorkbookEditorOptions guard_options;
    guard_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor guarded_editor =
        fastxlsx::WorkbookEditor::open(source, guard_options);
    try {
        guarded_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
        check(false, "guarded replacement should throw");
    } catch (const fastxlsx::FastXlsxError& error) {
        const std::string message = error.what();
        check_contains(message, "WorkbookEditor::replace_sheet_data() failed",
            "guardrail diagnostic should name the public API");
        check_contains(message, "Data",
            "guardrail diagnostic should include the requested sheet");
        check_contains(message, "with 1 rows and 2 cells",
            "guardrail diagnostic should include the input shape");
        check_contains(message, "CellStore max_cells guardrail exceeded",
            "guardrail diagnostic should preserve the root cause");
        check(guarded_editor.last_edit_error().has_value()
                && *guarded_editor.last_edit_error() == message,
            "guardrail last_edit_error should match the thrown diagnostic");
    }
    check_workbook_editor_public_no_pending_state(
        guarded_editor, "guardrail diagnostic failure");
    check(guarded_editor.pending_replacement_cell_count() == 0,
        "guardrail diagnostic failure should not record replacement cells");

    guarded_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(!guarded_editor.last_edit_error().has_value(),
        "successful sheetData replacement should clear prior failure diagnostics");
    guarded_editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>9</v>)",
        "editor should remain usable after sheetData replacement diagnostics");
}

} // namespace

int main()
{
    try {
        test_pending_change_diagnostics_track_public_edits();
        test_last_edit_error_tracks_failed_public_edits();
        test_pending_worksheet_edit_summaries_track_public_facade_state();
        test_replace_sheet_data_source_read_failure_preserves_public_state();
        test_replace_sheet_data_source_xml_failures_preserve_public_state();
        test_replacement_guardrails_and_payload_diagnostics();
        test_missing_sheet_throws_and_editor_stays_usable();
        test_replace_sheet_data_failure_diagnostics_include_context();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade core check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade core tests passed\n");
    return 0;
}
