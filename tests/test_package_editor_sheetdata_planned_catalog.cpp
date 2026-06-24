#include "test_package_editor_sheetdata_common.hpp"

void test_package_editor_replaces_worksheet_by_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
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
        output_path("fastxlsx-package-editor-worksheet-by-name-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_worksheet =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- FastXLSX patch keeps caller-owned worksheet prolog -->\n"
        "<?fastxlsx-patch preserve-prolog?>\n"
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="5"><c r="B5"><v>55</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", replacement_worksheet);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheet-name worksheet replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sheet-name worksheet replacement should plan worksheet stream rewrite");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheet-name worksheet replacement should update workbook calc metadata");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet-name worksheet replacement should plan workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheet-name worksheet replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheet-name worksheet replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "sheet-name worksheet replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sheet-name worksheet output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name worksheet output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name worksheet output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheet-name worksheet replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "sheet-name worksheet replacement should keep dot-segment sheet lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "sheet-name worksheet replacement should preserve dot-segment package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sheet-name worksheet replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "./worksheets/../worksheets/sheet1.xml",
        "sheet-name worksheet replacement should preserve dot-segment worksheet target");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "sheet-name worksheet replacement should write the target worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sheet-name worksheet replacement should preserve worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "sheet-name worksheet replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "sheet-name worksheet replacement should keep drawing relationship readable");
    check(worksheet_relationships->find_by_id("rId9") != nullptr,
        "sheet-name worksheet replacement should keep unknown extension relationship readable");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet-name worksheet replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet-name worksheet replacement should keep unknown extension default content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "sheet-name worksheet replacement should remove calcChain content type override");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "sheet-name worksheet replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "sheet-name worksheet replacement should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "sheet-name worksheet replacement should request workbook recalculation");
}

void test_package_editor_replaces_worksheet_by_planned_workbook_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-by-name-planned-catalog-source.xlsx");
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
        output_path("fastxlsx-package-editor-worksheet-by-name-planned-catalog-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Renamed!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name worksheet patch");

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

    bool failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor,
            "Sheet1", "<worksheet><sheetData/></worksheet>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned workbook catalog old-name failure should use planned sheet names");
    }
    check(failed,
        "PackageEditor should reject old source sheet name after planned workbook replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "planned catalog old-name worksheet failure should preserve queued edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned catalog old-name worksheet failure should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "planned catalog old-name worksheet failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "planned catalog old-name worksheet failure should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "planned catalog old-name worksheet failure should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned catalog old-name worksheet failure should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "planned catalog old-name worksheet failure should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "planned catalog old-name worksheet failure should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned catalog old-name worksheet failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "planned catalog old-name worksheet failure should not change calcChain policy");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name worksheet failure should keep prior workbook replacement");
    check(workbook_plan->reason.find("ordinary workbook replacement before by-name worksheet patch")
            != std::string::npos,
        "planned catalog old-name worksheet failure should keep prior workbook replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name worksheet failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name worksheet failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name worksheet failure should leave calcChain copy-original");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="8"><c r="A8"><v>88</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_by_name_from_single_chunk_source(editor, "Renamed", replacement_worksheet);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned catalog worksheet replacement should resolve the renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned catalog worksheet replacement should plan worksheet stream rewrite");
    workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog worksheet replacement should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog worksheet replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned catalog worksheet replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog worksheet replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog worksheet output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned catalog worksheet output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog worksheet output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned catalog worksheet output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog worksheet replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed") == worksheet_part,
        "planned catalog worksheet output should expose the renamed sheet catalog");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "planned catalog worksheet output should not expose the old source sheet name");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "planned catalog worksheet replacement should write target worksheet bytes");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed")",
        "planned catalog worksheet replacement should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog worksheet replacement should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog worksheet replacement should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog worksheet replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog worksheet replacement should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog worksheet replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog worksheet replacement should keep unknown extension default content type");
}

void test_package_editor_replaces_sheet_data_by_planned_workbook_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-planned-catalog-source.xlsx");
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
        output_path("fastxlsx-package-editor-sheetdata-by-name-planned-catalog-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Renamed!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name sheetData patch");

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

    bool failed = false;
    try {
        replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", "<sheetData/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned workbook catalog old-name sheetData failure should use planned sheet names");
    }
    check(failed,
        "PackageEditor should reject old source sheet name for planned sheetData patch");

    check(editor.edit_plan().size() == queued_plan_size,
        "planned catalog old-name sheetData failure should preserve queued edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned catalog old-name sheetData failure should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "planned catalog old-name sheetData failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "planned catalog old-name sheetData failure should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "planned catalog old-name sheetData failure should not append worksheet payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned catalog old-name sheetData failure should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "planned catalog old-name sheetData failure should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "planned catalog old-name sheetData failure should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned catalog old-name sheetData failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "planned catalog old-name sheetData failure should not change calcChain policy");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name sheetData failure should keep prior workbook replacement");
    check(workbook_plan->reason.find("ordinary workbook replacement before by-name sheetData patch")
            != std::string::npos,
        "planned catalog old-name sheetData failure should keep prior workbook replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog old-name sheetData failure should keep workbook local-DOM-rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name sheetData failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned catalog old-name sheetData failure should leave calcChain copy-original");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="9"><c r="A9"><v>99</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Renamed", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned catalog sheetData replacement should resolve the renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog sheetData replacement should plan worksheet local-DOM rewrite");
    workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned catalog sheetData replacement should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "planned catalog sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned catalog sheetData replacement should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "planned catalog sheetData replacement should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog sheetData output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned catalog sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "planned catalog sheetData output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "planned catalog sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "planned catalog sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Renamed") == worksheet_part,
        "planned catalog sheetData output should expose the renamed sheet catalog");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "planned catalog sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "planned catalog sheetData replacement should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed")",
        "planned catalog sheetData replacement should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "planned catalog sheetData replacement should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "planned catalog sheetData replacement should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "planned catalog sheetData replacement should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "planned catalog sheetData replacement should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "planned catalog sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "planned catalog sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_planned_workbook_catalog_respects_relationship_namespace()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-planned-catalog-namespace-source.xlsx");
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
        output_path("fastxlsx-package-editor-planned-catalog-namespace-output.xlsx");

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::string replacement_workbook =
        R"(<workbook xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="AltPlanned" sheetId="1" rel:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement with alternate relationship namespace prefix");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="10"><c r="A10"><v>100</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "AltPlanned", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "alternate-prefix planned catalog should resolve the worksheet part");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "alternate-prefix planned catalog should keep workbook local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "alternate-prefix planned catalog should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "alternate-prefix planned catalog should request full calculation");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "alternate-prefix planned catalog should remove calcChain from output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "alternate-prefix planned catalog output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "alternate-prefix planned catalog output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "alternate-prefix planned catalog output plan should omit stale calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "alternate-prefix planned catalog output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "alternate-prefix planned catalog output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("AltPlanned") == worksheet_part,
        "alternate-prefix planned catalog output should expose the planned sheet name");
    bool old_name_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Sheet1");
    } catch (const std::exception&) {
        old_name_failed = true;
    }
    check(old_name_failed,
        "alternate-prefix planned catalog output should not expose the old source sheet name");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "alternate-prefix planned catalog should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "alternate-prefix planned catalog should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="AltPlanned")",
        "alternate-prefix planned catalog should preserve planned workbook sheet name");
    check_contains(workbook_xml, R"(rel:id="rId1")",
        "alternate-prefix planned catalog should preserve the namespace-qualified sheet id");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "alternate-prefix planned catalog should update workbook calc metadata");
    check_not_contains(output_reader.read_entry("[Content_Types].xml"), "calcChain+xml",
        "alternate-prefix planned catalog should remove calcChain content type");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "alternate-prefix planned catalog should preserve dot-segment worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "alternate-prefix planned catalog should remove calcChain relationship");
    check(output_reader.read_entry("custom/opaque-extension.bin") == source.opaque_extension,
        "alternate-prefix planned catalog should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "alternate-prefix planned catalog should keep unknown extension default content type");

    const auto expect_invalid_planned_catalog_id =
        [&](const std::string& planned_workbook,
            std::string_view sheet_name,
            std::string_view output_name,
            const char* replacement_reason) {
            fastxlsx::detail::PackageEditor invalid_editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            invalid_editor.replace_part(workbook_part, planned_workbook,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                replacement_reason);

            const std::size_t queued_plan_size = invalid_editor.edit_plan().size();
            const std::size_t queued_note_count =
                invalid_editor.edit_plan().notes().size();
            const std::size_t queued_relationship_target_audit_count =
                invalid_editor.edit_plan().relationship_target_audits().size();
            const std::size_t queued_worksheet_relationship_reference_audit_count =
                invalid_editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t queued_worksheet_payload_dependency_audit_count =
                invalid_editor.edit_plan().worksheet_payload_dependency_audits().size();
            const std::size_t queued_package_entry_count =
                invalid_editor.edit_plan().package_entries().size();
            const std::size_t queued_removed_package_entry_count =
                invalid_editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                replace_worksheet_sheet_data_by_name_from_single_chunk_source(invalid_editor,
                    sheet_name, "<sheetData/>");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), "missing relationship id",
                    "invalid planned catalog namespace failure should explain missing relationship id");
            }
            check(failed,
                "PackageEditor should reject planned catalog ids outside the relationship namespace");

            check(invalid_editor.edit_plan().size() == queued_plan_size,
                "invalid planned catalog namespace failure should preserve queued edit-plan size");
            check(invalid_editor.edit_plan().notes().size() == queued_note_count,
                "invalid planned catalog namespace failure should not append notes");
            check(invalid_editor.edit_plan().relationship_target_audits().size()
                    == queued_relationship_target_audit_count,
                "invalid planned catalog namespace failure should not append relationship target audits");
            check(invalid_editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == queued_worksheet_relationship_reference_audit_count,
                "invalid planned catalog namespace failure should not append worksheet relationship audits");
            check(invalid_editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == queued_worksheet_payload_dependency_audit_count,
                "invalid planned catalog namespace failure should not append worksheet payload audits");
            check(invalid_editor.edit_plan().package_entries().size()
                    == queued_package_entry_count,
                "invalid planned catalog namespace failure should preserve package-entry audit count");
            check(invalid_editor.edit_plan().removed_package_entries().size()
                    == queued_removed_package_entry_count,
                "invalid planned catalog namespace failure should preserve removed package-entry audit count");
            check(invalid_editor.edit_plan().removed_parts().empty(),
                "invalid planned catalog namespace failure should not record removed parts");
            check(!invalid_editor.edit_plan().full_calculation_on_load(),
                "invalid planned catalog namespace failure should not request recalculation");
            check(invalid_editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid planned catalog namespace failure should not change calcChain policy");
            const auto* invalid_workbook_plan =
                invalid_editor.edit_plan().find_part(workbook_part);
            check(invalid_workbook_plan != nullptr
                    && invalid_workbook_plan->write_mode
                        == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog namespace failure should keep prior workbook replacement");
            check(invalid_workbook_plan->reason.find(replacement_reason)
                    != std::string::npos,
                "invalid planned catalog namespace failure should keep prior workbook replacement reason");
            check_manifest_write_mode(invalid_editor, workbook_part,
                fastxlsx::detail::PartWriteMode::LocalDomRewrite,
                "invalid planned catalog namespace failure should keep workbook local-DOM-rewrite");
            check_manifest_write_mode(invalid_editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog namespace failure should leave worksheet copy-original");
            check_manifest_write_mode(invalid_editor, calc_chain_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid planned catalog namespace failure should leave calcChain copy-original");

            const std::filesystem::path invalid_output = output_path(output_name);
            invalid_editor.save_as(invalid_output);

            const fastxlsx::detail::PackageReader invalid_output_reader =
                fastxlsx::detail::PackageReader::open(invalid_output);
            check(invalid_output_reader.read_entry("xl/workbook.xml") == planned_workbook,
                "invalid planned catalog namespace failure output should keep queued workbook replacement");
            check(invalid_output_reader.read_entry("xl/worksheets/sheet1.xml")
                    == source.worksheet,
                "invalid planned catalog namespace failure output should preserve source worksheet bytes");
            check(invalid_output_reader.read_entry("xl/calcChain.xml")
                    == source.calc_chain,
                "invalid planned catalog namespace failure output should preserve calcChain bytes");
            check(invalid_output_reader.read_entry("custom/opaque-extension.bin")
                    == source.opaque_extension,
                "invalid planned catalog namespace failure output should preserve unknown extension bytes");
        };

    const std::string wrong_namespace_workbook =
        R"(<workbook xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheets><sheet name="WrongNs" sheetId="1" x:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_invalid_planned_catalog_id(wrong_namespace_workbook, "WrongNs",
        "fastxlsx-package-editor-planned-catalog-wrong-namespace-output.xlsx",
        "wrong-namespace planned workbook catalog before by-name patch");

    const std::string unqualified_id_workbook =
        R"(<workbook><sheets><sheet name="PlainId" sheetId="1" id="rId1"/></sheets></workbook>)";
    expect_invalid_planned_catalog_id(unqualified_id_workbook, "PlainId",
        "fastxlsx-package-editor-planned-catalog-plain-id-output.xlsx",
        "plain-id planned workbook catalog before by-name patch");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-planned-catalog")) {
            test_package_editor_replaces_worksheet_by_sheet_name();
            test_package_editor_replaces_worksheet_by_planned_workbook_sheet_name();
            test_package_editor_replaces_sheet_data_by_planned_workbook_sheet_name();
            test_package_editor_planned_workbook_catalog_respects_relationship_namespace();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
