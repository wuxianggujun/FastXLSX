#include "test_package_editor_preservation_core_docparts_common.hpp"

void test_package_editor_shared_strings_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-sharedstrings-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(shared_strings_part, "temporary sharedStrings removal");
    check(editor.edit_plan().find_removed_part(shared_strings_part) != nullptr,
        "setup should record removed sharedStrings before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels")
            != nullptr,
        "setup should omit sharedStrings owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before sharedStrings restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after sharedStrings removal");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "setup should remove sharedStrings from manifest before replacement restore");

    const std::string restored_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t>Restored</t></si><si><t>Shared</t></si>)"
        R"(</sst>)";
    replace_part_with_memory_chunks(editor, shared_strings_part,
        restored_shared_strings, "restored sharedStrings after removal");

    check(editor.edit_plan().find_removed_part(shared_strings_part) == nullptr,
        "sharedStrings replacement after removal should clear stale removed-part audit");
    const auto* shared_strings_plan =
        editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "sharedStrings replacement after removal should restore active edit-plan part");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sharedStrings replacement after removal should keep final write mode");
    check(shared_strings_plan->reason.find("after removal") != std::string::npos,
        "sharedStrings replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "sharedStrings replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(shared_strings_part) != nullptr,
        "sharedStrings replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "sharedStrings replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "sharedStrings replacement after removal should clear stale owner relationships omission");
    const auto* shared_strings_relationships_audit =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_audit != nullptr,
        "sharedStrings replacement after removal should restore owner relationships copy audit");
    check(shared_strings_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should preserve owner relationships bytes");
    check(shared_strings_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings replacement after removal should keep source relationship audit role");
    check(shared_strings_relationships_audit->owner_part == shared_strings_part.value(),
        "sharedStrings replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "sharedStrings replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "sharedStrings replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep styles copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "sharedStrings replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "sharedStrings replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "sharedStrings replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "sharedStrings replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "sharedStrings replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sharedStrings replacement after removal output plan should rewrite sharedStrings");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml",
        true, shared_strings_part.value(),
        "sharedStrings replacement after removal output plan should classify rewritten sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("after removal") != std::string::npos,
        "sharedStrings replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "sharedStrings replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings replacement after removal output plan should classify owner relationships metadata");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "sharedStrings replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "sharedStrings replacement after removal output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") != entries.end(),
        "sharedStrings replacement after removal output should restore sharedStrings part entry");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") != entries.end(),
        "sharedStrings replacement after removal output should restore owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, shared_strings_part.zip_path());
    check(output_reader.read_entry("xl/sharedStrings.xml") == restored_shared_strings,
        "sharedStrings replacement after removal should write restored sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "sharedStrings replacement after removal should restore owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "sharedStrings replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "sharedStrings replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sharedStrings replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "sharedStrings replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sharedStrings replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sharedStrings replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sharedStrings replacement after removal should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sharedStrings replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sharedStrings replacement after removal should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "sharedStrings replacement after removal should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "sharedStrings replacement after removal should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sharedStrings replacement after removal should keep inbound sharedStrings target mode");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "sharedStrings replacement after removal should make owner relationships readable again");
    const auto* shared_external =
        shared_strings_relationships->find_by_id("rIdSharedExternal");
    check(shared_external != nullptr,
        "sharedStrings replacement after removal should keep owner relationship id");
    check(shared_external->target == "https://example.invalid/sharedStrings-audit",
        "sharedStrings replacement after removal should keep owner relationship target");
    check(shared_external->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "sharedStrings replacement after removal should keep owner relationship target mode");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "sharedStrings replacement after removal should restore sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sharedStrings replacement after removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "sharedStrings replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sharedStrings replacement after removal should not promote PNG media to an override");
}

void test_package_editor_shared_strings_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-sharedstrings-output.xlsx");

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

    const std::string replacement_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
        R"(<si><t>Stale replacement</t></si>)"
        R"(</sst>)";
    replace_part_with_memory_chunks(editor, shared_strings_part,
        replacement_shared_strings, "prior sharedStrings replacement before removal");

    const auto* prior_shared_strings_plan =
        editor.edit_plan().find_part(shared_strings_part);
    check(prior_shared_strings_plan != nullptr,
        "setup should record active sharedStrings replacement before removal override");
    check(prior_shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup sharedStrings replacement should be stream-rewrite before removal override");
    const auto* prior_shared_strings_relationships =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(prior_shared_strings_relationships != nullptr,
        "setup sharedStrings replacement should preserve owner relationships audit");
    check(prior_shared_strings_relationships->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "setup sharedStrings owner relationships audit should be copy-original");
    check(prior_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "setup sharedStrings owner relationships audit should keep source relationship role");
    check(prior_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "setup sharedStrings owner relationships audit should keep owner part");

    editor.remove_part(shared_strings_part,
        "explicit sharedStrings removal after replacement");

    check(editor.edit_plan().find_part(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should clear active replacement entry");
    const auto* removed_shared_strings =
        editor.edit_plan().find_removed_part(shared_strings_part);
    check(removed_shared_strings != nullptr,
        "sharedStrings removal after replacement should record removed-part audit");
    check(removed_shared_strings->reason.find("after replacement") != std::string::npos,
        "sharedStrings removal after replacement should keep final removal reason");
    check(removed_shared_strings->reason.find("inbound relationship preserved")
            != std::string::npos,
        "sharedStrings removal after replacement should keep inbound relationship audit");
    check(removed_shared_strings->inbound_relationships.size() == 1,
        "sharedStrings removal after replacement should keep structured inbound audit");
    const auto& shared_strings_inbound =
        removed_shared_strings->inbound_relationships.front();
    check(shared_strings_inbound.owner_part == workbook_part.value(),
        "sharedStrings removal after replacement should keep inbound owner part");
    check(shared_strings_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "sharedStrings removal after replacement should keep inbound owner relationship entry");
    check(shared_strings_inbound.relationship_id == "rId3",
        "sharedStrings removal after replacement should keep inbound relationship id");
    check(shared_strings_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "sharedStrings removal after replacement should keep inbound relationship type");
    check(shared_strings_inbound.relationship_target == "sharedStrings.xml",
        "sharedStrings removal after replacement should keep inbound raw target");
    check(shared_strings_inbound.target_part == shared_strings_part,
        "sharedStrings removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "sharedStrings removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sharedStrings removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings removal after replacement content types audit should keep structured role");
    const auto* removed_shared_strings_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(removed_shared_strings_relationships != nullptr,
        "sharedStrings removal after replacement should omit owner relationships");
    check(removed_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement owner relationships omission should keep source relationship role");
    check(removed_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "sharedStrings removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "sharedStrings removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "sharedStrings removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "sharedStrings removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "sharedStrings removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "sharedStrings removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == shared_strings_part,
        "sharedStrings removal after replacement output plan should expose removed sharedStrings");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "sharedStrings removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "sharedStrings removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "sharedStrings removal after replacement output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "xl/_rels/sharedStrings.xml.rels",
        "sharedStrings removal after replacement output plan should expose omitted owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement output plan should keep owner relationships omission role");
    check(output_plan.removed_package_entries.front().owner_part
            == shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should keep owner relationships omission owner");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sharedStrings removal after replacement output plan should omit sharedStrings part");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml",
        true, shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should classify omitted sharedStrings");
    const auto* output_shared_strings_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(output_shared_strings_plan->reason.find("after replacement") != std::string::npos,
        "sharedStrings removal after replacement output plan should keep final removal reason");
    check(output_shared_strings_plan->inbound_relationships.size() == 1,
        "sharedStrings removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/sharedStrings.xml", workbook_part.value(),
        "xl/_rels/workbook.xml.rels", "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "sharedStrings.xml", shared_strings_part,
        "sharedStrings removal after replacement output plan should keep workbook inbound audit");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sharedStrings removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        false, "",
        "sharedStrings removal after replacement output plan should keep owner relationships as metadata");
    const auto* output_shared_strings_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check(output_shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sharedStrings removal after replacement output plan should classify owner relationships metadata");
    check(output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "sharedStrings removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sharedStrings removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "sharedStrings removal after replacement output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sharedStrings removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "sharedStrings removal after replacement output should omit sharedStrings part");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") == entries.end(),
        "sharedStrings removal after replacement output should omit owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/sharedStrings.xml",
        "sharedStrings removal after replacement content types XML should omit sharedStrings override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "sharedStrings removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "sharedStrings removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "sharedStrings removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(shared_strings_part) == nullptr,
        "sharedStrings removal after replacement should not keep owner relationships for absent sharedStrings");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "sharedStrings removal after replacement should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "sharedStrings removal after replacement should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "sharedStrings removal after replacement should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sharedStrings removal after replacement should keep inbound sharedStrings target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sharedStrings removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sharedStrings removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "sharedStrings removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "sharedStrings removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "sharedStrings removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "sharedStrings removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sharedStrings removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sharedStrings removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "sharedStrings removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sharedStrings removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sharedStrings removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "sharedStrings removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sharedStrings removal after replacement should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "sharedStrings removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sharedStrings removal after replacement should not promote PNG media to an override");
}

void test_package_editor_styles_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-styles-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(styles_part, "temporary styles removal");
    check(editor.edit_plan().find_removed_part(styles_part) != nullptr,
        "setup should record removed styles before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "setup should not invent styles owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before styles restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after styles removal");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "setup should remove styles from manifest before replacement restore");

    const std::string restored_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><i/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";
    replace_part_with_memory_chunks(editor, styles_part, restored_styles,
        "restored styles after removal");

    check(editor.edit_plan().find_removed_part(styles_part) == nullptr,
        "styles replacement after removal should clear stale removed-part audit");
    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "styles replacement after removal should restore active edit-plan part");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "styles replacement after removal should keep final staged write mode");
    check(styles_plan->reason.find("after removal") != std::string::npos,
        "styles replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "styles replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(styles_part) != nullptr,
        "styles replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "styles replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "styles replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "styles replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "styles replacement after removal should not rewrite workbook relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "styles replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles replacement after removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "styles replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "styles replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "styles replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "styles replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "styles replacement after removal output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "styles replacement after removal output plan should rewrite styles");
    check_output_entry_part_context(output_plan.entries, "xl/styles.xml",
        true, styles_part.value(),
        "styles replacement after removal output plan should classify rewritten styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("after removal") != std::string::npos,
        "styles replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels") == nullptr,
        "styles replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "styles replacement after removal output plan should keep content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles replacement after removal output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") != entries.end(),
        "styles replacement after removal output should restore styles part entry");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "styles replacement after removal output should not invent owner relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, styles_part.zip_path());
    check(output_reader.read_entry("xl/styles.xml") == restored_styles,
        "styles replacement after removal should write restored styles bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "styles replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "styles replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "styles replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "styles replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "styles replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "styles replacement after removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "styles replacement after removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "styles replacement after removal should preserve unknown extension bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "styles replacement after removal should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "styles replacement after removal should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "styles replacement after removal should not rewrite inbound styles target");
    check(styles_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "styles replacement after removal should keep inbound styles target mode");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "styles replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "styles replacement after removal should restore styles content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "styles replacement after removal should keep sharedStrings content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "styles replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "styles replacement after removal should not promote PNG media to an override");
}

void test_package_editor_styles_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-styles-output.xlsx");

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

    const std::string replacement_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><b/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";
    replace_part_with_memory_chunks(editor, styles_part, replacement_styles,
        "prior styles replacement before removal");

    const auto* prior_styles_plan = editor.edit_plan().find_part(styles_part);
    check(prior_styles_plan != nullptr,
        "setup should record active styles replacement before removal override");
    check(prior_styles_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup styles replacement should be staged before removal override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup styles replacement should not rewrite content types before removal override");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "setup styles replacement should not invent owner relationships audit");

    editor.remove_part(styles_part, "explicit styles removal after replacement");

    check(editor.edit_plan().find_part(styles_part) == nullptr,
        "styles removal after replacement should clear active replacement entry");
    const auto* removed_styles = editor.edit_plan().find_removed_part(styles_part);
    check(removed_styles != nullptr,
        "styles removal after replacement should record removed-part audit");
    check(removed_styles->reason.find("after replacement") != std::string::npos,
        "styles removal after replacement should keep final removal reason");
    check(removed_styles->reason.find("inbound relationship preserved")
            != std::string::npos,
        "styles removal after replacement should keep inbound relationship audit");
    check(removed_styles->inbound_relationships.size() == 1,
        "styles removal after replacement should keep structured inbound audit");
    const auto& styles_inbound = removed_styles->inbound_relationships.front();
    check(styles_inbound.owner_part == workbook_part.value(),
        "styles removal after replacement should keep inbound owner part");
    check(styles_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "styles removal after replacement should keep inbound owner relationship entry");
    check(styles_inbound.relationship_id == "rId4",
        "styles removal after replacement should keep inbound relationship id");
    check(styles_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles removal after replacement should keep inbound relationship type");
    check(styles_inbound.relationship_target == "styles.xml",
        "styles removal after replacement should keep inbound raw target");
    check(styles_inbound.target_part == styles_part,
        "styles removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "styles removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(styles_part) == nullptr,
        "styles removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "styles removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "styles removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "styles removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "styles removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "styles removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "styles removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "styles removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "styles removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == styles_part,
        "styles removal after replacement output plan should expose removed styles");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "styles removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "styles removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "styles removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "styles removal after replacement output plan should omit styles part");
    check_output_entry_part_context(output_plan.entries, "xl/styles.xml",
        true, styles_part.value(),
        "styles removal after replacement output plan should classify omitted styles");
    const auto* output_styles_plan =
        find_output_entry_plan(output_plan.entries, "xl/styles.xml");
    check(output_styles_plan->reason.find("after replacement") != std::string::npos,
        "styles removal after replacement output plan should keep final removal reason");
    check(output_styles_plan->inbound_relationships.size() == 1,
        "styles removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/styles.xml", workbook_part.value(), "xl/_rels/workbook.xml.rels",
        "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "styles.xml", styles_part,
        "styles removal after replacement output plan should keep workbook inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/_rels/styles.xml.rels")
            == nullptr,
        "styles removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "styles removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "styles removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "styles removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles removal after replacement output plan should preserve inbound workbook relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") == entries.end(),
        "styles removal after replacement output should omit styles part");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "styles removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(styles_part) == nullptr,
        "styles removal after replacement output should remove styles content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/styles.xml",
        "styles removal after replacement content types XML should omit styles override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "styles removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "styles removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "styles removal after replacement should preserve inbound workbook relationships");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "styles removal after replacement should not create owner relationships for absent styles");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "styles removal after replacement should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "styles removal after replacement should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "styles removal after replacement should not rewrite inbound styles target");
    check(styles_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "styles removal after replacement should keep inbound styles target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "styles removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "styles removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "styles removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "styles removal after replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "styles removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "styles removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "styles removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "styles removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "styles removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "styles removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "styles removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "styles removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "styles removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "styles removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "styles removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "styles removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "styles removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "styles removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "styles removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "styles removal after replacement should not promote PNG media to an override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-core-docparts-shared")) {
            test_package_editor_shared_strings_replacement_restores_prior_removal();
            test_package_editor_shared_strings_removal_overrides_prior_replacement();
            test_package_editor_styles_replacement_restores_prior_removal();
            test_package_editor_styles_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
