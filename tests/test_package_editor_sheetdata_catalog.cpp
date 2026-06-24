#include "test_package_editor_sheetdata_catalog_common.hpp"

void test_package_editor_renames_sheet_catalog_entry_preserving_parts()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst><ext><sheets><sheet name="Decoy Sheet" sheetId="777" r:id="rId1"/></sheets></ext></extLst>)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    editor.rename_sheet_catalog_entry("Sheet1", "Renamed & Data");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheet catalog rename should record workbook rewrite in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet catalog rename should use local-DOM rewrite for workbook XML");
    check(workbook_plan->reason.find("sheet catalog") != std::string::npos,
        "sheet catalog rename plan reason should name the catalog rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheet catalog rename", "not synchronized"}),
        "sheet catalog rename should audit unsynchronized linked references");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "sheet catalog rename should record structured sheet catalog audit");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "sheet catalog rename should record structured definedNames audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "sheet catalog rename should not request recalculation by default");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "sheet catalog rename should not change calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet catalog rename should mark workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename should leave calcChain copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheet catalog rename output plan should rewrite workbook XML");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet catalog rename output plan should preserve unknown extension bytes");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "sheet catalog rename output plan should snapshot sheet catalog audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "sheet catalog rename output plan should snapshot definedNames audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
    check(output_reader.worksheet_part_by_sheet_name("Renamed & Data") == worksheet_part,
        "sheet catalog rename output should expose the new sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "sheet catalog rename output should no longer expose the old sheet name");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "sheet catalog rename should XML-escape the new sheet name");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "sheet catalog rename should preserve definedNames without semantic sync");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sheet catalog rename should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sheet catalog rename should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sheet catalog rename should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet catalog rename should preserve unknown extension payload");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "sheet catalog rename should preserve unknown owner relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet catalog rename should keep unknown extension default content type");
}

void test_package_editor_sheet_catalog_rename_rewrites_defined_names_opt_in()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-defined-names-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames>)"
        R"(<definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName>)"
        R"(<definedName name="External">[Book.xlsx]Sheet1!$A$1</definedName>)"
        R"(<definedName name="ThreeD">Sheet1:Other!$A$1</definedName>)"
        R"(</definedNames></workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-defined-names-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    fastxlsx::detail::SheetCatalogRenameOptions options;
    options.formula_policy =
        fastxlsx::detail::SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    editor.rename_sheet_catalog_entry("Sheet1", "Renamed & Data", {}, options);

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "opt-in definedName rename should record workbook rewrite");
    check(workbook_plan->reason.find("definedName formula references") != std::string::npos,
        "opt-in definedName rename plan reason should name definedName formula rewrite");
    check(has_note_containing(editor.edit_plan().notes(),
              {"definedName", "opt-in narrow policy", "worksheet formulas"}),
        "opt-in definedName rename should audit the narrow formula policy");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedName", "opt-in narrow sync"}),
        "opt-in definedName rename should record structured definedName rewrite audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "opt-in definedName rename should still XML-escape the sheet catalog name");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'Renamed &amp; Data'!$A$1:$B$2</definedName>)",
        "opt-in definedName rename should rewrite direct local definedName formulas");
    check_contains(workbook_xml,
        R"(<definedName name="External">[Book.xlsx]Sheet1!$A$1</definedName>)",
        "opt-in definedName rename should preserve external workbook references");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeD">Sheet1:Other!$A$1</definedName>)",
        "opt-in definedName rename should preserve 3D sheet-range references");
}

void test_package_editor_sheet_catalog_rename_defined_name_rewrite_failure_is_clean()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-defined-name-failure-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="Nested"><x>Sheet1!A1</x></definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_workbook_payload_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();

    fastxlsx::detail::SheetCatalogRenameOptions options;
    options.formula_policy =
        fastxlsx::detail::SheetCatalogRenameFormulaPolicy::RewriteDefinedNames;
    bool failed = false;
    try {
        editor.rename_sheet_catalog_entry("Sheet1", "Renamed", {}, options);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "nested XML in definedName text",
            "definedName rewrite failure should report malformed definedName XML");
    }
    check(failed,
        "opt-in definedName rewrite should reject nested definedName XML before state changes");
    check(editor.edit_plan().size() == initial_plan_size,
        "definedName rewrite failure should preserve edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "definedName rewrite failure should not add notes");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_audit_count,
        "definedName rewrite failure should not add workbook payload audits");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "definedName rewrite failure should leave workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "definedName rewrite failure should leave worksheet copy-original");
}

void test_package_editor_sheet_catalog_rename_uses_planned_workbook_xml()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-planned-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheet-catalog-rename-planned-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary planned workbook sheet catalog before rename");

    bool old_source_name_failed = false;
    try {
        editor.rename_sheet_catalog_entry("Sheet1", "Final");
    } catch (const std::exception& error) {
        old_source_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned sheet catalog rename should use planned sheet names");
    }
    check(old_source_name_failed,
        "sheet catalog rename should reject the old source name once planned workbook XML exists");

    editor.rename_sheet_catalog_entry("Planned", "Final");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned sheet catalog rename should keep workbook local-DOM-rewrite");
    check(workbook_plan->reason.find("sheet catalog") != std::string::npos,
        "planned sheet catalog rename should replace the prior ordinary workbook reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned sheet catalog rename should leave worksheet copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned sheet catalog rename output plan should expose final workbook rewrite");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve calcChain bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned sheet catalog rename output plan should preserve unknown extension bytes");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::SheetCatalog,
              WorkbookAuditScope::SheetCatalogRename, "sheets/sheet@name",
              {"sheet catalog rename", "sheet name attribute"}),
        "planned sheet catalog rename output plan should snapshot sheet catalog audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::SheetCatalogRename, "definedNames",
              {"definedNames", "without semantic sync"}),
        "planned sheet catalog rename output plan should snapshot definedNames audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Final") == worksheet_part,
        "planned sheet catalog rename output should expose the final sheet name");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Final")",
        "planned sheet catalog rename should write the final sheet name");
    check_not_contains(workbook_xml, R"(name="Planned")",
        "planned sheet catalog rename should remove the intermediate planned sheet name");
    check_not_contains(workbook_xml, R"(name="Sheet1")",
        "planned sheet catalog rename should not resurrect the source sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned sheet catalog rename should preserve existing workbook calc metadata");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "planned sheet catalog rename should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "planned sheet catalog rename should preserve unknown extension bytes");
}

void test_package_editor_rejects_sheet_catalog_rename_without_state_changes()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheet-catalog-rename-failure-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Summary" sheetId="2" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_clean_failure =
        [&](auto&& operation, std::string_view expected_message, const char* context) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t initial_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t initial_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t initial_workbook_payload_dependency_audit_count =
                editor.edit_plan().workbook_payload_dependency_audits().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                operation(editor);
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_message, context);
            }
            check(failed, context);

            check(editor.edit_plan().size() == initial_plan_size,
                "sheet catalog rename failure should preserve edit-plan size");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "sheet catalog rename failure should not append notes");
            check(editor.edit_plan().relationship_target_audits().size()
                    == initial_relationship_target_audit_count,
                "sheet catalog rename failure should not append relationship target audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == initial_worksheet_relationship_reference_audit_count,
                "sheet catalog rename failure should not append worksheet relationship audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == initial_worksheet_payload_dependency_audit_count,
                "sheet catalog rename failure should not append worksheet payload audits");
            check(editor.edit_plan().workbook_payload_dependency_audits().size()
                    == initial_workbook_payload_dependency_audit_count,
                "sheet catalog rename failure should not append workbook payload audits");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "sheet catalog rename failure should preserve package-entry audit count");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "sheet catalog rename failure should preserve removed package-entry audit count");
            check(editor.edit_plan().removed_parts().empty(),
                "sheet catalog rename failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "sheet catalog rename failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "sheet catalog rename failure should not change calcChain policy");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave worksheet copy-original");
            check_manifest_write_mode(editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "sheet catalog rename failure should leave calcChain copy-original");
        };

    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Missing", "Renamed");
        },
        "workbook sheet name is not present",
        "PackageEditor should reject sheet catalog rename with missing old name");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "Sheet1");
        },
        "target name already exists",
        "PackageEditor should reject sheet catalog rename to an existing name");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "summary");
        },
        "target name already exists",
        "PackageEditor should reject case-insensitive sheet catalog rename conflicts");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            editor.rename_sheet_catalog_entry("Sheet1", "Bad/Name");
        },
        "invalid characters",
        "PackageEditor should reject invalid sheet catalog rename targets");
    expect_clean_failure(
        [](fastxlsx::detail::PackageEditor& editor) {
            fastxlsx::detail::ReferencePolicy policy;
            policy.unsupported_linked_part_action =
                fastxlsx::detail::ReferencePolicyAction::Fail;
            editor.rename_sheet_catalog_entry("Sheet1", "Renamed", policy);
        },
        "definedNames",
        "PackageEditor should reject sheet catalog rename with definedNames under fail policy");

    const auto expect_invalid_planned_rename_failure =
        [&](std::string_view source_name, std::string_view output_name,
            std::string planned_workbook, auto configure_source,
            std::string_view expected_error, const char* scenario_message) {
            LinkedObjectSourcePackage planned_source =
                write_sheet_data_patch_source_package(source_name);
            configure_source(planned_source);
            rewrite_linked_object_source_package(planned_source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(planned_source.path);
            editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned workbook catalog before rename");

            const std::size_t queued_plan_size = editor.edit_plan().size();
            const std::size_t queued_note_count = editor.edit_plan().notes().size();
            const std::size_t queued_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t queued_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t queued_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t queued_workbook_payload_dependency_audit_count =
                editor.edit_plan().workbook_payload_dependency_audits().size();
            const std::size_t queued_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t queued_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                editor.rename_sheet_catalog_entry("Broken", "Renamed");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog rename failure should explain the catalog issue");
            }
            check(failed, scenario_message);

            check(editor.edit_plan().size() == queued_plan_size,
                "invalid planned catalog rename failure should preserve queued edit-plan size");
            check(editor.edit_plan().notes().size() == queued_note_count,
                "invalid planned catalog rename failure should not append notes");
            check(editor.edit_plan().relationship_target_audits().size()
                    == queued_relationship_target_audit_count,
                "invalid planned catalog rename failure should not append relationship audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == queued_worksheet_relationship_reference_audit_count,
                "invalid planned catalog rename failure should not append worksheet audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == queued_worksheet_payload_dependency_audit_count,
                "invalid planned catalog rename failure should not append worksheet payload audits");
            check(editor.edit_plan().workbook_payload_dependency_audits().size()
                    == queued_workbook_payload_dependency_audit_count,
                "invalid planned catalog rename failure should not append workbook payload audits");
            check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
                "invalid planned catalog rename failure should preserve package-entry audit count");
            check(editor.edit_plan().removed_package_entries().size()
                    == queued_removed_package_entry_count,
                "invalid planned catalog rename failure should preserve removed package-entry audit count");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid planned catalog rename failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid planned catalog rename failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid planned catalog rename failure should not change calcChain policy");
            const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
            check(workbook_plan != nullptr
                    && workbook_plan->write_mode
                        == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog rename failure should keep queued workbook replacement");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog rename failure should keep workbook local-DOM-rewrite");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog rename failure should leave worksheet copy-original");
            check_manifest_write_mode(editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog rename failure should leave calcChain copy-original");

            editor.save_as(output_path(output_name));
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(
                editor.reader(), output_reader, workbook_part.zip_path());
            check(output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog rename output should keep queued workbook replacement");
            check(output_reader.read_entry("xl/worksheets/sheet1.xml")
                    == planned_source.worksheet,
                "invalid planned catalog rename output should preserve source worksheet bytes");
            check(output_reader.read_entry("xl/calcChain.xml") == planned_source.calc_chain,
                "invalid planned catalog rename output should preserve calcChain bytes");
            check(output_reader.read_entry("custom/opaque-extension.bin")
                    == planned_source.opaque_extension,
                "invalid planned catalog rename output should preserve unknown extension bytes");
        };

    const std::string missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_rename_failure(
        "fastxlsx-package-editor-sheet-catalog-rename-missing-rel-source.xlsx",
        "fastxlsx-package-editor-sheet-catalog-rename-missing-rel-output.xlsx",
        missing_relationship_id_workbook,
        [](LinkedObjectSourcePackage&) {},
        "relationship id is not present",
        "PackageEditor should reject sheet catalog rename with missing planned relationship id");

    const std::string unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_rename_failure(
        "fastxlsx-package-editor-sheet-catalog-rename-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-sheet-catalog-rename-unregistered-target-output.xlsx",
        unregistered_target_workbook,
        [](LinkedObjectSourcePackage& planned_source) {
            planned_source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject sheet catalog rename with unregistered planned worksheet target");

    fastxlsx::detail::PackageEditor removed_workbook_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    removed_workbook_editor.remove_part(workbook_part, "remove workbook before rename failure");
    const std::size_t queued_plan_size = removed_workbook_editor.edit_plan().size();
    const std::size_t queued_package_entry_count =
        removed_workbook_editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        removed_workbook_editor.edit_plan().removed_package_entries().size();
    bool removed_workbook_failed = false;
    try {
        removed_workbook_editor.rename_sheet_catalog_entry("Sheet1", "Renamed");
    } catch (const std::exception& error) {
        removed_workbook_failed = true;
        check_contains(error.what(), "requires the officeDocument workbook part",
            "workbook removal should make sheet catalog rename fail before state changes");
    }
    check(removed_workbook_failed,
        "PackageEditor should reject sheet catalog rename after planned workbook removal");
    check(removed_workbook_editor.edit_plan().size() == queued_plan_size,
        "sheet catalog rename failure after workbook removal should preserve queued plan size");
    check(removed_workbook_editor.edit_plan().package_entries().size()
            == queued_package_entry_count,
        "sheet catalog rename failure after workbook removal should preserve package-entry audit");
    check(removed_workbook_editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "sheet catalog rename failure after workbook removal should preserve removed package-entry audit");
    check(removed_workbook_editor.manifest().find_part(workbook_part) == nullptr,
        "sheet catalog rename failure after workbook removal should keep workbook removed");
    check_manifest_write_mode(removed_workbook_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheet catalog rename failure after workbook removal should leave worksheet copy-original");
}

void test_package_editor_rejects_invalid_planned_workbook_catalog_by_name_without_state_changes()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_invalid_planned_catalog_failure =
        [&](std::string_view source_name, std::string_view output_name,
            std::string planned_workbook, auto configure_source,
            std::string_view expected_error, const char* scenario_message) {
            LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(source_name);
            configure_source(source);
            rewrite_linked_object_source_package(source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned workbook catalog before by-name patch");

            const std::size_t queued_plan_size = editor.edit_plan().size();
            const std::size_t queued_note_count = editor.edit_plan().notes().size();
            const std::size_t queued_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t queued_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t queued_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t queued_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t queued_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            const auto check_no_state_change = [&]() {
                check(editor.edit_plan().size() == queued_plan_size,
                    "invalid planned catalog failure should preserve queued edit-plan size");
                check(editor.edit_plan().notes().size() == queued_note_count,
                    "invalid planned catalog failure should not append notes");
                check(editor.edit_plan().relationship_target_audits().size()
                        == queued_relationship_target_audit_count,
                    "invalid planned catalog failure should not append relationship target audits");
                check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                        == queued_worksheet_relationship_reference_audit_count,
                    "invalid planned catalog failure should not append worksheet relationship audits");
                check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                        == queued_worksheet_payload_dependency_audit_count,
                    "invalid planned catalog failure should not append worksheet payload audits");
                check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
                    "invalid planned catalog failure should preserve package-entry audit count");
                check(editor.edit_plan().removed_package_entries().size()
                        == queued_removed_package_entry_count,
                    "invalid planned catalog failure should preserve removed package-entry audit count");
                check(editor.edit_plan().removed_parts().empty(),
                    "invalid planned catalog failure should not record removed parts");
                check(!editor.edit_plan().full_calculation_on_load(),
                    "invalid planned catalog failure should not request recalculation");
                check(editor.edit_plan().calc_chain_action()
                        == fastxlsx::detail::CalcChainAction::Preserve,
                    "invalid planned catalog failure should not change calcChain policy");
                check_manifest_write_mode(editor, workbook_part,
                    fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                    "invalid planned catalog failure should keep workbook local-DOM-rewrite");
                check_manifest_write_mode(editor, worksheet_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid planned catalog failure should leave worksheet copy-original");
                check_manifest_write_mode(editor, calc_chain_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid planned catalog failure should leave calcChain copy-original");
            };

            bool failed = false;
            try {
                replace_worksheet_part_by_name_from_single_chunk_source(editor, "Broken", "<worksheet/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog worksheet failure should explain the catalog issue");
            }
            check(failed, scenario_message);
            check_no_state_change();

            failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Broken", "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid planned catalog sheetData failure should explain the catalog issue");
            }
            check(failed,
                "PackageEditor should reject by-name sheetData replacement with invalid planned catalog");
            check_no_state_change();

            editor.save_as(output_path(output_name));

            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
            check(output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog failure output should keep queued workbook replacement");
            check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
                "invalid planned catalog failure output should preserve source worksheet bytes");
            check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
                "invalid planned catalog failure output should preserve calcChain bytes");
            check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
                "invalid planned catalog failure output should preserve unknown extension bytes");
        };

    const std::string missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_failure(
        "fastxlsx-package-editor-invalid-planned-catalog-missing-rel-source.xlsx",
        "fastxlsx-package-editor-invalid-planned-catalog-missing-rel-output.xlsx",
        missing_relationship_id_workbook,
        [](LinkedObjectSourcePackage&) {},
        "relationship id is not present",
        "PackageEditor should reject planned workbook sheet ids missing from workbook relationships");

    const std::string unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Broken" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_failure(
        "fastxlsx-package-editor-invalid-planned-catalog-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-invalid-planned-catalog-unregistered-target-output.xlsx",
        unregistered_target_workbook,
        [](LinkedObjectSourcePackage& source) {
            source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject planned workbook worksheet targets absent from the package");
}

void test_package_editor_by_name_helpers_allow_workbook_metadata_rewrite()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-after-workbook-metadata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-after-workbook-metadata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.request_full_calculation();
    check(editor.edit_plan().find_part(workbook_part) != nullptr,
        "workbook metadata helper should queue workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "workbook metadata helper should queue stale calcChain removal");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="7"><c r="A7"><v>77</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "by-name helpers after workbook metadata helper should keep workbook local-DOM rewrite");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "by-name helpers after workbook metadata helper should local-DOM-rewrite the worksheet");
    check(editor.edit_plan().full_calculation_on_load(),
        "by-name helpers after workbook metadata helper should keep fullCalcOnLoad");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "by-name helpers after workbook metadata helper should keep calcChain removal policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "by-name helpers after workbook metadata helper should keep calcChain removed-part audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name helpers after workbook metadata helper should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name helpers after workbook metadata helper should expose worksheet local-DOM rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name helpers after workbook metadata helper should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name helpers after workbook metadata helper should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "by-name helpers after workbook metadata helper output should omit calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string worksheet_xml = output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "by-name helpers after workbook metadata helper output should include replacement sheetData");
    check_contains(worksheet_xml, R"(<dimension ref="A1:A1"/>)",
        "sheetData by-name helper should preserve the queued worksheet wrapper");
    check_contains(worksheet_xml, R"(<autoFilter ref="A1:A1"/>)",
        "sheetData by-name helper should preserve queued worksheet metadata");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "by-name helpers after workbook metadata helper output should request recalculation");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "by-name helpers after workbook metadata helper output should remove calcChain content type");
    check_not_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/calcChain",
        "by-name helpers after workbook metadata helper output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "by-name helpers after workbook metadata helper output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "by-name helpers after workbook metadata helper output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "by-name helpers after workbook metadata helper output should keep unknown extension default content type");
}

void test_package_editor_by_name_helpers_use_planned_catalog_after_workbook_metadata_rewrite()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-planned-after-workbook-metadata-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="./worksheets/../worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-planned-after-workbook-metadata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="RenamedAfterCalc" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">RenamedAfterCalc!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before calc metadata helper and by-name patch");
    editor.request_full_calculation();

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned catalog after workbook metadata helper should reject the old source sheet name");
    }
    check(failed,
        "PackageEditor should keep using planned sheet names after workbook metadata helper takes ownership");
    check(editor.edit_plan().size() == queued_plan_size,
        "old-name failure after workbook metadata helper should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "old-name failure after workbook metadata helper should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "old-name failure after workbook metadata helper should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "old-name failure after workbook metadata helper should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "old-name failure after workbook metadata helper should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "old-name failure after workbook metadata helper should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "old-name failure after workbook metadata helper should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "old-name failure after workbook metadata helper should preserve removed-part audit count");
    check(editor.edit_plan().full_calculation_on_load(),
        "old-name failure after workbook metadata helper should preserve fullCalcOnLoad");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "old-name failure after workbook metadata helper should preserve calcChain policy");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "old-name failure after workbook metadata helper should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "old-name failure after workbook metadata helper should leave worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "old-name failure after workbook metadata helper should keep calcChain omitted from manifest");

    const std::string queued_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="C3:C3"/>)"
        R"(<sheetData><row r="3"><c r="C3"><v>33</v></c></row></sheetData>)"
        R"(<autoFilter ref="C3:C3"/>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "RenamedAfterCalc", queued_worksheet);

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="9"><c r="A9"><v>99</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor,
        "RenamedAfterCalc", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog after workbook metadata helper should resolve renamed worksheet part");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog after workbook metadata helper should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog after workbook metadata helper should keep calcChain removed-part audit");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog after workbook metadata helper should keep calcChain omitted from manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog after workbook metadata helper output plan should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog after workbook metadata helper output plan should expose worksheet local-DOM rewrite");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog after workbook metadata helper output plan should omit calcChain");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog after workbook metadata helper output should omit calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("RenamedAfterCalc")
            == worksheet_part,
        "planned catalog after workbook metadata helper output should expose planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "planned catalog after workbook metadata helper output should not expose old source sheet name");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "planned catalog after workbook metadata helper output should include final sheetData");
    check_contains(worksheet_xml, R"(<dimension ref="C3:C3"/>)",
        "planned catalog after workbook metadata helper sheetData patch should preserve queued worksheet wrapper");
    check_contains(worksheet_xml, R"(<autoFilter ref="C3:C3"/>)",
        "planned catalog after workbook metadata helper sheetData patch should preserve queued metadata");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="RenamedAfterCalc")",
        "planned catalog after workbook metadata helper output should preserve planned sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog after workbook metadata helper output should keep full calculation request");
    check_contains(workbook_xml, "RenamedAfterCalc!$A$1:$B$2",
        "planned catalog after workbook metadata helper output should preserve planned definedNames text");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog after workbook metadata helper output should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog after workbook metadata helper should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog after workbook metadata helper output should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog after workbook metadata helper output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "planned catalog after workbook metadata helper output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog after workbook metadata helper output should keep unknown extension default content type");
}

void test_package_editor_by_name_helpers_reject_after_workbook_removal_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-by-name-after-workbook-removal-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-by-name-after-workbook-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(workbook_part,
        "explicit workbook removal before by-name helper lookup");

    const std::size_t removal_plan_size = editor.edit_plan().size();
    const std::size_t removal_note_count = editor.edit_plan().notes().size();
    const std::size_t removal_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t removal_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t removal_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t removal_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t removal_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t removal_removed_part_count =
        editor.edit_plan().removed_parts().size();

    const auto check_removal_state = [&]() {
        check(editor.edit_plan().size() == removal_plan_size,
            "by-name after workbook removal failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == removal_note_count,
            "by-name after workbook removal failure should preserve note count");
        check(editor.edit_plan().relationship_target_audits().size()
                == removal_relationship_target_audit_count,
            "by-name after workbook removal failure should preserve relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == removal_worksheet_relationship_reference_audit_count,
            "by-name after workbook removal failure should preserve worksheet relationship audits");
        check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                == removal_worksheet_payload_dependency_audit_count,
            "by-name after workbook removal failure should preserve worksheet payload audits");
        check(editor.edit_plan().package_entries().size() == removal_package_entry_count,
            "by-name after workbook removal failure should preserve package-entry audits");
        check(editor.edit_plan().removed_package_entries().size()
                == removal_removed_package_entry_count,
            "by-name after workbook removal failure should preserve removed package-entry audits");
        check(editor.edit_plan().removed_parts().size() == removal_removed_part_count,
            "by-name after workbook removal failure should preserve removed-part audits");
        check(!editor.edit_plan().full_calculation_on_load(),
            "by-name after workbook removal failure should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "by-name after workbook removal failure should preserve calcChain policy");
        check(editor.edit_plan().find_removed_part(workbook_part) != nullptr,
            "by-name after workbook removal failure should keep workbook removed");
        check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
                != nullptr,
            "by-name after workbook removal failure should keep workbook owner relationships omitted");
        check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels")
                == nullptr,
            "by-name after workbook removal failure should not restore workbook owner relationships");
        check(editor.manifest().find_part(workbook_part) == nullptr,
            "by-name after workbook removal failure should keep workbook absent from manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "by-name after workbook removal failure should leave worksheet copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "by-name after workbook removal failure should leave calcChain copy-original");
    };

    bool worksheet_failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        worksheet_failed = true;
        check_contains(error.what(), "workbook sheet catalog",
            "by-name worksheet failure after workbook removal should name the catalog");
        check_contains(error.what(), "removed",
            "by-name worksheet failure after workbook removal should name planned removal");
    }
    check(worksheet_failed,
        "PackageEditor should reject by-name worksheet replacement after workbook removal");
    check_removal_state();

    bool sheet_data_failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        sheet_data_failed = true;
        check_contains(error.what(), "workbook sheet catalog",
            "by-name sheetData failure after workbook removal should name the catalog");
        check_contains(error.what(), "removed",
            "by-name sheetData failure after workbook removal should name planned removal");
    }
    check(sheet_data_failed,
        "PackageEditor should reject by-name sheetData replacement after workbook removal");
    check_removal_state();

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name after workbook removal output plan should keep workbook omitted");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "by-name after workbook removal output plan should keep workbook relationships omitted");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name after workbook removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "by-name after workbook removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "by-name after workbook removal output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "by-name after workbook removal output should omit workbook owner relationships");
    check(entries.find("xl/worksheets/sheet1.xml") != entries.end(),
        "by-name after workbook removal output should keep worksheet part");
    check(entries.find("custom/opaque-extension.bin") != entries.end(),
        "by-name after workbook removal output should keep unknown extension");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "by-name after workbook removal output should remove workbook content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "by-name after workbook removal output should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "by-name after workbook removal output should not keep workbook owner relationships");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "by-name after workbook removal output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "by-name after workbook removal output should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "by-name after workbook removal output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "by-name after workbook removal output should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "by-name after workbook removal output should preserve unknown extension relationships");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "by-name after workbook removal output should keep worksheet content type");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "by-name after workbook removal output should keep calcChain content type");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "by-name after workbook removal output should keep unknown extension default content type");
}

void test_package_editor_rejects_worksheet_by_sheet_name_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-invalid-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-invalid-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Missing", "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "sheet name",
            "missing sheet-name worksheet replacement failure should name sheet lookup");
    }
    check(failed,
        "PackageEditor should reject worksheet replacement for a missing sheet name");
    check(editor.edit_plan().size() == initial_plan_size,
        "missing sheet-name worksheet replacement should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing sheet-name worksheet replacement should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing sheet-name worksheet replacement should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing sheet-name worksheet replacement should not add worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing sheet-name worksheet replacement should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "missing sheet-name worksheet replacement should not record package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing sheet-name worksheet replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "missing sheet-name worksheet replacement should not change calcChain policy");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "missing sheet-name worksheet replacement should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing sheet-name worksheet replacement should keep worksheet copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_rejects_sheet_name_lookup_with_invalid_office_document_without_state_changes()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-bad-office-document-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="https://example.invalid/workbook.xml" TargetMode="External"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-bad-office-document-output.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "invalid officeDocument lookup should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "invalid officeDocument lookup should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "invalid officeDocument lookup should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "invalid officeDocument lookup should not add worksheet relationship reference audits");
        check(editor.edit_plan().removed_parts().empty(),
            "invalid officeDocument lookup should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "invalid officeDocument lookup should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "invalid officeDocument lookup should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "invalid officeDocument lookup should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "invalid officeDocument lookup should not change calcChain policy");
        check(editor.manifest().find_part(calc_chain_part) != nullptr,
            "invalid officeDocument lookup should keep calcChain in the manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid officeDocument lookup should keep calcChain copy-original");
    };

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument",
            "invalid officeDocument worksheet replacement failure should name package entrypoint");
    }
    check(failed,
        "PackageEditor should reject by-name worksheet replacement with invalid officeDocument");
    check_no_state_change();

    failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument",
            "invalid officeDocument sheetData replacement failure should name package entrypoint");
    }
    check(failed,
        "PackageEditor should reject by-name sheetData replacement with invalid officeDocument");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_rejects_invalid_source_workbook_sheet_catalog_without_state_changes()
{
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const auto expect_source_catalog_failure =
        [&](std::string_view source_name, std::string_view output_name,
            auto configure_source, std::string_view expected_error,
            const char* scenario_message) {
            LinkedObjectSourcePackage source = write_sheet_data_patch_source_package(source_name);
            configure_source(source);
            rewrite_linked_object_source_package(source);

            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t initial_worksheet_relationship_reference_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t initial_worksheet_payload_dependency_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            const auto check_no_state_change = [&]() {
                check(editor.edit_plan().size() == initial_plan_size,
                    "invalid source catalog lookup should not change edit plan size");
                check(editor.edit_plan().notes().size() == initial_note_count,
                    "invalid source catalog lookup should not add audit notes");
                check(editor.edit_plan().relationship_target_audits().size()
                        == initial_relationship_target_audit_count,
                    "invalid source catalog lookup should not add relationship target audits");
                check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                        == initial_worksheet_relationship_reference_audit_count,
                    "invalid source catalog lookup should not add worksheet relationship audits");
                check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                        == initial_worksheet_payload_dependency_audit_count,
                    "invalid source catalog lookup should not add worksheet payload audits");
                check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                    "invalid source catalog lookup should not record package-entry audit");
                check(editor.edit_plan().removed_package_entries().size()
                        == initial_removed_package_entry_count,
                    "invalid source catalog lookup should not record removed package-entry audit");
                check(editor.edit_plan().removed_parts().empty(),
                    "invalid source catalog lookup should not record removed parts");
                check(!editor.edit_plan().full_calculation_on_load(),
                    "invalid source catalog lookup should not request recalculation");
                check(editor.edit_plan().calc_chain_action()
                        == fastxlsx::detail::CalcChainAction::Preserve,
                    "invalid source catalog lookup should not change calcChain policy");
                check(editor.manifest().find_part(calc_chain_part) != nullptr,
                    "invalid source catalog lookup should keep calcChain in the manifest");
                check_manifest_write_mode(editor, worksheet_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep worksheet copy-original");
                check_manifest_write_mode(editor, workbook_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep workbook copy-original");
                check_manifest_write_mode(editor, calc_chain_part,
                    fastxlsx::detail::PartWriteMode::CopyOriginal,
                    "invalid source catalog lookup should keep calcChain copy-original");
            };

            bool failed = false;
            try {
                replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<worksheet/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid source catalog worksheet replacement failure should explain the catalog issue");
            }
            check(failed, scenario_message);
            check_no_state_change();

            failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), expected_error,
                    "invalid source catalog sheetData failure should explain the catalog issue");
            }
            check(failed,
                "PackageEditor should reject by-name sheetData replacement with invalid source catalog");
            check_no_state_change();

            editor.save_as(output_path(output_name));
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output_path(output_name));
            check_preserved_source_entries(editor.reader(), output_reader);
            check(output_reader.read_entry("custom/opaque-extension.bin")
                    == source.opaque_extension,
                "invalid source catalog output should preserve unknown extension bytes");
        };

    expect_source_catalog_failure(
        "fastxlsx-package-editor-worksheet-by-name-missing-rel-source.xlsx",
        "fastxlsx-package-editor-worksheet-by-name-missing-rel-output.xlsx",
        [](LinkedObjectSourcePackage& source) {
            source.workbook =
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="missingRel"/></sheets>)"
                R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
                R"(</workbook>)";
        },
        "relationship id is not present",
        "PackageEditor should reject by-name worksheet replacement with missing source sheet relationship id");

    expect_source_catalog_failure(
        "fastxlsx-package-editor-worksheet-by-name-unregistered-target-source.xlsx",
        "fastxlsx-package-editor-worksheet-by-name-unregistered-target-output.xlsx",
        [](LinkedObjectSourcePackage& source) {
            source.workbook_relationships =
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
                R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
                R"(</Relationships>)";
        },
        "unknown part",
        "PackageEditor should reject by-name worksheet replacement with unregistered worksheet target");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "sheetdata-catalog")) {
            throw TestFailure("wrong shard for sheetdata-catalog executable");
        }

        test_package_editor_renames_sheet_catalog_entry_preserving_parts();
        test_package_editor_sheet_catalog_rename_rewrites_defined_names_opt_in();
        test_package_editor_sheet_catalog_rename_defined_name_rewrite_failure_is_clean();
        test_package_editor_sheet_catalog_rename_uses_planned_workbook_xml();
        test_package_editor_rejects_sheet_catalog_rename_without_state_changes();
        test_package_editor_rejects_invalid_planned_workbook_catalog_by_name_without_state_changes();
        test_package_editor_by_name_helpers_allow_workbook_metadata_rewrite();
        test_package_editor_by_name_helpers_use_planned_catalog_after_workbook_metadata_rewrite();
        test_package_editor_by_name_helpers_reject_after_workbook_removal_without_state_changes();
        test_package_editor_rejects_worksheet_by_sheet_name_without_state_changes();
        test_package_editor_rejects_sheet_name_lookup_with_invalid_office_document_without_state_changes();
        test_package_editor_rejects_invalid_source_workbook_sheet_catalog_without_state_changes();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
