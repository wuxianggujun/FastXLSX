#include "test_package_editor_preservation_core_docparts_common.hpp"

void test_package_editor_workbook_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(workbook_part, "temporary workbook removal");
    check(editor.edit_plan().find_removed_part(workbook_part) != nullptr,
        "setup should record removed workbook before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
            != nullptr,
        "setup should omit workbook owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before workbook restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after workbook removal");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "setup should remove workbook from manifest before replacement restore");

    const std::string restored_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Restored" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Restored!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, restored_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "restored workbook after removal");

    check(editor.edit_plan().find_removed_part(workbook_part) == nullptr,
        "workbook replacement after removal should clear stale removed-part audit");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "workbook replacement after removal should restore active edit-plan part");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook replacement after removal should keep final write mode");
    check(workbook_plan->reason.find("after removal") != std::string::npos,
        "workbook replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "workbook replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "workbook replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels")
            == nullptr,
        "workbook replacement after removal should clear stale owner relationships omission");
    const auto* workbook_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_audit != nullptr,
        "workbook replacement after removal should restore workbook relationships copy audit");
    check(workbook_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should preserve workbook relationships bytes");
    check(workbook_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook replacement after removal should keep source relationship audit role");
    check(workbook_relationships_audit->owner_part == workbook_part.value(),
        "workbook replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "workbook replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") != entries.end(),
        "workbook replacement after removal output should restore workbook part entry");
    check(entries.find("xl/_rels/workbook.xml.rels") != entries.end(),
        "workbook replacement after removal output should restore workbook relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, workbook_part.zip_path());
    check(output_reader.read_entry("xl/workbook.xml") == restored_workbook,
        "workbook replacement after removal should write restored workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "workbook replacement after removal should restore workbook relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "workbook replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "workbook replacement after removal should preserve package relationships bytes");
    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "workbook replacement after removal should keep package workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "workbook replacement after removal should keep package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "workbook replacement after removal should make workbook relationships readable again");
    check(workbook_relationships->find_by_id("rId1") != nullptr,
        "workbook replacement after removal should keep worksheet relationship id");
    check(workbook_relationships->find_by_id("rId3") != nullptr,
        "workbook replacement after removal should keep sharedStrings relationship id");
    check(workbook_relationships->find_by_id("rId5") != nullptr,
        "workbook replacement after removal should keep calcChain relationship id");
}

void test_package_editor_workbook_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Stale" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Stale!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, stale_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "stale workbook replacement before removal");
    check(editor.edit_plan().find_part(workbook_part) != nullptr,
        "setup should record active workbook replacement before removal");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels")
            != nullptr,
        "setup should preserve owner relationships before final workbook removal");

    editor.remove_part(workbook_part, "final workbook removal after replacement");

    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "workbook removal after replacement should clear active replacement");
    const auto* removed_workbook = editor.edit_plan().find_removed_part(workbook_part);
    check(removed_workbook != nullptr,
        "workbook removal after replacement should record removed-part audit");
    check(removed_workbook->reason.find("after replacement") != std::string::npos,
        "workbook removal after replacement should keep final removal reason");
    check(removed_workbook->inbound_relationships.size() == 1,
        "workbook removal after replacement should keep package inbound audit");
    const auto& inbound = removed_workbook->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "workbook removal after replacement should audit package owner part as empty");
    check(inbound.owner_entry == "_rels/.rels",
        "workbook removal after replacement should audit package relationships entry");
    check(inbound.relationship_id == "rId1",
        "workbook removal after replacement should audit package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "workbook removal after replacement should audit package relationship type");
    check(inbound.relationship_target == "xl/workbook.xml",
        "workbook removal after replacement should audit raw workbook target");
    check(inbound.target_part == workbook_part,
        "workbook removal after replacement should audit normalized workbook target");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "workbook removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(workbook_part) == nullptr,
        "workbook removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "workbook removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "workbook removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "workbook removal after replacement content types audit should keep structured role");

    const auto* removed_workbook_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels");
    check(removed_workbook_relationships != nullptr,
        "workbook removal after replacement should omit source-owned workbook relationships");
    check(removed_workbook_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook removal after replacement owner relationships omission should keep source role");
    check(removed_workbook_relationships->owner_part == workbook_part.value(),
        "workbook removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "workbook removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "workbook removal after replacement should not rewrite package relationships");

    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "workbook removal after replacement should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "workbook removal after replacement output plan should omit workbook part");
    check_output_entry_part_context(output_plan, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "workbook removal after replacement output plan should classify omitted workbook");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "workbook removal after replacement output plan should omit workbook relationships");
    check_output_entry_part_context(output_plan, "xl/_rels/workbook.xml.rels", false, "",
        "workbook removal after replacement output plan should classify owner relationships");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "workbook removal after replacement output plan should classify owner relationships omission");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "workbook removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "workbook removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "workbook removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "workbook removal after replacement output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "workbook removal after replacement output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "workbook removal after replacement output should omit workbook owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "workbook removal after replacement output should remove workbook content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/workbook.xml",
        "workbook removal after replacement content types XML should omit workbook override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "workbook removal after replacement should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "workbook removal after replacement should not keep owner relationships for absent workbook");

    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "workbook removal after replacement should keep inbound workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "workbook removal after replacement should not rewrite inbound workbook target");
    check(workbook_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "workbook removal after replacement should keep inbound workbook target mode");

    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "workbook removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "workbook removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "workbook removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "workbook removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "workbook removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "workbook removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "workbook removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "workbook removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "workbook removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "workbook removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "workbook removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "workbook removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "workbook removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "workbook removal after replacement should preserve unknown extension relationships bytes");

    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "workbook removal after replacement should keep worksheet content type override");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "workbook removal after replacement should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "workbook removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "workbook removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "workbook removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "workbook removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "workbook removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "workbook removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "workbook removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "workbook removal after replacement should not promote PNG media default to override");
}

void test_package_editor_worksheet_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(worksheet_part, "temporary worksheet removal");
    check(editor.edit_plan().find_removed_part(worksheet_part) != nullptr,
        "setup should record removed worksheet before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            != nullptr,
        "setup should omit worksheet owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before worksheet restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after worksheet removal");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "setup should remove worksheet from manifest before replacement restore");

    const std::string restored_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, restored_worksheet,
        fastxlsx::detail::ReferencePolicy {}, "restored worksheet after removal");

    check(editor.edit_plan().find_removed_part(worksheet_part) == nullptr,
        "worksheet replacement after removal should clear stale removed-part audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "worksheet replacement after removal should restore active edit-plan part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement after removal should keep final write mode");
    check(worksheet_plan->reason.find("after removal") != std::string::npos,
        "worksheet replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement after removal should restore manifest part write mode");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet replacement after removal should request workbook recalculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet replacement after removal should use worksheet rewrite calcChain cleanup");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet replacement after removal should record stale calcChain removal");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "worksheet replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal should rewrite content types for calcChain cleanup");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "worksheet replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "worksheet replacement after removal should clear stale owner relationships omission");
    const auto* worksheet_relationships_audit =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_audit != nullptr,
        "worksheet replacement after removal should restore worksheet relationships copy audit");
    check(worksheet_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should preserve worksheet relationships bytes");
    check(worksheet_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet replacement after removal should keep source relationship audit role");
    check(worksheet_relationships_audit->owner_part == worksheet_part.value(),
        "worksheet replacement after removal should keep owner part on relationship audit");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet replacement after removal should rewrite workbook relationships for calcChain cleanup");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal workbook relationships should be local-DOM-rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "worksheet replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement after removal should update workbook calc metadata");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") != entries.end(),
        "worksheet replacement after removal output should restore worksheet part entry");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "worksheet replacement after removal output should restore worksheet relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == restored_worksheet,
        "worksheet replacement after removal should write restored worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet replacement after removal should restore worksheet relationships bytes");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "calcChain+xml",
        "worksheet replacement after removal should remove calcChain content type");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "worksheet replacement after removal should omit parsed calcChain content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "worksheet replacement after removal should preserve package relationships bytes");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "worksheet replacement after removal should remove calcChain workbook relationship");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "worksheet replacement after removal should write workbook fullCalcOnLoad");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "worksheet replacement after removal should omit stale calcChain payload");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "worksheet replacement after removal should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "worksheet replacement after removal should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "worksheet replacement after removal should not rewrite inbound worksheet target");
    check(workbook_relationships->find_by_id("rId5") == nullptr,
        "worksheet replacement after removal should remove calcChain relationship id");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "worksheet replacement after removal should make worksheet relationships readable again");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "worksheet replacement after removal should keep drawing relationship id");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "worksheet replacement after removal should keep table relationship id");
    check(worksheet_relationships->find_by_id("rId9") != nullptr,
        "worksheet replacement after removal should keep unknown extension relationship id");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "worksheet replacement after removal should restore worksheet content type override");
}

void test_package_editor_worksheet_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>7</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, stale_worksheet,
        fastxlsx::detail::ReferencePolicy {}, "stale worksheet replacement before removal");
    check(editor.edit_plan().find_part(worksheet_part) != nullptr,
        "setup should record active worksheet replacement before removal");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            != nullptr,
        "setup should preserve owner relationships before final worksheet removal");
    check(editor.edit_plan().full_calculation_on_load(),
        "setup worksheet replacement should request recalculation before final removal");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "setup worksheet replacement should request calcChain cleanup before final removal");

    editor.remove_part(worksheet_part, "final worksheet removal after replacement");

    check(editor.edit_plan().find_part(worksheet_part) == nullptr,
        "worksheet removal after replacement should clear active replacement");
    const auto* removed_worksheet = editor.edit_plan().find_removed_part(worksheet_part);
    check(removed_worksheet != nullptr,
        "worksheet removal after replacement should record removed-part audit");
    check(removed_worksheet->reason.find("after replacement") != std::string::npos,
        "worksheet removal after replacement should keep final removal reason");
    check(removed_worksheet->inbound_relationships.size() == 1,
        "worksheet removal after replacement should keep workbook inbound audit");
    const auto& inbound = removed_worksheet->inbound_relationships.front();
    check(inbound.owner_part == workbook_part.value(),
        "worksheet removal after replacement should audit workbook owner part");
    check(inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "worksheet removal after replacement should audit workbook relationships entry");
    check(inbound.relationship_id == "rId1",
        "worksheet removal after replacement should audit workbook relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "worksheet removal after replacement should audit worksheet relationship type");
    check(inbound.relationship_target == "worksheets/sheet1.xml",
        "worksheet removal after replacement should audit raw worksheet target");
    check(inbound.target_part == worksheet_part,
        "worksheet removal after replacement should audit normalized worksheet target");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "worksheet removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(worksheet_part) == nullptr,
        "worksheet removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "worksheet removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "worksheet removal after replacement content types audit should keep structured role");

    const auto* removed_worksheet_relationships =
        editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(removed_worksheet_relationships != nullptr,
        "worksheet removal after replacement should omit source-owned worksheet relationships");
    check(removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet removal after replacement owner relationships omission should keep source role");
    check(removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "worksheet removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "worksheet removal after replacement should clear active owner relationships audit");
    const auto* workbook_relationships_entry =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_entry != nullptr,
        "worksheet removal after replacement should keep prior calcChain workbook relationships rewrite");
    check(workbook_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement workbook relationships should remain local-DOM-rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "worksheet removal after replacement should not rewrite package relationships");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet removal after replacement should keep prior recalculation request");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Remove,
        "worksheet removal after replacement should keep prior calcChain cleanup policy");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "worksheet removal after replacement should keep stale calcChain removal audit");

    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet removal after replacement should keep workbook calc metadata rewrite");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part) == nullptr,
        "worksheet removal after replacement should keep stale calcChain as removed");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet removal after replacement should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit worksheet part");
    check_output_entry_part_context(output_plan, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "worksheet removal after replacement output plan should classify omitted worksheet");
    check_output_entry_plan(output_plan, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit worksheet relationships");
    check_output_entry_part_context(output_plan, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "worksheet removal after replacement output plan should classify owner relationships");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "worksheet removal after replacement output plan should classify owner relationships omission");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "worksheet removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should rewrite content types");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should keep workbook relationships rewrite");
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "worksheet removal after replacement output plan should keep workbook calc metadata rewrite");
    check_output_entry_plan(output_plan, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "worksheet removal after replacement output plan should omit stale calcChain");
    check_output_entry_plan(output_plan, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "worksheet removal after replacement output plan should preserve drawing");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") == entries.end(),
        "worksheet removal after replacement output should omit worksheet part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "worksheet removal after replacement output should omit worksheet owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(worksheet_part) == nullptr,
        "worksheet removal after replacement output should remove worksheet content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/worksheets/sheet1.xml",
        "worksheet removal after replacement content types XML should omit worksheet override");
    check_not_contains(output_content_types, "calcChain+xml",
        "worksheet removal after replacement should keep calcChain content type removed");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "worksheet removal after replacement should remove parsed calcChain content type");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "worksheet removal after replacement should preserve package relationships bytes");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "worksheet removal after replacement should keep workbook recalculation metadata");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "worksheet removal after replacement should keep calcChain relationship removed");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "worksheet removal after replacement should not keep owner relationships for absent worksheet");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "worksheet removal after replacement should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "worksheet removal after replacement should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "worksheet removal after replacement should not rewrite inbound worksheet target");
    check(worksheet_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "worksheet removal after replacement should keep inbound worksheet target mode");
    check(workbook_relationships->find_by_id("rId5") == nullptr,
        "worksheet removal after replacement should keep calcChain relationship id removed");
    check(output_reader.find_entry(calc_chain_part.zip_path()) == nullptr,
        "worksheet removal after replacement should omit stale calcChain payload");

    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "worksheet removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "worksheet removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "worksheet removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "worksheet removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "worksheet removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "worksheet removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "worksheet removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "worksheet removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "worksheet removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "worksheet removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "worksheet removal after replacement should preserve unknown extension relationships bytes");

    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "worksheet removal after replacement should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "worksheet removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "worksheet removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "worksheet removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "worksheet removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "worksheet removal after replacement should keep VBA content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "worksheet removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "worksheet removal after replacement should not promote PNG media default to override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-core-docparts")) {
            test_package_editor_workbook_replacement_restores_prior_removal();
            test_package_editor_workbook_removal_overrides_prior_replacement();
            test_package_editor_worksheet_replacement_restores_prior_removal();
            test_package_editor_worksheet_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
