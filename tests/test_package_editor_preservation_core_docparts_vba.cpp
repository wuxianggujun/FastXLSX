#include "test_package_editor_preservation_core_docparts_common.hpp"

void test_package_editor_vba_project_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-vba-output.xlsx");

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

    editor.remove_part(vba_part, "temporary VBA project removal");
    check(editor.edit_plan().find_removed_part(vba_part) != nullptr,
        "setup should record removed VBA before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "setup should not invent VBA owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before VBA restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after VBA removal");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "setup should remove VBA from manifest before replacement restore");

    std::string restored_vba = "VBA";
    restored_vba.push_back('\0');
    restored_vba += "PROJECT";
    restored_vba.push_back('\0');
    restored_vba += "restored";
    replace_part_with_memory_chunks(
        editor, vba_part, restored_vba, "restored VBA project after removal");

    check(editor.edit_plan().find_removed_part(vba_part) == nullptr,
        "VBA replacement after removal should clear stale removed-part audit");
    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "VBA replacement after removal should restore active edit-plan part");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VBA replacement after removal should keep final write mode");
    check(vba_plan->reason.find("after removal") != std::string::npos,
        "VBA replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "VBA replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(vba_part) != nullptr,
        "VBA replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "VBA replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "VBA replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "VBA replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "VBA replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VBA replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VBA replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VBA replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "VBA replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "VBA replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "VBA replacement after removal output plan should rewrite VBA project");
    check_output_entry_part_context(output_plan.entries, "xl/vbaProject.bin",
        true, vba_part.value(),
        "VBA replacement after removal output plan should classify rewritten VBA project");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("after removal") != std::string::npos,
        "VBA replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "VBA replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve drawing");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve chart");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve media");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve table");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve calcChain");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") != entries.end(),
        "VBA replacement after removal output should restore VBA project entry");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "VBA replacement after removal output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, vba_part.zip_path());
    check(output_reader.read_entry("xl/vbaProject.bin") == restored_vba,
        "VBA replacement after removal should write restored VBA bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "VBA replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VBA replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VBA replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VBA replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VBA replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VBA replacement after removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VBA replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VBA replacement after removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VBA replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VBA replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VBA replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VBA replacement after removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VBA replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "VBA replacement after removal should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "VBA replacement after removal should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "VBA replacement after removal should not rewrite inbound VBA target");
    check(vba_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VBA replacement after removal should keep inbound VBA target mode");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "VBA replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "VBA replacement after removal should restore VBA content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "VBA replacement after removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "VBA replacement after removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "VBA replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "VBA replacement after removal should not promote PNG media to an override");
}

void test_package_editor_vba_project_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-vba-output.xlsx");

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

    const std::string replacement_vba = std::string("VBA\0PROJECT\0stale", 18);
    replace_part_with_memory_chunks(
        editor, vba_part, replacement_vba, "prior VBA project replacement before removal");
    const auto* prior_vba_plan = editor.edit_plan().find_part(vba_part);
    check(prior_vba_plan != nullptr,
        "setup should record active VBA replacement before removal override");
    check(prior_vba_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup VBA replacement should be stream-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "setup VBA replacement should not invent owner relationships audit");

    editor.remove_part(vba_part, "explicit VBA project removal after replacement");

    check(editor.edit_plan().find_part(vba_part) == nullptr,
        "VBA removal after replacement should clear active replacement entry");
    const auto* removed_vba = editor.edit_plan().find_removed_part(vba_part);
    check(removed_vba != nullptr,
        "VBA removal after replacement should record removed-part audit");
    check(removed_vba->reason.find("after replacement") != std::string::npos,
        "VBA removal after replacement should keep final removal reason");
    check(removed_vba->reason.find("inbound relationship preserved")
            != std::string::npos,
        "VBA removal after replacement should keep inbound relationship audit");
    check(removed_vba->inbound_relationships.size() == 1,
        "VBA removal after replacement should keep structured inbound audit");
    const auto& vba_inbound = removed_vba->inbound_relationships.front();
    check(vba_inbound.owner_part == workbook_part.value(),
        "VBA removal after replacement should keep inbound owner part");
    check(vba_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "VBA removal after replacement should keep inbound owner relationship entry");
    check(vba_inbound.relationship_id == "rId2",
        "VBA removal after replacement should keep inbound relationship id");
    check(vba_inbound.relationship_target == "vbaProject.bin",
        "VBA removal after replacement should keep inbound raw target");
    check(vba_inbound.target_part == vba_part,
        "VBA removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "VBA removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(vba_part) == nullptr,
        "VBA removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "VBA removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "VBA removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "VBA removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "VBA removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "VBA removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "VBA removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "VBA removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "VBA removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == vba_part,
        "VBA removal after replacement output plan should expose removed VBA project");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "VBA removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "VBA removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "VBA removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "VBA removal after replacement output plan should omit target part");
    check_output_entry_part_context(output_plan.entries, "xl/vbaProject.bin",
        true, vba_part.value(),
        "VBA removal after replacement output plan should classify omitted target");
    const auto* output_vba_plan =
        find_output_entry_plan(output_plan.entries, "xl/vbaProject.bin");
    check(output_vba_plan->reason.find("after replacement") != std::string::npos,
        "VBA removal after replacement output plan should keep final removal reason");
    check(output_vba_plan->inbound_relationships.size() == 1,
        "VBA removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/vbaProject.bin", workbook_part.value(), "xl/_rels/workbook.xml.rels",
        "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject",
        "vbaProject.bin", vba_part,
        "VBA removal after replacement output plan should keep workbook inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "VBA removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "VBA removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "VBA removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "VBA removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "VBA removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") == entries.end(),
        "VBA removal after replacement output should omit VBA project part");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "VBA removal after replacement output should not invent VBA owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vba_part) == nullptr,
        "VBA removal after replacement output should remove VBA content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/vbaProject.bin",
        "VBA removal after replacement content types XML should omit VBA override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "VBA removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "VBA removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "VBA removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "VBA removal after replacement should not create owner relationships");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "VBA removal after replacement should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "VBA removal after replacement should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "VBA removal after replacement should not rewrite inbound VBA target");
    check(vba_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "VBA removal after replacement should keep inbound VBA target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "VBA removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "VBA removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "VBA removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "VBA removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "VBA removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "VBA removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "VBA removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "VBA removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "VBA removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "VBA removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "VBA removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "VBA removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "VBA removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "VBA removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "VBA removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "VBA removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "VBA removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "VBA removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "VBA removal after replacement should not promote PNG media to an override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-core-docparts-vba")) {
            test_package_editor_vba_project_replacement_restores_prior_removal();
            test_package_editor_vba_project_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
