#include "test_package_editor_preservation_linked_custom_xml_common.hpp"

void test_package_editor_custom_xml_item_removal_then_properties_replacement_keeps_owner_omitted()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-then-replace-props-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-then-replace-props-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "explicit custom XML item removal before properties rewrite");
    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "setup should record removed custom XML item before properties rewrite");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup should omit custom XML item owner relationships before properties rewrite");

    const std::string replacement_properties =
        R"(<ds:datastoreItem ds:itemID="{dddddddd-eeee-ffff-1111-222222222222}" xmlns:ds="http://schemas.openxmlformats.org/officeDocument/2006/customXml">)"
        R"(<ds:schemaRefs><ds:schemaRef ds:uri="urn:fastxlsx:properties-after-item-removal"/></ds:schemaRefs>)"
        R"(</ds:datastoreItem>)";
    replace_part_with_memory_chunks(editor, custom_xml_props_part, replacement_properties,
        "custom XML properties replacement after item removal");

    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "properties replacement after item removal should keep removed custom XML item audit");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "properties replacement after item removal should keep omitted owner relationships");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "properties replacement after item removal should not revive owner relationships audit");
    const auto* properties_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(properties_plan != nullptr,
        "properties replacement after item removal should keep active properties part");
    check(properties_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "properties replacement after item removal should keep final properties stream-rewrite mode");
    check(properties_plan->reason.find("after item removal") != std::string::npos,
        "properties replacement after item removal should keep final reason");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "properties replacement after item removal should keep custom XML item removed");
    check_manifest_write_mode(editor, custom_xml_props_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "properties replacement after item removal should mark properties part rewritten");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) != nullptr,
        "properties replacement after item removal should keep properties content type override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "properties replacement after item removal should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "properties replacement after item removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "properties replacement after item removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "properties replacement after item removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "properties replacement after item removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "properties replacement after item removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "properties replacement after item removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_part,
        "properties replacement after item removal output plan should expose removed custom XML item");
    check(output_plan.removed_parts.front().reason.find("before properties rewrite")
            != std::string::npos,
        "properties replacement after item removal output plan should keep removed item reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "properties replacement after item removal output plan should expose removed item inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "properties replacement after item removal output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "customXml/_rels/item1.xml.rels",
        "properties replacement after item removal output plan should omit item owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "properties replacement after item removal output plan should classify omitted owner relationships");
    check(output_plan.removed_package_entries.front().owner_part == custom_xml_part.value(),
        "properties replacement after item removal output plan should keep omitted owner context");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "properties replacement after item removal output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "properties replacement after item removal output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("before properties rewrite") != std::string::npos,
        "properties replacement after item removal output plan should keep item removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "properties replacement after item removal output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "properties replacement after item removal output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "properties replacement after item removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "properties replacement after item removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "properties replacement after item removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "properties replacement after item removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "properties replacement after item removal output plan should rewrite properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "properties replacement after item removal output plan should classify rewritten properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("after item removal") != std::string::npos,
        "properties replacement after item removal output plan should keep properties replacement reason");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "properties replacement after item removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "properties replacement after item removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "properties replacement after item removal output should omit custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "properties replacement after item removal output should keep owner relationships omitted");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "properties replacement after item removal output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/itemProps1.xml") == replacement_properties,
        "properties replacement after item removal should write replacement properties payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "properties replacement after item removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "properties replacement after item removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "properties replacement after item removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "properties replacement after item removal should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "properties replacement after item removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "properties replacement after item removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "properties replacement after item removal should not rewrite package customXml target");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "properties replacement after item removal should not revive owner relationships");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "properties replacement after item removal should not invent properties owner relationships");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "properties replacement after item removal should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "properties replacement after item removal should keep properties content type override");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after properties rewrite");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships after properties rewrite");
}

void test_package_editor_custom_xml_properties_removal_then_item_replacement_keeps_properties_omitted()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-props-then-replace-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-props-then-replace-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_props_part,
        "explicit custom XML properties removal before item rewrite");
    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "setup should record removed custom XML properties before item rewrite");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "setup should remove custom XML properties from manifest before item rewrite");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "setup should keep custom XML item owner relationships before item rewrite");

    const std::string replacement_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Custom XML replacement after properties removal</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, replacement_custom_xml,
        "custom XML item replacement after properties removal");

    check(editor.edit_plan().find_removed_part(custom_xml_props_part) != nullptr,
        "item replacement after properties removal should keep removed properties audit");
    check(editor.edit_plan().find_part(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not restore active properties part");
    check(editor.manifest().find_part(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should keep properties part removed");
    check(editor.manifest().content_types().override_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should keep properties content type removed");
    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "item replacement after properties removal should keep active custom XML item");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "item replacement after properties removal should keep final item write mode");
    check(custom_xml_plan->reason.find("after properties removal") != std::string::npos,
        "item replacement after properties removal should keep final reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "item replacement after properties removal should mark item rewritten");
    const auto* owner_relationships_entry =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(owner_relationships_entry != nullptr,
        "item replacement after properties removal should keep owner relationships audit");
    check(owner_relationships_entry->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal owner relationships should be copy-original");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "item replacement after properties removal should not omit owner relationships");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "item replacement after properties removal should keep content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "item replacement after properties removal content types audit should be rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "item replacement after properties removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "item replacement after properties removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "item replacement after properties removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "item replacement after properties removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "item replacement after properties removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "item replacement after properties removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_props_part,
        "item replacement after properties removal output plan should expose removed properties part");
    check(output_plan.removed_parts.front().reason.find("before item rewrite")
            != std::string::npos,
        "item replacement after properties removal output plan should keep removed properties reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "item replacement after properties removal output plan should expose removed properties inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "item replacement after properties removal output plan should not omit owner relationships");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "item replacement after properties removal output plan should omit properties part");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "item replacement after properties removal output plan should classify omitted properties");
    const auto* output_props_plan =
        find_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml");
    check(output_props_plan->reason.find("before item rewrite") != std::string::npos,
        "item replacement after properties removal output plan should keep properties removal reason");
    check(output_props_plan->inbound_relationships.size() == 1,
        "item replacement after properties removal output plan should expose owner inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/itemProps1.xml", custom_xml_part.value(),
        "customXml/_rels/item1.xml.rels", "rIdCustomXmlProps",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXmlProps",
        "itemProps1.xml", custom_xml_props_part,
        "item replacement after properties removal output plan should keep owner inbound audit");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "item replacement after properties removal output plan should not invent properties owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "item replacement after properties removal output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "item replacement after properties removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "item replacement after properties removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "item replacement after properties removal output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "item replacement after properties removal output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after properties removal") != std::string::npos,
        "item replacement after properties removal output plan should keep item replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve item owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "item replacement after properties removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "item replacement after properties removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "item replacement after properties removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "item replacement after properties removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "item replacement after properties removal output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "item replacement after properties removal output should keep owner relationships");
    check(entries.find("customXml/itemProps1.xml") == entries.end(),
        "item replacement after properties removal output should omit properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/item1.xml") == replacement_custom_xml,
        "item replacement after properties removal should write replacement item payload");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "item replacement after properties removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "item replacement after properties removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "item replacement after properties removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "item replacement after properties removal should preserve owner relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "item replacement after properties removal should preserve unknown bytes");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/customXml/itemProps1.xml",
        "item replacement after properties removal content types should omit properties override");
    check(output_reader.content_types().override_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not restore properties content type");
    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "item replacement after properties removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "item replacement after properties removal should not rewrite package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "item replacement after properties removal should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "item replacement after properties removal should keep inbound customXmlProps relationship");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "item replacement after properties removal should not rewrite customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "item replacement after properties removal should not invent properties owner relationships");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after item rewrite");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after item rewrite");
}



} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-linked-custom-xml-ordering")) {
            test_package_editor_custom_xml_item_removal_then_properties_replacement_keeps_owner_omitted();
            test_package_editor_custom_xml_properties_removal_then_item_replacement_keeps_properties_omitted();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
