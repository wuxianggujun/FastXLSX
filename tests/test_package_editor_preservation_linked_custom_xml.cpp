#include "test_package_editor_preservation_linked_custom_xml_common.hpp"

void test_package_editor_worksheet_rewrite_preserves_custom_xml_parts()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>2048</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML item should remain visible in worksheet rewrite edit plan");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML item should remain copy-original during worksheet rewrite");
    const auto* custom_xml_props_plan =
        editor.edit_plan().find_part(custom_xml_props_part);
    check(custom_xml_props_plan != nullptr,
        "custom XML properties should remain visible in worksheet rewrite edit plan");
    check(custom_xml_props_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML properties should remain copy-original during worksheet rewrite");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML worksheet rewrite should keep unrelated unknown part copy-original");

    const auto* custom_xml_relationships_plan =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_plan != nullptr,
        "custom XML worksheet rewrite should audit preserved custom XML relationships");
    check(custom_xml_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML relationships should be copy-original in package-entry audit");
    check(custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML relationships audit should keep structured role");
    check(custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML worksheet rewrite should not rewrite content types without calcChain");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML worksheet rewrite should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "custom XML worksheet rewrite output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "custom XML worksheet rewrite output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML worksheet rewrite output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML worksheet rewrite output plan should stream-rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "custom XML worksheet rewrite output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom XML worksheet rewrite output plan should update workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "custom XML worksheet rewrite output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "custom XML worksheet rewrite output plan should classify package relationships");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML worksheet rewrite output plan should classify custom XML item");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML worksheet rewrite output plan should classify custom XML relationships");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML worksheet rewrite output plan should keep owner relationship role");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML worksheet rewrite output plan should keep custom XML owner context");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve custom XML properties");
    check_output_entry_part_context(output_plan.entries, "customXml/itemProps1.xml",
        true, custom_xml_props_part.value(),
        "custom XML worksheet rewrite output plan should classify custom XML properties");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML worksheet rewrite output plan should not invent properties relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML worksheet rewrite output plan should classify content types");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML worksheet rewrite output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "custom XML worksheet rewrite output plan should classify unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML worksheet rewrite output should keep custom XML properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "custom XML worksheet rewrite should write replacement worksheet XML");
    check(output_reader.read_entry("customXml/item1.xml") == source.custom_xml,
        "custom XML worksheet rewrite should preserve custom XML item bytes");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML worksheet rewrite should preserve custom XML relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML worksheet rewrite should preserve custom XML properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML worksheet rewrite should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML worksheet rewrite should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML worksheet rewrite should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML worksheet rewrite should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML worksheet rewrite should not rewrite customXml package target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML worksheet rewrite should keep customXml relationship internal");

    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML worksheet rewrite should keep custom XML relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML worksheet rewrite should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML worksheet rewrite should not rewrite customXmlProps target");
    check(output_reader.relationships_for(custom_xml_props_part) == nullptr,
        "custom XML worksheet rewrite should not invent custom XML properties relationships");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML worksheet rewrite should preserve default XML content type for item");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML worksheet rewrite should preserve custom XML properties content type override");
}

void test_package_editor_replaces_custom_xml_and_preserves_package_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-linked-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Replacement custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, replacement_custom_xml,
        "custom XML item replacement");

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML replacement should keep target part in edit plan");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement should record final write mode");
    check(custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML replacement should keep readable replacement reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement should update manifest write mode");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_audit != nullptr,
        "custom XML replacement should audit preserved owner relationships");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML owner relationships audit should be copy-original");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML owner relationships audit should keep source relationship role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "custom XML owner relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML replacement should not rewrite content types for default XML part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML replacement should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement should keep unrelated unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom XML replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML replacement output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML replacement output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML replacement output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML replacement output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML replacement output plan should not invent properties relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_part.zip_path());
    check(output_reader.read_entry("customXml/item1.xml") == replacement_custom_xml,
        "custom XML replacement output should write replacement payload");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML replacement should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML replacement should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML replacement should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML replacement should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML replacement should keep package customXml target mode");

    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "custom XML replacement should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "custom XML replacement should not rewrite customXmlProps target");
    check(custom_xml_props_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML replacement should keep customXmlProps target mode");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after replacement");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after replacement");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML replacement should preserve default XML content type for item");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML replacement should preserve custom XML properties content type override");
}

void test_package_editor_repeated_custom_xml_replacement_updates_final_state()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package(
            "fastxlsx-package-editor-repeat-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string custom_xml_relationships_entry = "customXml/_rels/item1.xml.rels";

    const std::string stale_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Stale custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    const std::string final_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Final custom XML payload</fx:value>)"
        R"(</fx:payload>)";

    replace_part_with_memory_chunks(editor, custom_xml_part, stale_custom_xml,
        "stale repeated custom XML item replacement");
    replace_part_with_memory_chunks(editor, custom_xml_part, final_custom_xml,
        "final repeated custom XML item replacement");

    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "repeated custom XML replacement should keep an active edit-plan part");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML replacement should keep final local-DOM-rewrite mode");
    check(custom_xml_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML replacement should keep final reason");
    check(custom_xml_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML replacement should drop stale reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated custom XML replacement should mirror final write mode into manifest");
    const auto* custom_xml_content_type =
        editor.manifest().content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "repeated custom XML replacement should keep default XML content type");
    check(editor.edit_plan().find_removed_part(custom_xml_part) == nullptr,
        "repeated custom XML replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(custom_xml_relationships_entry)
            == nullptr,
        "repeated custom XML replacement should not leave owner relationships omission");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry(custom_xml_relationships_entry);
    check(custom_xml_relationships_audit != nullptr,
        "repeated custom XML replacement should preserve owner relationships audit");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should preserve owner relationships bytes");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated custom XML replacement should keep owner relationship audit role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "repeated custom XML replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated custom XML replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "repeated custom XML replacement should not rewrite package relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated custom XML replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated custom XML replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated custom XML replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated custom XML replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated custom XML replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated custom XML replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated custom XML replacement output plan should rewrite custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("final repeated") != std::string::npos,
        "repeated custom XML replacement output plan should keep final reason");
    check(output_custom_xml_plan->reason.find("stale repeated") == std::string::npos,
        "repeated custom XML replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, custom_xml_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve owner relationships");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, custom_xml_relationships_entry);
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated custom XML replacement output plan should keep owner relationship audit role");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "repeated custom XML replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated custom XML replacement output plan should preserve package relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("customXml/item1.xml") == final_custom_xml,
        "repeated custom XML replacement should write final custom XML payload");
    check(output_reader.read_entry("customXml/item1.xml") != stale_custom_xml,
        "repeated custom XML replacement should not write stale custom XML payload");
    check(output_reader.read_entry(custom_xml_relationships_entry)
            == source.custom_xml_relationships,
        "repeated custom XML replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "repeated custom XML replacement should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated custom XML replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated custom XML replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated custom XML replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated custom XML replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated custom XML replacement should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated custom XML replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "repeated custom XML replacement should keep package customXml relationship id");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "repeated custom XML replacement should preserve package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "repeated custom XML replacement should keep owner relationships readable");
    const auto* custom_xml_props_relationship =
        custom_xml_relationships->find_by_id("rIdCustomXmlProps");
    check(custom_xml_props_relationship != nullptr,
        "repeated custom XML replacement should keep customXmlProps relationship id");
    check(custom_xml_props_relationship->target == "itemProps1.xml",
        "repeated custom XML replacement should preserve customXmlProps target");
    check(output_reader.content_types().content_type_for(custom_xml_part) != nullptr,
        "repeated custom XML replacement should keep default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "repeated custom XML replacement should keep properties content type override");
}

void test_package_editor_removes_custom_xml_and_preserves_package_links()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "explicit custom XML item removal");

    check(editor.edit_plan().find_part(custom_xml_part) == nullptr,
        "custom XML removal should clear the active edit-plan part");
    const auto* removed_custom_xml =
        editor.edit_plan().find_removed_part(custom_xml_part);
    check(removed_custom_xml != nullptr,
        "custom XML removal should record removed-part audit");
    check(removed_custom_xml->reason.find("custom XML") != std::string::npos,
        "custom XML removal should keep readable removal reason");
    check(removed_custom_xml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML removal should audit preserved inbound relationship");
    check(removed_custom_xml->reason.find("_rels/.rels") != std::string::npos,
        "custom XML removal inbound audit should include package relationships entry");
    check(removed_custom_xml->reason.find("rIdCustomXml") != std::string::npos,
        "custom XML removal inbound audit should include package relationship id");
    check(removed_custom_xml->reason.find("customXml/item1.xml") != std::string::npos,
        "custom XML removal inbound audit should include raw package target");
    check(removed_custom_xml->inbound_relationships.size() == 1,
        "custom XML removal should keep one structured inbound audit");
    const auto& inbound = removed_custom_xml->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "custom XML removal should keep package inbound owner part empty");
    check(inbound.owner_entry == "_rels/.rels",
        "custom XML removal should keep package relationships entry");
    check(inbound.relationship_id == "rIdCustomXml",
        "custom XML removal should keep package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "custom XML removal should keep package relationship type");
    check(inbound.relationship_target == "customXml/item1.xml",
        "custom XML removal should keep package raw target");
    check(inbound.target_part == custom_xml_part,
        "custom XML removal should keep normalized custom XML target");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "custom XML removal should remove target part from manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML removal should not rewrite content types for default XML part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML removal should not rewrite package relationships");
    const auto* removed_custom_xml_relationships =
        editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels");
    check(removed_custom_xml_relationships != nullptr,
        "custom XML removal should omit source-owned custom XML relationships");
    check(removed_custom_xml_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML owner relationships omission should keep source relationship role");
    check(removed_custom_xml_relationships->owner_part == custom_xml_part.value(),
        "custom XML owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "custom XML removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep properties part copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal should keep unrelated unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "custom XML removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == custom_xml_part,
        "custom XML removal output plan should expose removed custom XML item");
    check(output_plan.removed_parts.front().reason.find("custom XML") != std::string::npos,
        "custom XML removal output plan should keep removed item reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "custom XML removal output plan should expose removed item inbound audit");
    check(output_plan.removed_package_entries.size() == 1,
        "custom XML removal output plan should expose owner relationships omission");
    check(output_plan.removed_package_entries.front().entry_name
            == "customXml/_rels/item1.xml.rels",
        "custom XML removal output plan should omit item owner relationships");
    check(output_plan.removed_package_entries.front().audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal output plan should classify omitted owner relationships");
    check(output_plan.removed_package_entries.front().owner_part == custom_xml_part.value(),
        "custom XML removal output plan should keep omitted owner context");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML removal output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("custom XML") != std::string::npos,
        "custom XML removal output plan should keep item removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "custom XML removal output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "custom XML removal output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML removal output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "customXml/_rels/itemProps1.xml.rels")
            == nullptr,
        "custom XML removal output plan should not invent properties relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "custom XML removal output should omit custom XML item");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "custom XML removal output should omit custom XML owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML removal output should preserve custom XML properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML removal should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML removal should preserve properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML removal should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML removal should keep package customXml target mode");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "custom XML removal should not keep owner relationships for absent item");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML removal should preserve default XML content type for item path");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML removal should preserve custom XML properties content type override");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after removal");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships to absent custom XML item");
}

void test_package_editor_custom_xml_replacement_restores_prior_removal()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-replace-after-remove-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(custom_xml_part, "temporary custom XML item removal");
    check(editor.edit_plan().find_removed_part(custom_xml_part) != nullptr,
        "setup should record removed custom XML before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup should omit custom XML owner relationships before replacement restore");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "setup should remove custom XML from manifest before replacement restore");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup should not rewrite content types for default custom XML removal");

    const std::string restored_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Restored custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, restored_custom_xml,
        "restored custom XML after removal");

    check(editor.edit_plan().find_removed_part(custom_xml_part) == nullptr,
        "custom XML replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels")
            == nullptr,
        "custom XML replacement after removal should clear stale owner relationships omission");
    const auto* custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(custom_xml_plan != nullptr,
        "custom XML replacement after removal should restore active edit-plan part");
    check(custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement after removal should keep final write mode");
    check(custom_xml_plan->reason.find("after removal") != std::string::npos,
        "custom XML replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, custom_xml_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "custom XML replacement after removal should restore manifest part write mode");
    const auto* custom_xml_relationships_audit =
        editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels");
    check(custom_xml_relationships_audit != nullptr,
        "custom XML replacement after removal should restore owner relationships audit");
    check(custom_xml_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal owner relationships should be copy-original");
    check(custom_xml_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement after removal owner relationships should keep source role");
    check(custom_xml_relationships_audit->owner_part == custom_xml_part.value(),
        "custom XML replacement after removal owner relationships should keep owner part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML replacement after removal should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep properties copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "custom XML replacement after removal output plan should clear stale removed-part audits");
    check(output_plan.removed_package_entries.empty(),
        "custom XML replacement after removal output plan should clear stale removed package-entry audits");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "custom XML replacement after removal output plan should rewrite custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML replacement after removal output plan should classify rewritten custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after removal") != std::string::npos,
        "custom XML replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML replacement after removal output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom XML replacement after removal output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") != entries.end(),
        "custom XML replacement after removal output should restore item entry");
    check(entries.find("customXml/_rels/item1.xml.rels") != entries.end(),
        "custom XML replacement after removal output should restore owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML replacement after removal output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, custom_xml_part.zip_path());
    check(output_reader.read_entry("customXml/item1.xml") == restored_custom_xml,
        "custom XML replacement after removal should write restored payload");
    check(output_reader.read_entry("customXml/_rels/item1.xml.rels")
            == source.custom_xml_relationships,
        "custom XML replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML replacement after removal should preserve properties bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML replacement after removal should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML replacement after removal should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML replacement after removal should not rewrite package customXml target");
    const auto* custom_xml_relationships =
        output_reader.relationships_for(custom_xml_part);
    check(custom_xml_relationships != nullptr,
        "custom XML replacement after removal should keep owner relationships readable");
    check(custom_xml_relationships->find_by_id("rIdCustomXmlProps") != nullptr,
        "custom XML replacement after removal should keep customXmlProps relationship");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after restore");
    check(output_graph.relationships_for(custom_xml_part)->find_by_id("rIdCustomXmlProps")
            != nullptr,
        "relationship graph should keep customXmlProps relationship after restore");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML replacement after removal should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML replacement after removal should preserve properties content type override");
}

void test_package_editor_custom_xml_removal_overrides_prior_replacement()
{
    const CustomXmlSourcePackage source =
        write_custom_xml_source_package("fastxlsx-package-editor-remove-after-replace-custom-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-custom-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName custom_xml_part("/customXml/item1.xml");
    const fastxlsx::detail::PartName custom_xml_props_part("/customXml/itemProps1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_custom_xml =
        R"(<fx:payload xmlns:fx="urn:fastxlsx:test-custom-xml">)"
        R"(<fx:value>Stale custom XML payload</fx:value>)"
        R"(</fx:payload>)";
    replace_part_with_memory_chunks(editor, custom_xml_part, stale_custom_xml,
        "prior custom XML replacement before removal");
    const auto* prior_custom_xml_plan = editor.edit_plan().find_part(custom_xml_part);
    check(prior_custom_xml_plan != nullptr,
        "setup should record active custom XML replacement before removal override");
    check(prior_custom_xml_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup custom XML replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels")
            != nullptr,
        "setup custom XML replacement should audit owner relationships");

    editor.remove_part(custom_xml_part, "explicit custom XML removal after replacement");

    check(editor.edit_plan().find_part(custom_xml_part) == nullptr,
        "custom XML removal after replacement should clear active replacement entry");
    const auto* removed_custom_xml =
        editor.edit_plan().find_removed_part(custom_xml_part);
    check(removed_custom_xml != nullptr,
        "custom XML removal after replacement should record removed-part audit");
    check(removed_custom_xml->reason.find("after replacement") != std::string::npos,
        "custom XML removal after replacement should keep final removal reason");
    check(removed_custom_xml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "custom XML removal after replacement should keep inbound relationship audit");
    check(removed_custom_xml->inbound_relationships.size() == 1,
        "custom XML removal after replacement should keep structured inbound audit");
    check(removed_custom_xml->inbound_relationships.front().target_part == custom_xml_part,
        "custom XML removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(custom_xml_part) == nullptr,
        "custom XML removal after replacement should remove manifest part");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "custom XML removal after replacement should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "custom XML removal after replacement should not rewrite package relationships");
    const auto* removed_custom_xml_relationships =
        editor.edit_plan().find_removed_package_entry("customXml/_rels/item1.xml.rels");
    check(removed_custom_xml_relationships != nullptr,
        "custom XML removal after replacement should omit owner relationships");
    check(removed_custom_xml_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal after replacement owner omission should keep source role");
    check(removed_custom_xml_relationships->owner_part == custom_xml_part.value(),
        "custom XML removal after replacement owner omission should keep owner part");
    check(editor.edit_plan().find_package_entry("customXml/_rels/item1.xml.rels") == nullptr,
        "custom XML removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(custom_xml_props_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep properties copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom XML removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "custom XML removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "custom XML removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "custom XML removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "customXml/item1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal after replacement output plan should omit custom XML item");
    check_output_entry_part_context(output_plan.entries, "customXml/item1.xml",
        true, custom_xml_part.value(),
        "custom XML removal after replacement output plan should classify omitted custom XML item");
    const auto* output_custom_xml_plan =
        find_output_entry_plan(output_plan.entries, "customXml/item1.xml");
    check(output_custom_xml_plan->reason.find("after replacement") != std::string::npos,
        "custom XML removal after replacement output plan should keep final removal reason");
    check(output_custom_xml_plan->inbound_relationships.size() == 1,
        "custom XML removal after replacement output plan should expose package inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "customXml/item1.xml", "", "_rels/.rels", "rIdCustomXml",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml",
        "customXml/item1.xml", custom_xml_part,
        "custom XML removal after replacement output plan should keep package inbound audit");
    check_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "custom XML removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, "customXml/_rels/item1.xml.rels",
        false, "",
        "custom XML removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_custom_xml_relationships_plan =
        find_output_entry_plan(output_plan.entries, "customXml/_rels/item1.xml.rels");
    check(output_custom_xml_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "custom XML removal after replacement output plan should classify owner relationships metadata");
    check(output_custom_xml_relationships_plan->owner_part == custom_xml_part.value(),
        "custom XML removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "customXml/itemProps1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve properties part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom XML removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("customXml/item1.xml") == entries.end(),
        "custom XML removal after replacement output should omit item entry");
    check(entries.find("customXml/_rels/item1.xml.rels") == entries.end(),
        "custom XML removal after replacement output should omit owner relationships");
    check(entries.find("customXml/itemProps1.xml") != entries.end(),
        "custom XML removal after replacement output should keep properties part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "custom XML removal after replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "custom XML removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "custom XML removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "custom XML removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "custom XML removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("customXml/itemProps1.xml")
            == source.custom_xml_properties,
        "custom XML removal after replacement should preserve properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom XML removal after replacement should preserve unknown bytes");

    const auto* package_custom_xml_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomXml");
    check(package_custom_xml_relationship != nullptr,
        "custom XML removal after replacement should keep package customXml relationship");
    check(package_custom_xml_relationship->target == "customXml/item1.xml",
        "custom XML removal after replacement should not rewrite package customXml target");
    check(package_custom_xml_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom XML removal after replacement should keep package customXml target mode");
    check(output_reader.relationships_for(custom_xml_part) == nullptr,
        "custom XML removal after replacement should not keep owner relationships");
    const auto* custom_xml_content_type =
        output_reader.content_types().content_type_for(custom_xml_part);
    check(custom_xml_content_type != nullptr && *custom_xml_content_type == "application/xml",
        "custom XML removal after replacement should preserve default XML content type");
    check(output_reader.content_types().override_for(custom_xml_props_part) != nullptr,
        "custom XML removal after replacement should preserve properties content type override");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomXml") != nullptr,
        "relationship graph should keep package customXml relationship after removal override");
    check(output_graph.relationships_for(custom_xml_part) == nullptr,
        "relationship graph should not attach owner relationships after removal override");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-linked-custom-xml")) {
            test_package_editor_worksheet_rewrite_preserves_custom_xml_parts();
            test_package_editor_replaces_custom_xml_and_preserves_package_links();
            test_package_editor_repeated_custom_xml_replacement_updates_final_state();
            test_package_editor_removes_custom_xml_and_preserves_package_links();
            test_package_editor_custom_xml_replacement_restores_prior_removal();
            test_package_editor_custom_xml_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
