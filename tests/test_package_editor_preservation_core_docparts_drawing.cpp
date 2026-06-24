#include "test_package_editor_preservation_core_docparts_common.hpp"

void test_package_editor_drawing_replacement_restores_prior_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-replace-after-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(drawing_part, "temporary drawing removal");
    check(editor.edit_plan().find_removed_part(drawing_part) != nullptr,
        "setup should record removed drawing before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            != nullptr,
        "setup should omit drawing owner relationships before replacement restore");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before drawing restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after drawing removal");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "setup should remove drawing from manifest before replacement restore");

    const std::string restored_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<xdr:twoCellAnchor><xdr:pic><xdr:blipFill><a:blip r:embed="rId1"/></xdr:blipFill></xdr:pic></xdr:twoCellAnchor>)"
        R"(</xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, drawing_part, restored_drawing,
        "restored drawing after removal");

    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "drawing replacement after removal should clear stale removed-part audit");
    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "drawing replacement after removal should restore active edit-plan part");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "drawing replacement after removal should keep final write mode");
    check(drawing_plan->reason.find("after removal") != std::string::npos,
        "drawing replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "drawing replacement after removal should restore manifest part write mode");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "drawing replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "drawing replacement after removal should clear stale owner relationships omission");
    const auto* drawing_relationships_audit =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_audit != nullptr,
        "drawing replacement after removal should restore drawing relationships copy audit");
    check(drawing_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should preserve drawing relationships bytes");
    check(drawing_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing replacement after removal should keep source relationship audit role");
    check(drawing_relationships_audit->owner_part == drawing_part.value(),
        "drawing replacement after removal should keep owner part on relationship audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "drawing replacement after removal should not rewrite worksheet relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "drawing replacement after removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep table copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing replacement after removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") != entries.end(),
        "drawing replacement after removal output should restore drawing part entry");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") != entries.end(),
        "drawing replacement after removal output should restore drawing relationships entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, drawing_part.zip_path());
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == restored_drawing,
        "drawing replacement after removal should write restored drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "drawing replacement after removal should restore drawing relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "drawing replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "drawing replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "drawing replacement after removal should preserve inbound worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "drawing replacement after removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "drawing replacement after removal should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "drawing replacement after removal should not rewrite inbound drawing target");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "drawing replacement after removal should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "drawing replacement after removal should not rewrite URI-qualified inbound drawing target");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "drawing replacement after removal should make drawing relationships readable again");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "drawing replacement after removal should keep image relationship id");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "drawing replacement after removal should keep chart relationship id");
    check(drawing_relationships->find_by_id("rId3") != nullptr,
        "drawing replacement after removal should keep external relationship id");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "drawing replacement after removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "drawing replacement after removal should preserve media bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "drawing replacement after removal should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "drawing replacement after removal should restore drawing content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "drawing replacement after removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "drawing replacement after removal should not promote PNG media to an override");
}

void test_package_editor_drawing_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName percent_encoded_drawing_part(
        "/xl/drawings/drawing space.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string stale_drawing =
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )"
        R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<xdr:twoCellAnchor><xdr:pic><xdr:blipFill><a:blip r:embed="rId1"/></xdr:blipFill></xdr:pic></xdr:twoCellAnchor>)"
        R"(</xdr:wsDr>)";
    replace_part_with_memory_chunks(editor, drawing_part, stale_drawing,
        "prior drawing replacement before removal");

    const auto* prior_drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(prior_drawing_plan != nullptr,
        "setup should record active drawing replacement before removal override");
    check(prior_drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup drawing replacement should be local-DOM-rewrite before removal override");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup drawing replacement should mirror write mode into manifest");
    const auto* prior_drawing_relationships =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(prior_drawing_relationships != nullptr,
        "setup should preserve drawing owner relationships before final removal");
    check(prior_drawing_relationships->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "setup drawing relationships audit should be copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup drawing replacement should not rewrite content types audit");
    check(editor.manifest().content_types().override_for(drawing_part) != nullptr,
        "setup drawing replacement should keep drawing content type override");

    editor.remove_part(drawing_part, "explicit drawing removal after replacement");

    check(editor.edit_plan().find_part(drawing_part) == nullptr,
        "drawing removal after replacement should clear active replacement entry");
    const auto* removed_drawing = editor.edit_plan().find_removed_part(drawing_part);
    check(removed_drawing != nullptr,
        "drawing removal after replacement should record removed-part audit");
    check(removed_drawing->reason.find("after replacement") != std::string::npos,
        "drawing removal after replacement should keep final removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "drawing removal after replacement should keep inbound relationship audit");
    check(removed_drawing->inbound_relationships.size() == 2,
        "drawing removal after replacement should keep structured inbound audits");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* direct_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* fragment_inbound = nullptr;
    for (const auto& audit : removed_drawing->inbound_relationships) {
        if (audit.relationship_id == "rId1") {
            direct_inbound = &audit;
        } else if (audit.relationship_id == "rId5") {
            fragment_inbound = &audit;
        }
    }
    check(direct_inbound != nullptr,
        "drawing removal after replacement should keep direct drawing inbound audit");
    check(direct_inbound->owner_part == worksheet_part.value(),
        "drawing removal after replacement should keep direct inbound owner part");
    check(direct_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "drawing removal after replacement should keep direct inbound owner entry");
    check(direct_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "drawing removal after replacement should keep direct inbound relationship type");
    check(direct_inbound->relationship_target == "../drawings/drawing1.xml",
        "drawing removal after replacement should keep direct inbound raw target");
    check(direct_inbound->target_part == drawing_part,
        "drawing removal after replacement should keep direct normalized target");
    check(fragment_inbound != nullptr,
        "drawing removal after replacement should keep URI-qualified drawing inbound audit");
    check(fragment_inbound->owner_part == worksheet_part.value(),
        "drawing removal after replacement should keep URI-qualified inbound owner part");
    check(fragment_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "drawing removal after replacement should keep URI-qualified inbound owner entry");
    check(fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "drawing removal after replacement should keep URI-qualified inbound relationship type");
    check(fragment_inbound->relationship_target == "../drawings/drawing1.xml#shape1",
        "drawing removal after replacement should keep URI-qualified raw target");
    check(fragment_inbound->target_part == drawing_part,
        "drawing removal after replacement should keep URI-qualified normalized target");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "drawing removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(drawing_part) == nullptr,
        "drawing removal after replacement should remove manifest content type override");

    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "drawing removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "drawing removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing removal after replacement content types audit should keep structured role");

    const auto* removed_drawing_relationships =
        editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(removed_drawing_relationships != nullptr,
        "drawing removal after replacement should omit source-owned drawing relationships");
    check(removed_drawing_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing removal after replacement owner relationships omission should keep source role");
    check(removed_drawing_relationships->owner_part == drawing_part.value(),
        "drawing removal after replacement owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "drawing removal after replacement should clear active owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "drawing removal after replacement should not rewrite worksheet relationships");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "drawing removal after replacement should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "drawing removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "drawing removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "drawing removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "drawing removal after replacement output plan should omit drawing part");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "drawing removal after replacement output plan should classify omitted drawing");
    const auto* output_drawing_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml");
    check(output_drawing_plan->reason.find("after replacement") != std::string::npos,
        "drawing removal after replacement output plan should keep final removal reason");
    check(output_drawing_plan->inbound_relationships.size() == 2,
        "drawing removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/drawing1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml", drawing_part,
        "drawing removal after replacement output plan should keep direct inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/drawings/drawing1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId5",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml#shape1", drawing_part,
        "drawing removal after replacement output plan should keep URI-qualified inbound audit");
    check_output_entry_plan(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "drawing removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "drawing removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_drawing_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check(output_drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "drawing removal after replacement output plan should classify owner relationships metadata");
    check(output_drawing_relationships_plan->owner_part == drawing_part.value(),
        "drawing removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "drawing removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "drawing removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "drawing removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "drawing removal after replacement output plan should preserve inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "drawing removal after replacement should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") == entries.end(),
        "drawing removal after replacement output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") == entries.end(),
        "drawing removal after replacement output should omit drawing owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(drawing_part) == nullptr,
        "drawing removal after replacement should remove drawing content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing1.xml",
        "drawing removal after replacement content types XML should omit drawing override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "drawing removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "drawing removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "drawing removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "drawing removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "drawing removal after replacement should preserve inbound worksheet relationships");
    check(output_reader.relationships_for(drawing_part) == nullptr,
        "drawing removal after replacement should not keep owner relationships for absent drawing");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "drawing removal after replacement should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "drawing removal after replacement should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "drawing removal after replacement should not rewrite inbound drawing target");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "drawing removal after replacement should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "drawing removal after replacement should not rewrite URI-qualified inbound target");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "drawing removal after replacement should keep table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "drawing removal after replacement should keep VML relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "drawing removal after replacement should keep percent-encoded drawing relationship");

    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "drawing removal after replacement should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "drawing removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "drawing removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "drawing removal after replacement should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "drawing removal after replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "drawing removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "drawing removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "drawing removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "drawing removal after replacement should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "drawing removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "drawing removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "drawing removal after replacement should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "drawing removal after replacement should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "drawing removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "drawing removal after replacement should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "drawing removal after replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "drawing removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "drawing removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "drawing removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "drawing removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "drawing removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "drawing removal after replacement should not promote PNG media to an override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-core-docparts-drawing")) {
            test_package_editor_drawing_replacement_restores_prior_removal();
            test_package_editor_drawing_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
