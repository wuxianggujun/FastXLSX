#include "test_workbook_editor_public_state_shifts_support.hpp"

namespace {

void test_public_worksheet_editor_shifts_rewrite_stationary_formula_references()
{
    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-rows-noop-output.xlsx",
        "A3+B1",
        "A4+B1",
        "stationary formula insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-rows-noop-output.xlsx",
        "A3+B1",
        "#REF!+B1",
        "stationary formula delete_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-insert-columns-noop-output.xlsx",
        "D1+B1",
        "E1+B1",
        "stationary formula insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-columns-noop-output.xlsx",
        "D1+B1",
        "#REF!+B1",
        "stationary formula delete_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-rows-noop-output.xlsx",
        "SUM(A3:B3)+3:3",
        "SUM(A4:B4)+4:4",
        "stationary formula range insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-insert-columns-noop-output.xlsx",
        "SUM(D1:E1)+D:E",
        "SUM(E1:F1)+E:F",
        "stationary formula range insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-rows-noop-output.xlsx",
        "SUM(A1:B4)+1:4+Data!$1:$4",
        "SUM(A1:B5)+1:5+Data!$1:$5",
        "stationary formula split range insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-split-range-insert-columns-noop-output.xlsx",
        "SUM(A1:F2)+A:F+Data!$A:$F",
        "SUM(A1:G2)+A:G+Data!$A:$G",
        "stationary formula split range insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-rows-noop-output.xlsx",
        "A3:B4+B5:A1",
        "#REF!:#REF!+B3:A1",
        "stationary formula range endpoint delete_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(3, 2); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-delete-columns-noop-output.xlsx",
        "D1:E2+F2:D1",
        "#REF!:#REF!+D2:#REF!",
        "stationary formula range endpoint delete_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(4, 2); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-row-boundary-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-row-boundary-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-row-boundary-noop-output.xlsx",
        "A1048576:B1048576",
        "#REF!:#REF!",
        "stationary formula range endpoint row boundary insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(1048576, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-column-boundary-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-column-boundary-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-endpoint-column-boundary-noop-output.xlsx",
        "XFD1:XFD2",
        "#REF!:#REF!",
        "stationary formula range endpoint column boundary insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(16384, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-rows-noop-output.xlsx",
        R"(SUM(A3,"A3",Table1[A3],'A3 Sheet'!B1,[A3.xlsx]Sheet1!A3,LOG10(A3),A3foo,_A3,A3_))",
        R"(SUM(A4,"A3",Table1[A3],'A3 Sheet'!B1,[A3.xlsx]Sheet1!A4,LOG10(A4),A3foo,_A3,A3_))",
        "stationary formula skip-token insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-skip-insert-columns-noop-output.xlsx",
        R"(SUM(D1,"D1",Table1[D1],'D Sheet'!D1,[D1.xlsx]Sheet1!D1,LOG10(D5),D1foo,_D1,D1_))",
        R"(SUM(E1,"D1",Table1[D1],'D Sheet'!E1,[D1.xlsx]Sheet1!E1,LOG10(E5),D1foo,_D1,D1_))",
        "stationary formula skip-token insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-rows-noop-output.xlsx",
        "SUM($A$3,$B1,C$3,Data!$A$3)",
        "SUM($A$4,$B1,C$4,Data!$A$4)",
        "stationary formula absolute insert_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-rows-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-rows-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-rows-noop-output.xlsx",
        "SUM($A$3,$B1,C$4,Data!$A$3)",
        "SUM(#REF!,$B1,C$3,Data!#REF!)",
        "stationary formula absolute delete_rows",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_rows(3, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-insert-columns-noop-output.xlsx",
        "SUM($D$1,B$1,$D1,Data!$D$1)",
        "SUM($E$1,B$1,$E1,Data!$E$1)",
        "stationary formula absolute insert_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); });

    check_public_stationary_formula_shift_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-columns-source.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-columns-output.xlsx",
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-absolute-delete-columns-noop-output.xlsx",
        "SUM($D$1,B$1,E$1,Data!$D$1)",
        "SUM(#REF!,B$1,D$1,Data!#REF!)",
        "stationary formula absolute delete_columns",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.delete_columns(4, 1); });
}

void test_public_worksheet_editor_stationary_formula_shift_audits_rewritten_references()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-audit-source.xlsx",
            "Data!A3+Data!B1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(3, 1);

    constexpr std::string_view expected_formula = "Data!A4+Data!B1";
    check(sheet.has_pending_changes(),
        "stationary formula shift audit setup should dirty the materialized sheet");
    check(sheet.cell_count() == 4,
        "stationary formula shift audit setup should keep sparse cell count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "stationary formula shift audit setup should keep sparse bounds stable");
    const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
    check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
            rewritten_formula.text_value() == expected_formula,
        "stationary formula shift audit setup should expose the rewritten formula");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "stationary formula shift audit setup should keep only materialized diagnostics dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "stationary formula shift audit");
    check(audits.size() == 2,
        "stationary formula shift audit should report both rewritten references");

    const auto check_rewritten_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the formula audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the current formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report rewritten formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_rewritten_audit(
        "Data!A4", "A4", "stationary formula shift audit rewritten A reference");
    check_rewritten_audit(
        "Data!B1", "B1", "stationary formula shift audit stable B reference");
}

void test_public_worksheet_editor_stationary_formula_delete_audits_skip_ref()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-audit-source.xlsx",
            "Data!A3+Data!B1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(3, 1);

    constexpr std::string_view expected_formula = "Data!#REF!+Data!B1";
    check(sheet.has_pending_changes(),
        "stationary formula delete audit setup should dirty the materialized sheet");
    check(sheet.cell_count() == 4,
        "stationary formula delete audit setup should keep sparse cell count stable");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "stationary formula delete audit setup should keep sparse bounds stable");
    const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
    check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
            rewritten_formula.text_value() == expected_formula,
        "stationary formula delete audit setup should expose the #REF! formula");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "stationary formula delete audit setup should keep only materialized diagnostics dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "stationary formula delete audit");
    check(audits.size() == 1,
        "stationary formula delete audit should report only the surviving reference");
    check(find_public_state_formula_audit(audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete audit should skip Data!#REF! as a reference");

    const fastxlsx::WorkbookEditorFormulaReferenceAudit* surviving_audit =
        find_public_state_formula_audit(audits, 1, 3, "Data!B1");
    check(surviving_audit != nullptr,
        "stationary formula delete audit should expose the surviving B reference");
    if (surviving_audit != nullptr) {
        check(surviving_audit->formula_sheet_source_name == "Data" &&
                surviving_audit->formula_sheet_planned_name == "Data" &&
                surviving_audit->formula_text == expected_formula,
            "stationary formula delete audit should report the current formula cell");
        check(surviving_audit->sheet_qualifier_text == "Data!" &&
                surviving_audit->reference_text == "B1" &&
                surviving_audit->referenced_sheet_name == "Data",
            "stationary formula delete audit should report surviving formula tokens");
        check(surviving_audit->matched_current_workbook_sheet &&
                surviving_audit->matched_source_sheet_name == "Data" &&
                surviving_audit->matched_planned_sheet_name == "Data",
            "stationary formula delete audit should match the current Data sheet");
        check(!surviving_audit->references_renamed_source_name &&
                surviving_audit->references_planned_sheet_name &&
                !surviving_audit->external_workbook_qualifier &&
                !surviving_audit->sheet_range_qualifier,
            "stationary formula delete audit should keep qualifier flags clean");
    }
}

void test_public_worksheet_editor_stationary_formula_source_audits_preserve_source_scan()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-source-audit-source.xlsx",
            "Data!A3+Data!B1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(3, 1);

    constexpr std::string_view shifted_formula = "Data!A4+Data!B1";
    constexpr std::string_view source_formula = "Data!A3+Data!B1";
    const fastxlsx::CellValue materialized_formula = sheet.get_cell("C1");
    check(materialized_formula.kind() == fastxlsx::CellValueKind::Formula &&
            materialized_formula.text_value() == shifted_formula,
        "stationary formula source audit setup should expose the rewritten formula");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "stationary formula source audit setup should keep only materialized diagnostics dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "stationary formula source audit");
    check(source_audits.size() == 2,
        "stationary formula source audit should report the original source references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A4") == nullptr,
        "stationary formula source audit should not report the materialized A4 reference");

    const auto check_source_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    source_audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the source audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == source_formula,
                std::string(message_prefix) + " should report the source formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report source formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_source_audit(
        "Data!A3", "A3", "stationary formula source audit original A reference");
    check_source_audit(
        "Data!B1", "B1", "stationary formula source audit original B reference");
}

void test_public_worksheet_editor_stationary_formula_delete_source_audits_preserve_source_scan()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-source-audit-source.xlsx",
            "Data!A3+Data!B1");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(3, 1);

    constexpr std::string_view materialized_formula = "Data!#REF!+Data!B1";
    constexpr std::string_view source_formula = "Data!A3+Data!B1";
    const fastxlsx::CellValue current_formula = sheet.get_cell("C1");
    check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
            current_formula.text_value() == materialized_formula,
        "stationary formula delete source audit setup should expose the #REF! formula");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "stationary formula delete source audit setup should keep only materialized diagnostics dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "stationary formula delete source audit");
    check(source_audits.size() == 2,
        "stationary formula delete source audit should report the original source references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete source audit should not report the materialized #REF! token");

    const auto check_source_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    source_audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the source audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == source_formula,
                std::string(message_prefix) + " should report the source formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report source formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_source_audit(
        "Data!A3", "A3", "stationary formula delete source audit original A reference");
    check_source_audit(
        "Data!B1", "B1", "stationary formula delete source audit original B reference");
}

void test_public_worksheet_editor_stationary_formula_range_source_audits_preserve_source_scan()
{
    const auto check_dirty_source_audit_case =
        [](std::string_view source_name,
            std::string_view source_formula,
            std::string_view materialized_formula,
            std::string_view case_label,
            auto shift_operation,
            std::initializer_list<std::string_view> materialized_tokens,
            std::initializer_list<std::pair<std::string_view, std::string_view>> source_tokens) {
            const std::filesystem::path source =
                write_two_sheet_source_with_stationary_formula(source_name, source_formula);

            fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
            fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

            shift_operation(sheet);

            check(sheet.has_pending_changes(),
                std::string(case_label) + " setup should dirty the materialized sheet");
            check(sheet.cell_count() == 4,
                std::string(case_label) + " setup should keep sparse cell count stable");
            check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
                std::string(case_label) + " setup should keep sparse bounds stable");
            const fastxlsx::CellValue current_formula = sheet.get_cell("C1");
            check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
                    current_formula.text_value() == materialized_formula,
                std::string(case_label) + " setup should expose the rewritten formula");
            const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
            check(editor.pending_change_count() == 0 &&
                    editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
                    editor.pending_materialized_cell_count() == 4 &&
                    editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
                std::string(case_label) + " setup should keep only materialized diagnostics dirty");

            const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
                check_public_state_source_formula_audits_preserve_editor_diagnostics(
                    editor, case_label);
            check(source_audits.size() == source_tokens.size(),
                std::string(case_label) + " should report the original source references");
            for (const std::string_view materialized_token : materialized_tokens) {
                check(find_public_state_formula_audit(source_audits, 1, 3, materialized_token) == nullptr,
                    std::string(case_label) + " should not report materialized token " +
                        std::string(materialized_token));
            }

            for (const auto& [qualified_reference_text, reference_text] : source_tokens) {
                const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                    find_public_state_formula_audit(
                        source_audits, 1, 3, qualified_reference_text);
                check(audit != nullptr,
                    std::string(case_label) + " should expose source token " +
                        std::string(qualified_reference_text));
                if (audit == nullptr) {
                    continue;
                }

                check(audit->formula_sheet_source_name == "Data" &&
                        audit->formula_sheet_planned_name == "Data" &&
                        audit->formula_text == source_formula,
                    std::string(case_label) + " should report the source formula cell");
                check(audit->sheet_qualifier_text == "Data!" &&
                        audit->reference_text == reference_text &&
                        audit->referenced_sheet_name == "Data",
                    std::string(case_label) + " should report source formula tokens");
                check(audit->matched_current_workbook_sheet &&
                        audit->matched_source_sheet_name == "Data" &&
                        audit->matched_planned_sheet_name == "Data",
                    std::string(case_label) + " should match the current Data sheet");
                check(!audit->references_renamed_source_name &&
                        audit->references_planned_sheet_name &&
                        !audit->external_workbook_qualifier &&
                        !audit->sheet_range_qualifier,
                    std::string(case_label) + " should keep qualifier flags clean");
            }
        };

    check_dirty_source_audit_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-source-audit-source.xlsx",
        "SUM(Data!A3:B3)+Data!3:3",
        "SUM(Data!A4:B4)+Data!4:4",
        "stationary formula range source audit",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_rows(3, 1); },
        {"Data!A4:B4", "Data!4:4"},
        {{"Data!A3:B3", "A3:B3"}, {"Data!3:3", "3:3"}});

    check_dirty_source_audit_case(
        "fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-range-source-audit-source.xlsx",
        "SUM(Data!D1:E1)+Data!D:E",
        "SUM(Data!E1:F1)+Data!E:F",
        "stationary formula column range source audit",
        [](fastxlsx::WorksheetEditor& sheet) { sheet.insert_columns(4, 1); },
        {"Data!E1:F1", "Data!E:F"},
        {{"Data!D1:E1", "D1:E1"}, {"Data!D:E", "D:E"}});
}

void test_public_worksheet_editor_delete_ref_formula_source_audits_preserve_source_scan()
{
    const auto check_source_audit =
        [](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& source_audits,
            std::uint32_t formula_row,
            std::uint32_t formula_column,
            std::string_view source_formula,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    source_audits, formula_row, formula_column, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the source audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == source_formula,
                std::string(message_prefix) + " should report the source formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report source formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_delete_row_ref_formula(
                "fastxlsx-workbook-editor-public-worksheet-delete-row-ref-source-audit-source.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_rows(1, 1);

        constexpr std::string_view source_formula =
            "Data!A1+Data!A:A+Data!1:1+Data!B4";
        constexpr std::string_view materialized_formula =
            "Data!#REF!+Data!A:A+Data!#REF!+Data!B3";
        const fastxlsx::CellValue current_formula = sheet.get_cell("C3");
        check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
                current_formula.text_value() == materialized_formula,
            "delete-row #REF source audit setup should expose the rewritten formula");
        check(!sheet.try_cell("C4").has_value(),
            "delete-row #REF source audit setup should move the formula cell");
        check(sheet.has_pending_changes(),
            "delete-row #REF source audit setup should dirty the materialized sheet");
        check(sheet.cell_count() == 5,
            "delete-row #REF source audit setup should drop cells from the deleted row");
        check_cell_range_equals(sheet.used_range(), 1, 1, 3, 3,
            "delete-row #REF source audit setup should refresh sparse bounds");
        const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
        check(editor.pending_change_count() == 0 &&
                editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
                editor.pending_materialized_cell_count() == 5 &&
                editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            "delete-row #REF source audit setup should keep only materialized diagnostics dirty");

        const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
            check_public_state_source_formula_audits_preserve_editor_diagnostics(
                editor, "delete-row #REF source audit");
        check(source_audits.size() == 4,
            "delete-row #REF source audit should report the original source references");
        check(find_public_state_formula_audit(source_audits, 3, 3, "Data!A:A") == nullptr,
            "delete-row #REF source audit should not report the materialized formula cell");
        check(find_public_state_formula_audit(source_audits, 4, 3, "Data!#REF!") == nullptr,
            "delete-row #REF source audit should not report materialized #REF! tokens");
        check(find_public_state_formula_audit(source_audits, 4, 3, "Data!B3") == nullptr,
            "delete-row #REF source audit should not report the materialized shifted B reference");

        check_source_audit(source_audits, 4, 3, source_formula, "Data!A1", "A1",
            "delete-row #REF source audit original A1 reference");
        check_source_audit(source_audits, 4, 3, source_formula, "Data!A:A", "A:A",
            "delete-row #REF source audit original whole-column reference");
        check_source_audit(source_audits, 4, 3, source_formula, "Data!1:1", "1:1",
            "delete-row #REF source audit original whole-row reference");
        check_source_audit(source_audits, 4, 3, source_formula, "Data!B4", "B4",
            "delete-row #REF source audit original B4 reference");
    }

    {
        const std::filesystem::path source =
            write_two_sheet_source_with_delete_column_ref_formula(
                "fastxlsx-workbook-editor-public-worksheet-delete-column-ref-source-audit-source.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_columns(1, 1);

        constexpr std::string_view source_formula =
            "Data!A1+Data!A:A+Data!1:1+Data!D2";
        constexpr std::string_view materialized_formula =
            "Data!#REF!+Data!#REF!+Data!1:1+Data!C2";
        const fastxlsx::CellValue current_formula = sheet.get_cell("C1");
        check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
                current_formula.text_value() == materialized_formula,
            "delete-column #REF source audit setup should expose the rewritten formula");
        check(!sheet.try_cell("D1").has_value(),
            "delete-column #REF source audit setup should move the formula cell");
        check(sheet.has_pending_changes(),
            "delete-column #REF source audit setup should dirty the materialized sheet");
        check(sheet.cell_count() == 6,
            "delete-column #REF source audit setup should drop cells from the deleted column");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
            "delete-column #REF source audit setup should refresh sparse bounds");
        const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
        check(editor.pending_change_count() == 0 &&
                editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
                editor.pending_materialized_cell_count() == 6 &&
                editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            "delete-column #REF source audit setup should keep only materialized diagnostics dirty");

        const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
            check_public_state_source_formula_audits_preserve_editor_diagnostics(
                editor, "delete-column #REF source audit");
        check(source_audits.size() == 4,
            "delete-column #REF source audit should report the original source references");
        check(find_public_state_formula_audit(source_audits, 1, 3, "Data!1:1") == nullptr,
            "delete-column #REF source audit should not report the materialized formula cell");
        check(find_public_state_formula_audit(source_audits, 1, 4, "Data!#REF!") == nullptr,
            "delete-column #REF source audit should not report materialized #REF! tokens");
        check(find_public_state_formula_audit(source_audits, 1, 4, "Data!C2") == nullptr,
            "delete-column #REF source audit should not report the materialized shifted C reference");

        check_source_audit(source_audits, 1, 4, source_formula, "Data!A1", "A1",
            "delete-column #REF source audit original A1 reference");
        check_source_audit(source_audits, 1, 4, source_formula, "Data!A:A", "A:A",
            "delete-column #REF source audit original whole-column reference");
        check_source_audit(source_audits, 1, 4, source_formula, "Data!1:1", "1:1",
            "delete-column #REF source audit original whole-row reference");
        check_source_audit(source_audits, 1, 4, source_formula, "Data!D2", "D2",
            "delete-column #REF source audit original D2 reference");
    }
}

void test_public_worksheet_editor_materialized_only_formula_source_audits_ignore_dirty_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-source-audit-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    constexpr std::string_view dirty_formula = "Data!A1+Data!B1";
    sheet.set_cell(2, 3, fastxlsx::CellValue::formula(std::string(dirty_formula)));

    const fastxlsx::CellValue current_formula = sheet.get_cell("C2");
    check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
            current_formula.text_value() == dirty_formula,
        "materialized-only formula source audit setup should expose the dirty formula");
    check(sheet.has_pending_changes(),
        "materialized-only formula source audit setup should dirty the materialized sheet");
    check(sheet.cell_count() == 4,
        "materialized-only formula source audit setup should add one sparse formula cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "materialized-only formula source audit setup should expand sparse bounds");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "materialized-only formula source audit setup should keep only materialized diagnostics dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula source audit");
    check(source_audits.empty(),
        "materialized-only formula source audit should ignore the dirty materialized-only formula");
    check(find_public_state_formula_audit(source_audits, 2, 3, "Data!A1") == nullptr &&
            find_public_state_formula_audit(source_audits, 2, 3, "Data!B1") == nullptr,
        "materialized-only formula source audit should not report dirty materialized tokens");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula materialized audit");
    check(materialized_audits.size() == 2,
        "materialized-only formula materialized audit should report the dirty formula references");

    const auto check_materialized_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    materialized_audits, 2, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the materialized audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == dirty_formula,
                std::string(message_prefix) + " should report the dirty formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report dirty formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_materialized_audit(
        "Data!A1", "A1", "materialized-only formula materialized audit A reference");
    check_materialized_audit(
        "Data!B1", "B1", "materialized-only formula materialized audit B reference");
    check_materialized_only_formula_row_column_snapshots(
        sheet, dirty_formula, "materialized-only formula dirty audit live handle");
    check(sheet.has_pending_changes(),
        "materialized-only formula dirty audit live snapshots should keep the materialized sheet dirty");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "materialized-only formula dirty audit live snapshots should preserve dirty materialized diagnostics");
}

void test_public_worksheet_editor_materialized_only_formula_failed_save_preserves_audits()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-failed-save-audit-source.xlsx");
    const std::filesystem::path output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-failed-save-audit-output.xlsx");

    const auto source_entries_before_failed_save = fastxlsx::test::read_zip_entries(source);
    constexpr std::string_view expected_formula = "Data!A1+Data!B1";

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula(std::string(expected_formula)));

    const fastxlsx::CellValue current_formula = sheet.get_cell("C2");
    check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
            current_formula.text_value() == expected_formula,
        "materialized-only formula failed-save audit setup should expose the dirty formula");
    check(sheet.has_pending_changes(),
        "materialized-only formula failed-save audit setup should dirty the materialized sheet");
    check(sheet.cell_count() == 4,
        "materialized-only formula failed-save audit setup should add one sparse formula cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "materialized-only formula failed-save audit setup should expand sparse bounds");
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "materialized-only formula failed-save audit setup should keep only materialized diagnostics dirty");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "materialized-only formula failed-save audit should reject exact source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "materialized-only formula failed-save audit should leave source package bytes unchanged");
    check(sheet.has_pending_changes(),
        "materialized-only formula failed-save audit should preserve the dirty materialized sheet");
    const fastxlsx::CellValue formula_after_failed_save = sheet.get_cell("C2");
    check(formula_after_failed_save.kind() == fastxlsx::CellValueKind::Formula &&
            formula_after_failed_save.text_value() == expected_formula,
        "materialized-only formula failed-save audit should preserve the dirty formula");
    check(editor.pending_change_count() == 0 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 4 &&
            editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "materialized-only formula failed-save audit should preserve dirty materialized diagnostics");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula failed-save source audit");
    check(source_audits.empty(),
        "materialized-only formula failed-save source audit should still ignore the dirty formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula failed-save materialized audit");
    check(materialized_audits.size() == 2,
        "materialized-only formula failed-save materialized audit should still report dirty references");

    const auto check_materialized_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    materialized_audits, 2, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the materialized audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the dirty formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report dirty formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_materialized_audit(
        "Data!A1", "A1",
        "materialized-only formula failed-save materialized audit A reference");
    check_materialized_audit(
        "Data!B1", "B1",
        "materialized-only formula failed-save materialized audit B reference");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "materialized-only formula failed-save safe retry should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "materialized-only formula failed-save safe retry should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula failed-save safe retry should clear materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "materialized-only formula failed-save safe retry should leave the source package unchanged");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>Data!A1+Data!B1</f></c>)",
        "materialized-only formula failed-save safe retry should write the dirty formula");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula failed-save safe retry reopen setup");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> saved_source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "materialized-only formula failed-save saved source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula failed-save saved source audit");
    check(saved_source_audits.size() == 2 &&
            find_public_state_formula_audit(saved_source_audits, 2, 3, "Data!A1")
                != nullptr &&
            find_public_state_formula_audit(saved_source_audits, 2, 3, "Data!B1")
                != nullptr,
        "materialized-only formula failed-save saved source audit should report safe-retry references");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C2");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "materialized-only formula failed-save safe retry reopened output should read the saved formula");
    check_materialized_only_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula,
        "materialized-only formula failed-save safe retry reopened output");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula failed-save safe retry after materialization");
}

void test_public_worksheet_editor_materialized_only_formula_failed_save_noop_preserves_output()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-failed-save-noop-source.xlsx");
    const std::filesystem::path output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-failed-save-noop-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-failed-save-noop-second-output.xlsx");

    constexpr std::string_view expected_formula = "Data!A1+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula(std::string(expected_formula)));
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "materialized-only formula failed-save no-op should reject exact source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula failed-save no-op rejected save should leave the source package unchanged");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "materialized-only formula failed-save no-op safe retry should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "materialized-only formula failed-save no-op safe retry should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula failed-save no-op safe retry should clear materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula failed-save no-op safe retry should leave the source package unchanged");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>Data!A1+Data!B1</f></c>)",
        "materialized-only formula failed-save no-op safe retry should write the formula");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);

    check(!sheet.has_pending_changes(),
        "materialized-only formula failed-save no-op should keep the materialized sheet clean");
    check(editor.pending_change_count() == 1,
        "materialized-only formula failed-save no-op should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula failed-save no-op should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "materialized-only formula failed-save no-op should keep dirty summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "materialized-only formula failed-save no-op should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "materialized-only formula failed-save no-op should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "materialized-only formula failed-save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "materialized-only formula failed-save no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "materialized-only formula failed-save no-op output should match the safe retry output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula failed-save no-op save should leave the source package unchanged");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula failed-save no-op reopen setup");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> saved_source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "materialized-only formula failed-save no-op saved source audit");
    check(saved_source_audits.size() == 2 &&
            find_public_state_formula_audit(saved_source_audits, 2, 3, "Data!A1")
                != nullptr &&
            find_public_state_formula_audit(saved_source_audits, 2, 3, "Data!B1")
                != nullptr,
        "materialized-only formula failed-save no-op saved source audit should report formula references");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C2");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "materialized-only formula failed-save no-op reopened output should read the saved formula");
    check_materialized_only_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula,
        "materialized-only formula failed-save no-op reopened output");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula failed-save no-op after materialization");
}

void test_public_worksheet_editor_materialized_only_formula_same_editor_saved_audits()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-same-editor-audit-source.xlsx");
    const std::filesystem::path output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-same-editor-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-same-editor-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!A1+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(2, 3, fastxlsx::CellValue::formula(std::string(expected_formula)));
    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "materialized-only formula same-editor saved audit should clean the materialized sheet");
    check(editor.pending_change_count() == 1,
        "materialized-only formula same-editor saved audit should record one materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula same-editor saved audit should clear materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula same-editor saved audit save_as should leave the source package unchanged");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="C2"><f>Data!A1+Data!B1</f></c>)",
        "materialized-only formula same-editor saved audit should write the formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula same-editor saved source audit");
    check(source_audits.empty(),
        "materialized-only formula same-editor saved source audit should keep scanning original source XML");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "materialized-only formula same-editor saved materialized audit");
    check(materialized_audits.size() == 2,
        "materialized-only formula same-editor saved materialized audit should report saved references");

    const auto check_materialized_audit =
        [&](std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    materialized_audits, 2, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the materialized audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the current Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    check_materialized_audit(
        "Data!A1", "A1",
        "materialized-only formula same-editor saved materialized audit A reference");
    check_materialized_audit(
        "Data!B1", "B1",
        "materialized-only formula same-editor saved materialized audit B reference");
    check(!sheet.has_pending_changes() &&
            editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula same-editor saved audits should keep materialized state clean");
    check_materialized_only_formula_saved_reopen_snapshots(
        sheet, expected_formula, "materialized-only formula same-editor saved audit live handle");
    check(!sheet.has_pending_changes(),
        "materialized-only formula same-editor saved audit live snapshots should keep the sheet clean");
    check(editor.pending_change_count() == 1,
        "materialized-only formula same-editor saved audit live snapshots should preserve one handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "materialized-only formula same-editor saved audit live snapshots should keep dirty materialized diagnostics empty");
    check(editor.pending_worksheet_edits().empty(),
        "materialized-only formula same-editor saved audit live snapshots should keep dirty summaries empty");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "materialized-only formula same-editor saved audit no-op should keep the sheet clean");
    check(editor.pending_change_count() == 1,
        "materialized-only formula same-editor saved audit no-op should preserve one handoff");
    check(editor.pending_worksheet_edits().empty(),
        "materialized-only formula same-editor saved audit no-op should keep dirty summaries empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "materialized-only formula same-editor saved audit no-op");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "materialized-only formula same-editor saved audit no-op");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "materialized-only formula same-editor saved audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "materialized-only formula same-editor saved audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula same-editor saved audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula same-editor saved audit no-op reopen");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C2");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "materialized-only formula same-editor saved audit no-op should reopen the saved formula");
    check_materialized_only_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula,
        "materialized-only formula same-editor saved audit no-op reopen");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened,
        "materialized-only formula same-editor saved audit no-op after snapshots");
}

void test_public_worksheet_editor_materialized_only_formula_saved_reopen_audits_saved_formula()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-reopen-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-materialized-only-formula-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!A1+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(2, 3, fastxlsx::CellValue::formula(std::string(expected_formula)));

        const fastxlsx::CellValue current_formula = sheet.get_cell("C2");
        check(current_formula.kind() == fastxlsx::CellValueKind::Formula &&
                current_formula.text_value() == expected_formula,
            "materialized-only formula saved reopen audit setup should expose the dirty formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "materialized-only formula saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "materialized-only formula saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
            "materialized-only formula saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "materialized-only formula saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 2, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "materialized-only formula saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula saved reopen after source audit");
    check(source_audits.size() == 2,
        "materialized-only formula saved reopen source audit should report saved references");
    check_saved_audit(
        source_audits, "Data!A1", "A1",
        "materialized-only formula saved reopen source audit A reference");
    check_saved_audit(
        source_audits, "Data!B1", "B1",
        "materialized-only formula saved reopen source audit B reference");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "materialized-only formula saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C2");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "materialized-only formula saved reopen audit should read the saved formula");
    check_materialized_only_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "materialized-only formula saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "materialized-only formula saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula saved reopen after materialized audit");
    check(materialized_audits.size() == 2,
        "materialized-only formula saved reopen materialized audit should report saved references");
    check_saved_audit(
        materialized_audits, "Data!A1", "A1",
        "materialized-only formula saved reopen materialized audit A reference");
    check_saved_audit(
        materialized_audits, "Data!B1", "B1",
        "materialized-only formula saved reopen materialized audit B reference");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "materialized-only formula saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "materialized-only formula saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "materialized-only formula saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "materialized-only formula saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "materialized-only formula saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "materialized-only formula saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "materialized-only formula saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "materialized-only formula saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "materialized-only formula saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C2");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "materialized-only formula saved reopen audit no-op output should read the saved formula");
    check_materialized_only_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "materialized-only formula saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "materialized-only formula saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_saved_reopen_audits_saved_rewrite()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-reopen-audit-source.xlsx",
            "Data!A3+Data!B1");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!A4+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_rows(3, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula saved reopen after source audit");
    check(source_audits.size() == 2,
        "stationary formula saved reopen source audit should report both saved references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A3") == nullptr,
        "stationary formula saved reopen source audit should not report the original A3 reference");
    check_saved_audit(
        source_audits, "Data!A4", "A4",
        "stationary formula saved reopen source audit shifted A reference");
    check_saved_audit(
        source_audits, "Data!B1", "B1",
        "stationary formula saved reopen source audit stable B reference");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula saved reopen after materialized audit");
    check(materialized_audits.size() == 2,
        "stationary formula saved reopen materialized audit should report both saved references");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A3") == nullptr,
        "stationary formula saved reopen materialized audit should not report the original A3 reference");
    check_saved_audit(
        materialized_audits, "Data!A4", "A4",
        "stationary formula saved reopen materialized audit shifted A reference");
    check_saved_audit(
        materialized_audits, "Data!B1", "B1",
        "stationary formula saved reopen materialized audit stable B reference");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_delete_saved_reopen_audits_skip_ref()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-reopen-audit-source.xlsx",
            "Data!A3+Data!B1");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!#REF!+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_rows(3, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula delete saved reopen audit setup should expose the #REF! formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula delete saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula delete saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula delete saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula delete saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula delete saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete saved reopen audit setup");

    const auto check_surviving_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(audits, 1, 3, "Data!B1");
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the surviving saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == "B1" &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report the surviving saved token");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula delete saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete saved reopen after source audit");
    check(source_audits.size() == 1,
        "stationary formula delete saved reopen source audit should report only the surviving reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A3") == nullptr,
        "stationary formula delete saved reopen source audit should not report the original A3 reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete saved reopen source audit should skip Data!#REF!");
    check_surviving_audit(
        source_audits, "stationary formula delete saved reopen source audit");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula delete saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula delete saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula delete saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula delete saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete saved reopen after materialized audit");
    check(materialized_audits.size() == 1,
        "stationary formula delete saved reopen materialized audit should report only the surviving reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A3") == nullptr,
        "stationary formula delete saved reopen materialized audit should not report the original A3 reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete saved reopen materialized audit should skip Data!#REF!");
    check_surviving_audit(
        materialized_audits, "stationary formula delete saved reopen materialized audit");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula delete saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula delete saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula delete saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula delete saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula delete saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula delete saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula delete saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula delete saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula delete saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula delete saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula delete saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_column_saved_reopen_audits_saved_rewrite()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-reopen-audit-source.xlsx",
            "Data!D1+Data!B1");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!E1+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_columns(4, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula column saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula column saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula column saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula column saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula column saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula column saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula column saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column saved reopen after source audit");
    check(source_audits.size() == 2,
        "stationary formula column saved reopen source audit should report both saved references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!D1") == nullptr,
        "stationary formula column saved reopen source audit should not report the original D1 reference");
    check_saved_audit(
        source_audits, "Data!E1", "E1",
        "stationary formula column saved reopen source audit shifted E reference");
    check_saved_audit(
        source_audits, "Data!B1", "B1",
        "stationary formula column saved reopen source audit stable B reference");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula column saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula column saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula column saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula column saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column saved reopen after materialized audit");
    check(materialized_audits.size() == 2,
        "stationary formula column saved reopen materialized audit should report both saved references");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!D1") == nullptr,
        "stationary formula column saved reopen materialized audit should not report the original D1 reference");
    check_saved_audit(
        materialized_audits, "Data!E1", "E1",
        "stationary formula column saved reopen materialized audit shifted E reference");
    check_saved_audit(
        materialized_audits, "Data!B1", "B1",
        "stationary formula column saved reopen materialized audit stable B reference");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula column saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula column saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula column saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula column saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula column saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula column saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula column saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula column saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula column saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula column saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula column saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_delete_column_saved_reopen_audits_skip_ref()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-column-reopen-audit-source.xlsx",
            "Data!D1+Data!B1");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-column-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-delete-column-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula = "Data!#REF!+Data!B1";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.delete_columns(4, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula delete-column saved reopen audit setup should expose the #REF! formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula delete-column saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula delete-column saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula delete-column saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula delete-column saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula delete-column saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete-column saved reopen audit setup");

    const auto check_surviving_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(audits, 1, 3, "Data!B1");
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the surviving saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == "B1" &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report the surviving saved token");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula delete-column saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete-column saved reopen after source audit");
    check(source_audits.size() == 1,
        "stationary formula delete-column saved reopen source audit should report only the surviving reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!D1") == nullptr,
        "stationary formula delete-column saved reopen source audit should not report the original D1 reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete-column saved reopen source audit should skip Data!#REF!");
    check_surviving_audit(
        source_audits, "stationary formula delete-column saved reopen source audit");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula delete-column saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula delete-column saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula delete-column saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula delete-column saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete-column saved reopen after materialized audit");
    check(materialized_audits.size() == 1,
        "stationary formula delete-column saved reopen materialized audit should report only the surviving reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!D1") == nullptr,
        "stationary formula delete-column saved reopen materialized audit should not report the original D1 reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!#REF!") == nullptr,
        "stationary formula delete-column saved reopen materialized audit should skip Data!#REF!");
    check_surviving_audit(
        materialized_audits, "stationary formula delete-column saved reopen materialized audit");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula delete-column saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula delete-column saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula delete-column saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula delete-column saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula delete-column saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula delete-column saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula delete-column saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula delete-column saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula delete-column saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula delete-column saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula delete-column saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula delete-column saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_range_saved_reopen_audits_saved_rewrite()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-reopen-audit-source.xlsx",
            "SUM(Data!A3:B3)+Data!3:3+Data!A1:B4+Data!1:4");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-range-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula =
        "SUM(Data!A4:B4)+Data!4:4+Data!A1:B5+Data!1:5";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_rows(3, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula range saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula range saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula range saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula range saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula range saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula range saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula range saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula range saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula range saved reopen after source audit");
    check(source_audits.size() == 4,
        "stationary formula range saved reopen source audit should report saved references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A3:B3") == nullptr,
        "stationary formula range saved reopen source audit should not report the original row range");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!3:3") == nullptr,
        "stationary formula range saved reopen source audit should not report the original whole row");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A1:B4") == nullptr,
        "stationary formula range saved reopen source audit should not report the original split row range");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!1:4") == nullptr,
        "stationary formula range saved reopen source audit should not report the original split whole row");
    check_saved_audit(
        source_audits, "Data!A4:B4", "A4:B4",
        "stationary formula range saved reopen source audit shifted row range");
    check_saved_audit(
        source_audits, "Data!4:4", "4:4",
        "stationary formula range saved reopen source audit shifted whole row");
    check_saved_audit(
        source_audits, "Data!A1:B5", "A1:B5",
        "stationary formula range saved reopen source audit split row range");
    check_saved_audit(
        source_audits, "Data!1:5", "1:5",
        "stationary formula range saved reopen source audit split whole row");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula range saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula range saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula range saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula range saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula range saved reopen after materialized audit");
    check(materialized_audits.size() == 4,
        "stationary formula range saved reopen materialized audit should report saved references");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A3:B3") == nullptr,
        "stationary formula range saved reopen materialized audit should not report the original row range");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!3:3") == nullptr,
        "stationary formula range saved reopen materialized audit should not report the original whole row");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A1:B4") == nullptr,
        "stationary formula range saved reopen materialized audit should not report the original split row range");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!1:4") == nullptr,
        "stationary formula range saved reopen materialized audit should not report the original split whole row");
    check_saved_audit(
        materialized_audits, "Data!A4:B4", "A4:B4",
        "stationary formula range saved reopen materialized audit shifted row range");
    check_saved_audit(
        materialized_audits, "Data!4:4", "4:4",
        "stationary formula range saved reopen materialized audit shifted whole row");
    check_saved_audit(
        materialized_audits, "Data!A1:B5", "A1:B5",
        "stationary formula range saved reopen materialized audit split row range");
    check_saved_audit(
        materialized_audits, "Data!1:5", "1:5",
        "stationary formula range saved reopen materialized audit split whole row");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula range saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula range saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula range saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula range saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula range saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula range saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula range saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula range saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula range saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula range saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula range saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula range saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_stationary_formula_column_range_saved_reopen_audits_saved_rewrite()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_stationary_formula(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-range-reopen-audit-source.xlsx",
            "SUM(Data!D1:E1)+Data!D:E+Data!A1:F2+Data!A:F");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-range-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-stationary-formula-column-range-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula =
        "SUM(Data!E1:F1)+Data!E:F+Data!A1:G2+Data!A:G";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.insert_columns(4, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "stationary formula column range saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "stationary formula column range saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "stationary formula column range saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "stationary formula column range saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula column range saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "stationary formula column range saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column range saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula column range saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column range saved reopen after source audit");
    check(source_audits.size() == 4,
        "stationary formula column range saved reopen source audit should report saved references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!D1:E1") == nullptr,
        "stationary formula column range saved reopen source audit should not report the original column range");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!D:E") == nullptr,
        "stationary formula column range saved reopen source audit should not report the original whole columns");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A1:F2") == nullptr,
        "stationary formula column range saved reopen source audit should not report the original split column range");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A:F") == nullptr,
        "stationary formula column range saved reopen source audit should not report the original split whole columns");
    check_saved_audit(
        source_audits, "Data!E1:F1", "E1:F1",
        "stationary formula column range saved reopen source audit shifted column range");
    check_saved_audit(
        source_audits, "Data!E:F", "E:F",
        "stationary formula column range saved reopen source audit shifted whole columns");
    check_saved_audit(
        source_audits, "Data!A1:G2", "A1:G2",
        "stationary formula column range saved reopen source audit split column range");
    check_saved_audit(
        source_audits, "Data!A:G", "A:G",
        "stationary formula column range saved reopen source audit split whole columns");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "stationary formula column range saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "stationary formula column range saved reopen audit should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "stationary formula column range saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "stationary formula column range saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column range saved reopen after materialized audit");
    check(materialized_audits.size() == 4,
        "stationary formula column range saved reopen materialized audit should report saved references");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!D1:E1") == nullptr,
        "stationary formula column range saved reopen materialized audit should not report the original column range");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!D:E") == nullptr,
        "stationary formula column range saved reopen materialized audit should not report the original whole columns");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A1:F2") == nullptr,
        "stationary formula column range saved reopen materialized audit should not report the original split column range");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A:F") == nullptr,
        "stationary formula column range saved reopen materialized audit should not report the original split whole columns");
    check_saved_audit(
        materialized_audits, "Data!E1:F1", "E1:F1",
        "stationary formula column range saved reopen materialized audit shifted column range");
    check_saved_audit(
        materialized_audits, "Data!E:F", "E:F",
        "stationary formula column range saved reopen materialized audit shifted whole columns");
    check_saved_audit(
        materialized_audits, "Data!A1:G2", "A1:G2",
        "stationary formula column range saved reopen materialized audit split column range");
    check_saved_audit(
        materialized_audits, "Data!A:G", "A:G",
        "stationary formula column range saved reopen materialized audit split whole columns");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "stationary formula column range saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "stationary formula column range saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "stationary formula column range saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "stationary formula column range saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "stationary formula column range saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "stationary formula column range saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "stationary formula column range saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "stationary formula column range saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "stationary formula column range saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "stationary formula column range saved reopen audit no-op output should read the saved formula");
    check_stationary_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "stationary formula column range saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "stationary formula column range saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_delete_row_ref_formula_saved_reopen_audits_skip_ref()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-delete-row-ref-formula-reopen-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-row-ref-formula-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-delete-row-ref-formula-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula =
        "Data!#REF!+Data!A:A+Data!#REF!+Data!B3";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(4, 3,
            fastxlsx::CellValue::formula("Data!A1+Data!A:A+Data!1:1+Data!B4"));
        sheet.delete_rows(1, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C3");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "delete-row #REF saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "delete-row #REF saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "delete-row #REF saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "delete-row #REF saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete-row #REF saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "delete-row #REF saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-row #REF saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 3, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "delete-row #REF saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-row #REF saved reopen after source audit");
    check(source_audits.size() == 2,
        "delete-row #REF saved reopen source audit should report surviving saved references");
    check(find_public_state_formula_audit(source_audits, 3, 3, "Data!A1") == nullptr,
        "delete-row #REF saved reopen source audit should not report the original deleted A1 reference");
    check(find_public_state_formula_audit(source_audits, 3, 3, "Data!1:1") == nullptr,
        "delete-row #REF saved reopen source audit should not report the original deleted whole row");
    check(find_public_state_formula_audit(source_audits, 3, 3, "Data!B4") == nullptr,
        "delete-row #REF saved reopen source audit should not report the original B4 reference");
    check(find_public_state_formula_audit(source_audits, 3, 3, "Data!#REF!") == nullptr,
        "delete-row #REF saved reopen source audit should skip Data!#REF!");
    check_saved_audit(
        source_audits, "Data!A:A", "A:A",
        "delete-row #REF saved reopen source audit surviving whole column");
    check_saved_audit(
        source_audits, "Data!B3", "B3",
        "delete-row #REF saved reopen source audit surviving shifted cell");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "delete-row #REF saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C3");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "delete-row #REF saved reopen audit should read the saved formula");
    check_delete_row_ref_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "delete-row #REF saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "delete-row #REF saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-row #REF saved reopen after materialized audit");
    check(materialized_audits.size() == 2,
        "delete-row #REF saved reopen materialized audit should report surviving saved references");
    check(find_public_state_formula_audit(materialized_audits, 3, 3, "Data!A1") == nullptr,
        "delete-row #REF saved reopen materialized audit should not report the original deleted A1 reference");
    check(find_public_state_formula_audit(materialized_audits, 3, 3, "Data!1:1") == nullptr,
        "delete-row #REF saved reopen materialized audit should not report the original deleted whole row");
    check(find_public_state_formula_audit(materialized_audits, 3, 3, "Data!B4") == nullptr,
        "delete-row #REF saved reopen materialized audit should not report the original B4 reference");
    check(find_public_state_formula_audit(materialized_audits, 3, 3, "Data!#REF!") == nullptr,
        "delete-row #REF saved reopen materialized audit should skip Data!#REF!");
    check_saved_audit(
        materialized_audits, "Data!A:A", "A:A",
        "delete-row #REF saved reopen materialized audit surviving whole column");
    check_saved_audit(
        materialized_audits, "Data!B3", "B3",
        "delete-row #REF saved reopen materialized audit surviving shifted cell");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "delete-row #REF saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "delete-row #REF saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "delete-row #REF saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-row #REF saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "delete-row #REF saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "delete-row #REF saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "delete-row #REF saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete-row #REF saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "delete-row #REF saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C3");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "delete-row #REF saved reopen audit no-op output should read the saved formula");
    check_delete_row_ref_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "delete-row #REF saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "delete-row #REF saved reopen audit no-op output after snapshots");
}

void test_public_worksheet_editor_delete_column_ref_formula_saved_reopen_audits_skip_ref()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-public-worksheet-delete-column-ref-formula-reopen-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-column-ref-formula-reopen-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact(
            "fastxlsx-workbook-editor-public-worksheet-delete-column-ref-formula-reopen-audit-noop-output.xlsx");

    constexpr std::string_view expected_formula =
        "Data!#REF!+Data!#REF!+Data!1:1+Data!C2";
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        sheet.set_cell(1, 4,
            fastxlsx::CellValue::formula("Data!A1+Data!A:A+Data!1:1+Data!D2"));
        sheet.delete_columns(1, 1);

        const fastxlsx::CellValue rewritten_formula = sheet.get_cell("C1");
        check(rewritten_formula.kind() == fastxlsx::CellValueKind::Formula &&
                rewritten_formula.text_value() == expected_formula,
            "delete-column #REF saved reopen audit setup should expose the rewritten formula");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "delete-column #REF saved reopen audit setup should clean the materialized sheet");
        check(editor.pending_change_count() == 1,
            "delete-column #REF saved reopen audit setup should record one materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "delete-column #REF saved reopen audit setup should clear materialized diagnostics");
    }

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete-column #REF saved reopen audit setup should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data") && reopened.has_worksheet("Untouched"),
        "delete-column #REF saved reopen audit should expose saved worksheets");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-column #REF saved reopen audit setup");

    const auto check_saved_audit =
        [&](const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
            std::string_view qualified_reference_text,
            std::string_view reference_text,
            std::string_view message_prefix) {
            const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
                find_public_state_formula_audit(
                    audits, 1, 3, qualified_reference_text);
            check(audit != nullptr,
                std::string(message_prefix) + " should expose the saved audit entry");
            if (audit == nullptr) {
                return;
            }

            check(audit->formula_sheet_source_name == "Data" &&
                    audit->formula_sheet_planned_name == "Data" &&
                    audit->formula_text == expected_formula,
                std::string(message_prefix) + " should report the saved formula cell");
            check(audit->sheet_qualifier_text == "Data!" &&
                    audit->reference_text == reference_text &&
                    audit->referenced_sheet_name == "Data",
                std::string(message_prefix) + " should report saved formula tokens");
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Data",
                std::string(message_prefix) + " should match the reopened Data sheet");
            check(!audit->references_renamed_source_name &&
                    audit->references_planned_sheet_name &&
                    !audit->external_workbook_qualifier &&
                    !audit->sheet_range_qualifier,
                std::string(message_prefix) + " should keep qualifier flags clean");
        };

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            reopened, "delete-column #REF saved reopen source audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-column #REF saved reopen after source audit");
    check(source_audits.size() == 2,
        "delete-column #REF saved reopen source audit should report surviving saved references");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A1") == nullptr,
        "delete-column #REF saved reopen source audit should not report the original deleted A1 reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!A:A") == nullptr,
        "delete-column #REF saved reopen source audit should not report the original deleted whole column");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!D2") == nullptr,
        "delete-column #REF saved reopen source audit should not report the original D2 reference");
    check(find_public_state_formula_audit(source_audits, 1, 3, "Data!#REF!") == nullptr,
        "delete-column #REF saved reopen source audit should skip Data!#REF!");
    check_saved_audit(
        source_audits, "Data!1:1", "1:1",
        "delete-column #REF saved reopen source audit surviving whole row");
    check_saved_audit(
        source_audits, "Data!C2", "C2",
        "delete-column #REF saved reopen source audit surviving shifted cell");

    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("Data");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "delete-column #REF saved reopen audit should materialize Data cleanly");
    const std::optional<fastxlsx::CellValue> reopened_formula =
        reopened_sheet.try_cell("C1");
    check(reopened_formula.has_value() &&
            reopened_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula->text_value() == expected_formula,
        "delete-column #REF saved reopen audit should read the saved formula");
    check_delete_column_ref_formula_saved_reopen_snapshots(
        reopened_sheet, expected_formula, "delete-column #REF saved reopen audit");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            reopened, "delete-column #REF saved reopen materialized audit");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-column #REF saved reopen after materialized audit");
    check(materialized_audits.size() == 2,
        "delete-column #REF saved reopen materialized audit should report surviving saved references");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A1") == nullptr,
        "delete-column #REF saved reopen materialized audit should not report the original deleted A1 reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!A:A") == nullptr,
        "delete-column #REF saved reopen materialized audit should not report the original deleted whole column");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!D2") == nullptr,
        "delete-column #REF saved reopen materialized audit should not report the original D2 reference");
    check(find_public_state_formula_audit(materialized_audits, 1, 3, "Data!#REF!") == nullptr,
        "delete-column #REF saved reopen materialized audit should skip Data!#REF!");
    check_saved_audit(
        materialized_audits, "Data!1:1", "1:1",
        "delete-column #REF saved reopen materialized audit surviving whole row");
    check_saved_audit(
        materialized_audits, "Data!C2", "C2",
        "delete-column #REF saved reopen materialized audit surviving shifted cell");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(reopened);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(reopened);
    reopened.save_as(noop_output);
    check(reopened.pending_change_count() == 0,
        "delete-column #REF saved reopen audit no-op should keep pending changes empty");
    check(!reopened_sheet.has_pending_changes(),
        "delete-column #REF saved reopen audit no-op should keep the materialized sheet clean");
    check_workbook_editor_no_replacement_diagnostics(
        reopened, "delete-column #REF saved reopen audit no-op");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened, "delete-column #REF saved reopen audit no-op");
    check_workbook_editor_public_save_state_preserved(
        reopened, save_state_before_noop,
        "delete-column #REF saved reopen audit no-op");
    check_workbook_editor_public_catalog_preserved(
        reopened, catalog_before_noop,
        "delete-column #REF saved reopen audit no-op");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "delete-column #REF saved reopen audit no-op should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete-column #REF saved reopen audit no-op should leave the source package unchanged");

    fastxlsx::WorkbookEditor noop_reopened = fastxlsx::WorkbookEditor::open(noop_output);
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened, "delete-column #REF saved reopen audit no-op output");
    fastxlsx::WorksheetEditor noop_sheet = noop_reopened.worksheet("Data");
    const std::optional<fastxlsx::CellValue> noop_formula =
        noop_sheet.try_cell("C1");
    check(noop_formula.has_value() &&
            noop_formula->kind() == fastxlsx::CellValueKind::Formula &&
            noop_formula->text_value() == expected_formula,
        "delete-column #REF saved reopen audit no-op output should read the saved formula");
    check_delete_column_ref_formula_saved_reopen_snapshots(
        noop_sheet, expected_formula, "delete-column #REF saved reopen audit no-op output");
    check_public_state_reopened_formula_audit_clean_editor(
        noop_reopened,
        "delete-column #REF saved reopen audit no-op output after snapshots");
}


} // namespace

int main()
{
    try {
            test_public_worksheet_editor_shifts_rewrite_stationary_formula_references();
            test_public_worksheet_editor_stationary_formula_shift_audits_rewritten_references();
            test_public_worksheet_editor_stationary_formula_delete_audits_skip_ref();
            test_public_worksheet_editor_stationary_formula_source_audits_preserve_source_scan();
            test_public_worksheet_editor_stationary_formula_delete_source_audits_preserve_source_scan();
            test_public_worksheet_editor_stationary_formula_range_source_audits_preserve_source_scan();
            test_public_worksheet_editor_delete_ref_formula_source_audits_preserve_source_scan();
            test_public_worksheet_editor_materialized_only_formula_source_audits_ignore_dirty_formula();
            test_public_worksheet_editor_materialized_only_formula_failed_save_preserves_audits();
            test_public_worksheet_editor_materialized_only_formula_failed_save_noop_preserves_output();
            test_public_worksheet_editor_materialized_only_formula_same_editor_saved_audits();
            test_public_worksheet_editor_materialized_only_formula_saved_reopen_audits_saved_formula();
            test_public_worksheet_editor_stationary_formula_saved_reopen_audits_saved_rewrite();
            test_public_worksheet_editor_stationary_formula_delete_saved_reopen_audits_skip_ref();
            test_public_worksheet_editor_stationary_formula_column_saved_reopen_audits_saved_rewrite();
            test_public_worksheet_editor_stationary_formula_delete_column_saved_reopen_audits_skip_ref();
            test_public_worksheet_editor_stationary_formula_range_saved_reopen_audits_saved_rewrite();
            test_public_worksheet_editor_stationary_formula_column_range_saved_reopen_audits_saved_rewrite();
            test_public_worksheet_editor_delete_row_ref_formula_saved_reopen_audits_skip_ref();
            test_public_worksheet_editor_delete_column_ref_formula_saved_reopen_audits_skip_ref();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state shift formula audit tests passed\n");
    return 0;
}
