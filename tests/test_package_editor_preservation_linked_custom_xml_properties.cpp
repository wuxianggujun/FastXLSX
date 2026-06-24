#include "test_package_editor_preservation_linked_custom_xml_common.hpp"

void test_package_editor_replaces_custom_xml_properties_and_preserves_owner_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-linked-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_properties =
        R"(<ds:datastoreItem ds:itemID="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:replacement"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, replacement_properties,
        "custom XML properties replacement");

    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties replacement should keep target part in edit plan");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement should record final write mode");
    check(custom_xml_props_plan->reason.find("properties") != std::string::npos,
        "custom XML properties replacement should keep readable reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement should update manifest write mode");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML properties replacement should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties replacement should not rewrite package relationships");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "custom XML properties replacement should not record removed-part audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "custom XML properties replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML properties replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom XML properties replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML properties replacement output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties replacement output plan should classify properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("properties") != std::string::npos,
        "custom XML properties replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties replacement output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML properties replacement output plan should classify item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_props_part.zip_path());
    check(output_reader.read_entry("customXml/itemProps1.xml") == replacement_properties,
        "custom XML properties replacement output should write replacement payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML properties replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties replacement should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML properties replacement should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML properties replacement should keep customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML properties replacement should not rewrite customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML properties replacement should keep customXmlProps target mode");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after properties replacement");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties replacement should not invent properties owner relationships");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement should preserve properties content type override");
}

void test_package_editor_repeated_custom_xml_properties_replacement_updates_final_state()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package(
            "fastxlsx-package-editor-repeat-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_properties =
        R"(<ds:datastoreItem ds:itemID="{11111111-2222-3333-4444-aaaaaaaaaaaa}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:stale"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    const std::string final_properties =
        R"(<ds:datastoreItem ds:itemID="{22222222-3333-4444-5555-bbbbbbbbbbbb}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:final"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";

    replace_part_with_memory_chunks(editor, custom_xml_props_part, stale_properties,
        "stale repeated custom XML properties replacement");
    replace_part_with_memory_chunks(editor, custom_xml_props_part, final_properties,
        "final repeated custom XML properties replacement");

    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "repeated custom XML properties replacement should keep an active edit-plan part");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML properties replacement should keep final local-DOM-rewrite mode");
    check(custom_xml_props_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML properties replacement should keep final reason");
    check(custom_xml_props_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML properties replacement should drop stale reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML properties replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML properties replacement should keep properties content type override");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "repeated custom XML properties replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated custom XML properties replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "repeated custom XML properties replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML properties replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated custom XML properties replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated custom XML properties replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated custom XML properties replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated custom XML properties replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated custom XML properties replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated custom XML properties replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated custom XML properties replacement output plan should rewrite properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML properties replacement output plan should keep final reason");
    check(output_props_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML properties replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "repeated custom XML properties replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML properties replacement output plan should preserve item owner relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/itemProps1.xml") == final_properties,
        "repeated custom XML properties replacement should write final properties payload");
    check(output_reader.read_entry("customXml/itemProps1.xml") != stale_properties,
        "repeated custom XML properties replacement should not write stale properties payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "repeated custom XML properties replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "repeated custom XML properties replacement should preserve item owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated custom XML properties replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated custom XML properties replacement should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated custom XML properties replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "repeated custom XML properties replacement should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "repeated custom XML properties replacement should keep item relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "repeated custom XML properties replacement should keep customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "repeated custom XML properties replacement should preserve customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "repeated custom XML properties replacement should not invent properties owner relationships");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML properties replacement should keep properties content type override");
}

void test_package_editor_removes_custom_xml_properties_and_preserves_owner_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal");

    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal should clear active part entry");
    const auto* removed_props =
        editor.edit_plan().find_removed_part(custom_xml_props_part);
    check(removed_props != nullptr,
        "custom XML properties removal should record removed-part audit");
    check(removed_props->reason.find("properties") != std::string::npos,
        "custom XML properties removal should keep readable removal reason");
    check(removed_props->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML properties removal should audit preserved inbound relationship");
    check(removed_props->inbound_relationships.size() == 1,
        "custom XML properties removal should keep one structured inbound audit");
    const auto& inbound = removed_props->inbound_relationships.front();
    check(inbound.owner_part == custom_xml_part.value(),
        "custom XML properties removal should keep custom XML owner part");
    check(inbound.owner_entry == "customXml/_rels/item1.xml.rels",
        "custom XML properties removal should keep owner relationships entry");
    check(inbound.relationship_id == "rIdCustomXmlProps",
        "custom XML properties removal should keep customXmlProps relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "custom XML properties removal should keep customXmlProps relationship type");
    check(inbound.relationship_target == "itemProps1.xml",
        "custom XML properties removal should keep raw owner target");
    check(inbound.target_part == custom_xml_props_part,
        "custom XML properties removal should keep normalized properties target");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal should remove target part from manifest");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "custom XML properties removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "custom XML properties removal content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties removal content types audit should keep structured role");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties removal should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties removal should not omit inbound owner relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties removal should not invent missing properties owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal should keep unknown copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "custom XML properties removal output should omit properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties removal output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal output should remove properties content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "custom XML properties removal content types XML should omit properties override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML properties removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML properties removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML properties removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties removal should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties removal should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML properties removal should keep package customXml relationship");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties removal should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML properties removal should keep inbound customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML properties removal should not rewrite inbound customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML properties removal should keep inbound customXmlProps target mode");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal should not invent properties owner relationships");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep inbound customXmlProps relationship after removal");
}

void test_package_editor_custom_xml_properties_replacement_restores_prior_removal()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-replace-after-remove-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "temporary custom XML properties removal");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "setup should record removed custom XML properties before replacement restore");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "setup should remove custom XML properties from manifest before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before properties restore");
    check(removal_content_types->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after properties removal");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "setup should not omit inbound owner relationships before properties restore");

    const std::string restored_properties =
        R"(<ds:datastoreItem ds:itemID="{bbbbbbbb-cccc-dddd-eeee-ffffffffffff}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:restored"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, restored_properties,
        "restored custom XML properties after removal");

    check(editor.edit_plan().find_removed_part(custom_xml_props_part) == nullptr,
        "custom XML properties replacement after removal should clear stale removed-part audit");
    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties replacement after removal should restore active part");
    check(custom_xml_props_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement after removal should keep final write mode");
    check(custom_xml_props_plan->reason.find("after removal") != std::string::npos,
        "custom XML properties replacement after removal should keep final reason");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML properties replacement after removal should restore manifest write mode");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "custom XML properties replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties replacement after removal content types audit should keep role");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties replacement after removal should not omit inbound owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML properties replacement after removal output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties replacement after removal output plan should classify properties part");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after removal") != std::string::npos,
        "custom XML properties replacement after removal output plan should keep final reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties replacement after removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should restore content types copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties replacement after removal output plan should keep content types role");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML properties replacement after removal output plan should classify item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML properties replacement after removal output should restore properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties replacement after removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties replacement after removal output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_props_part.zip_path());
    check(output_reader.read_entry("customXml/itemProps1.xml") == restored_properties,
        "custom XML properties replacement after removal should write restored payload");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties replacement after removal should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML properties replacement after removal should restore content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties replacement after removal should preserve unknown bytes");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties replacement after removal should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML properties replacement after removal should keep customXmlProps relationship");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML properties replacement after removal should restore content type override");
}

void test_package_editor_custom_xml_properties_removal_overrides_prior_replacement()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-after-replace-custom-xml-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-custom-xml-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_properties =
        R"(<ds:datastoreItem ds:itemID="{cccccccc-dddd-eeee-ffff-111111111111}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:stale"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, stale_properties,
        "prior custom XML properties replacement before removal");
    const auto* prior_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(prior_props_plan != nullptr,
        "setup should record active custom XML properties replacement before removal");
    check(prior_props_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup custom XML properties replacement should be stream-rewrite before removal");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal after replacement");

    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should clear active replacement entry");
    const auto* removed_props =
        editor.edit_plan().find_removed_part(custom_xml_props_part);
    check(removed_props != nullptr,
        "custom XML properties removal after replacement should record removed-part audit");
    check(removed_props->reason.find("after replacement") != std::string::npos,
        "custom XML properties removal after replacement should keep final reason");
    check(removed_props->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML properties removal after replacement should keep inbound audit");
    check(removed_props->inbound_relationships.size() == 1,
        "custom XML properties removal after replacement should keep structured inbound audit");
    check(removed_props->inbound_relationships.front().target_part
            == custom_xml_props_part,
        "custom XML properties removal after replacement should keep normalized target");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "custom XML properties removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "custom XML properties removal after replacement content types audit should be rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML properties removal after replacement should not rewrite package relationships");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML properties removal after replacement should not omit inbound owner relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep custom XML item copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML properties removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML properties removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML properties removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML properties removal after replacement output plan should omit properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML properties removal after replacement output plan should classify omitted properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after replacement") != std::string::npos,
        "custom XML properties removal after replacement output plan should keep final reason");
    check(output_props_plan->inbound_relationships.size() == 1,
        "custom XML properties removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/itemProps1.xml", custom_xml_part.value(),
        "customXml/_rels/item1.xml.rels", "rIdCustomXmlProps",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "itemProps1.xml", custom_xml_props_part,
        "custom XML properties removal after replacement output plan should keep owner inbound audit");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML properties removal after replacement output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom XML properties removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "custom XML properties removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom XML properties removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve item owner relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML properties removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "custom XML properties removal after replacement output should omit properties part");
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML properties removal after replacement output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML properties removal after replacement output should keep owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should remove output content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "custom XML properties removal after replacement content types should omit override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML properties removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML properties removal after replacement should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML properties removal after replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML properties removal after replacement should preserve unknown bytes");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML properties removal after replacement should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML properties removal after replacement should keep inbound customXmlProps relationship");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML properties removal after replacement should not invent properties owner relationships");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-linked-custom-xml-properties")) {
            test_package_editor_replaces_custom_xml_properties_and_preserves_owner_links();
            test_package_editor_repeated_custom_xml_properties_replacement_updates_final_state();
            test_package_editor_removes_custom_xml_properties_and_preserves_owner_links();
            test_package_editor_custom_xml_properties_replacement_restores_prior_removal();
            test_package_editor_custom_xml_properties_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
