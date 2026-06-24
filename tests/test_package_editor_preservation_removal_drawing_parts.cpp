#include "test_package_editor_preservation_removal_common.hpp"

void test_package_editor_removes_vml_drawing_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-vml-output.xlsx");

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

    editor.remove_part(vml_drawing_part, "explicit VML drawing part removal");

    check(editor.edit_plan().find_part(vml_drawing_part) == nullptr,
        "explicit VML removal should clear the active edit-plan part");
    const auto* removed_vml = editor.edit_plan().find_removed_part(vml_drawing_part);
    check(removed_vml != nullptr,
        "explicit VML removal should record removed-part audit");
    check(removed_vml->reason.find("VML drawing") != std::string::npos,
        "explicit VML removal should retain the removal reason");
    check(removed_vml->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit VML removal should audit preserved inbound relationships");
    check(removed_vml->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit VML removal inbound audit should include owner part");
    check(removed_vml->reason.find("rId7") != std::string::npos,
        "explicit VML removal inbound audit should include relationship id");
    check(removed_vml->reason.find("../drawings/vmlDrawing1.vml#shape1")
            != std::string::npos,
        "explicit VML removal inbound audit should include original target");
    check(removed_vml->inbound_relationships.size() == 1,
        "explicit VML removal should keep structured inbound audit");
    const auto& vml_inbound = removed_vml->inbound_relationships.front();
    check(vml_inbound.owner_part == worksheet_part.value(),
        "explicit VML removal should keep inbound owner part");
    check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit VML removal should keep inbound owner relationship entry");
    check(vml_inbound.relationship_id == "rId7",
        "explicit VML removal should keep inbound relationship id");
    check(vml_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "explicit VML removal should keep inbound relationship type");
    check(vml_inbound.relationship_target == "../drawings/vmlDrawing1.vml#shape1",
        "explicit VML removal should keep inbound raw target");
    check(vml_inbound.target_part == vml_drawing_part,
        "explicit VML removal should keep normalized target part");
    check(editor.manifest().find_part(vml_drawing_part) == nullptr,
        "explicit VML removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(vml_drawing_part) == nullptr,
        "explicit VML removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit VML removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit VML removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit VML removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "explicit VML removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/vmlDrawing1.vml.rels")
            == nullptr,
        "explicit VML removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep table copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VML removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/vmlDrawing1.vml") == entries.end(),
        "explicit VML removal output should omit VML drawing part");
    check(entries.find("xl/drawings/_rels/vmlDrawing1.vml.rels") == entries.end(),
        "explicit VML removal output should not invent VML owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vml_drawing_part) == nullptr,
        "explicit VML removal output should remove VML content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/vmlDrawing1.vml",
        "explicit VML removal content types XML should omit VML override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit VML removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit VML removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit VML removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit VML removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit VML removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(vml_drawing_part) == nullptr,
        "explicit VML removal should not create owner relationships for absent VML drawing");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit VML removal should keep worksheet relationships readable");
    const auto* vml_link = worksheet_relationships->find_by_id("rId7");
    check(vml_link != nullptr,
        "explicit VML removal should keep inbound VML relationship id");
    check(vml_link->target == "../drawings/vmlDrawing1.vml#shape1",
        "explicit VML removal should not rewrite inbound VML relationship target");
    check(vml_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit VML removal should keep inbound VML relationship target mode");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "explicit VML removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit VML removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "explicit VML removal should keep percent-encoded drawing relationship");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit VML removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit VML removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit VML removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit VML removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit VML removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit VML removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit VML removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit VML removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit VML removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit VML removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit VML removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit VML removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit VML removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit VML removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit VML removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit VML removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit VML removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit VML removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit VML removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit VML removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit VML removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit VML removal should not promote PNG media to an override");
}

void test_package_editor_removes_percent_decoded_drawing_and_preserves_encoded_link()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-remove-percent-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-percent-drawing-output.xlsx");

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

    editor.remove_part(percent_encoded_drawing_part,
        "explicit percent-decoded drawing part removal");

    check(editor.edit_plan().find_part(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should clear the active edit-plan part");
    const auto* removed_drawing =
        editor.edit_plan().find_removed_part(percent_encoded_drawing_part);
    check(removed_drawing != nullptr,
        "explicit percent-decoded drawing removal should record removed-part audit");
    check(removed_drawing->reason.find("percent-decoded drawing") != std::string::npos,
        "explicit percent-decoded drawing removal should retain the removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit percent-decoded drawing removal should audit preserved inbound relationships");
    check(removed_drawing->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include owner part");
    check(removed_drawing->reason.find("rId8") != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include relationship id");
    check(removed_drawing->reason.find("../drawings/drawing%20space.xml")
            != std::string::npos,
        "explicit percent-decoded drawing removal inbound audit should include raw target");
    check(removed_drawing->inbound_relationships.size() == 1,
        "explicit percent-decoded drawing removal should keep structured inbound audit");
    const auto& drawing_inbound = removed_drawing->inbound_relationships.front();
    check(drawing_inbound.owner_part == worksheet_part.value(),
        "explicit percent-decoded drawing removal should keep inbound owner part");
    check(drawing_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit percent-decoded drawing removal should keep inbound owner relationship entry");
    check(drawing_inbound.relationship_id == "rId8",
        "explicit percent-decoded drawing removal should keep inbound relationship id");
    check(drawing_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit percent-decoded drawing removal should keep inbound relationship type");
    check(drawing_inbound.relationship_target == "../drawings/drawing%20space.xml",
        "explicit percent-decoded drawing removal should keep inbound raw target");
    check(drawing_inbound.target_part == percent_encoded_drawing_part,
        "explicit percent-decoded drawing removal should keep normalized target part");
    check(editor.manifest().find_part(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "explicit percent-decoded drawing removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit percent-decoded drawing removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit percent-decoded drawing removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit percent-decoded drawing removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/drawings/_rels/drawing space.xml.rels") == nullptr,
        "explicit percent-decoded drawing removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing space.xml.rels")
            == nullptr,
        "explicit percent-decoded drawing removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit percent-decoded drawing removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing space.xml") == entries.end(),
        "explicit percent-decoded drawing removal output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing space.xml.rels") == entries.end(),
        "explicit percent-decoded drawing removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(percent_encoded_drawing_part)
            == nullptr,
        "explicit percent-decoded drawing removal output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing space.xml",
        "explicit percent-decoded drawing removal content types XML should omit override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit percent-decoded drawing removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit percent-decoded drawing removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit percent-decoded drawing removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit percent-decoded drawing removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit percent-decoded drawing removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(percent_encoded_drawing_part) == nullptr,
        "explicit percent-decoded drawing removal should not create owner relationships");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId8");
    check(drawing_link != nullptr,
        "explicit percent-decoded drawing removal should keep inbound relationship id");
    check(drawing_link->target == "../drawings/drawing%20space.xml",
        "explicit percent-decoded drawing removal should not rewrite encoded target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit percent-decoded drawing removal should keep target mode internal");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet drawing relationship");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "explicit percent-decoded drawing removal should keep worksheet VML relationship");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit percent-decoded drawing removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit percent-decoded drawing removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit percent-decoded drawing removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit percent-decoded drawing removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit percent-decoded drawing removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit percent-decoded drawing removal should preserve VML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit percent-decoded drawing removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit percent-decoded drawing removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit percent-decoded drawing removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit percent-decoded drawing removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit percent-decoded drawing removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit percent-decoded drawing removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit percent-decoded drawing removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit percent-decoded drawing removal should keep VML content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit percent-decoded drawing removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit percent-decoded drawing removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit percent-decoded drawing removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit percent-decoded drawing removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit percent-decoded drawing removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit percent-decoded drawing removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit percent-decoded drawing removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit percent-decoded drawing removal should not promote PNG media to an override");
}

void test_package_editor_chart_removal_overrides_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-after-replace-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-chart-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
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

    const std::string replacement_chart =
        R"(<c:chartSpace xmlns:c="http://schemas.openxmlformats.org/drawingml/2006/chart">)"
        R"(<c:chart><c:title><c:tx><c:v>stale replacement</c:v></c:tx></c:title></c:chart>)"
        R"(</c:chartSpace>)";
    replace_part_with_memory_chunks(editor, chart_part, replacement_chart,
        "prior chart replacement before removal");

    const auto* prior_chart_plan = editor.edit_plan().find_part(chart_part);
    check(prior_chart_plan != nullptr,
        "setup should record active chart replacement before removal override");
    check(prior_chart_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup chart replacement should be local-DOM-rewrite before removal override");
    check_manifest_write_mode(editor, chart_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup chart replacement should mirror write mode into manifest");
    check(editor.manifest().content_types().override_for(chart_part) != nullptr,
        "setup chart replacement should keep chart content type override");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "setup chart replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "setup chart replacement should not invent chart owner relationships audit");

    editor.remove_part(chart_part, "explicit chart removal after replacement");

    check(editor.edit_plan().find_part(chart_part) == nullptr,
        "chart removal after replacement should clear active replacement entry");
    const auto* removed_chart = editor.edit_plan().find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "chart removal after replacement should record removed-part audit");
    check(removed_chart->reason.find("after replacement") != std::string::npos,
        "chart removal after replacement should keep final removal reason");
    check(removed_chart->reason.find("inbound relationship preserved")
            != std::string::npos,
        "chart removal after replacement should keep inbound relationship audit");
    check(removed_chart->inbound_relationships.size() == 2,
        "chart removal after replacement should keep structured inbound audits");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_fragment_inbound = nullptr;
    for (const auto& audit : removed_chart->inbound_relationships) {
        if (audit.relationship_id == "rId2") {
            chart_inbound = &audit;
        } else if (audit.relationship_id == "rId4") {
            chart_fragment_inbound = &audit;
        }
    }
    check(chart_inbound != nullptr,
        "chart removal after replacement should keep direct chart inbound audit");
    check(chart_inbound->owner_part == drawing_part.value(),
        "chart removal after replacement should keep direct inbound owner part");
    check(chart_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "chart removal after replacement should keep direct inbound owner entry");
    check(chart_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "chart removal after replacement should keep direct inbound relationship type");
    check(chart_inbound->relationship_target == "../charts/chart1.xml",
        "chart removal after replacement should keep direct inbound raw target");
    check(chart_inbound->target_part == chart_part,
        "chart removal after replacement should keep direct normalized target");
    check(chart_fragment_inbound != nullptr,
        "chart removal after replacement should keep URI-qualified chart inbound audit");
    check(chart_fragment_inbound->owner_part == drawing_part.value(),
        "chart removal after replacement should keep URI-qualified inbound owner part");
    check(chart_fragment_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "chart removal after replacement should keep URI-qualified inbound owner entry");
    check(chart_fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "chart removal after replacement should keep URI-qualified inbound relationship type");
    check(chart_fragment_inbound->relationship_target == "../charts/chart1.xml#plotArea",
        "chart removal after replacement should keep URI-qualified raw target");
    check(chart_fragment_inbound->target_part == chart_part,
        "chart removal after replacement should keep URI-qualified normalized target");
    check(editor.manifest().find_part(chart_part) == nullptr,
        "chart removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(chart_part) == nullptr,
        "chart removal after replacement should remove manifest chart override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "chart removal after replacement should still rewrite content types");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "chart removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/charts/_rels/chart1.xml.rels") == nullptr,
        "chart removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep drawing copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep image copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep VBA project copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "chart removal after replacement should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "chart removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "chart removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "chart removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "chart removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == chart_part,
        "chart removal after replacement output plan should expose removed chart");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "chart removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 2,
        "chart removal after replacement output plan should keep removed-part inbound audits");
    check(output_plan.removed_package_entries.empty(),
        "chart removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "chart removal after replacement output plan should omit chart part");
    check_output_entry_part_context(output_plan.entries, "xl/charts/chart1.xml",
        true, chart_part.value(),
        "chart removal after replacement output plan should classify omitted chart");
    const auto* output_chart_plan =
        find_output_entry_plan(output_plan.entries, "xl/charts/chart1.xml");
    check(output_chart_plan->reason.find("after replacement") != std::string::npos,
        "chart removal after replacement output plan should keep final removal reason");
    check(output_chart_plan->inbound_relationships.size() == 2,
        "chart removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/charts/chart1.xml", drawing_part.value(),
        "xl/drawings/_rels/drawing1.xml.rels", "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml", chart_part,
        "chart removal after replacement output plan should keep direct inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/charts/chart1.xml", drawing_part.value(),
        "xl/drawings/_rels/drawing1.xml.rels", "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml#plotArea", chart_part,
        "chart removal after replacement output plan should keep URI-qualified inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "chart removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "chart removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "chart removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "chart removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "chart removal after replacement output plan should preserve inbound drawing relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/charts/chart1.xml") == entries.end(),
        "chart removal after replacement output should omit target part");
    check(entries.find("xl/charts/_rels/chart1.xml.rels") == entries.end(),
        "chart removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_not_contains(output_reader.read_entry("[Content_Types].xml"),
        "/xl/charts/chart1.xml",
        "chart removal after replacement output should omit chart content type override");
    check(output_reader.content_types().override_for(chart_part) == nullptr,
        "chart removal after replacement should remove chart content type override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "chart removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "chart removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "chart removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "chart removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "chart removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "chart removal after replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "chart removal after replacement should not prune inbound drawing relationships");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "chart removal after replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "chart removal after replacement should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "chart removal after replacement should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "chart removal after replacement should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "chart removal after replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "chart removal after replacement should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "chart removal after replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "chart removal after replacement should preserve VBA project bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "chart removal after replacement should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "chart removal after replacement should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "chart removal after replacement should preserve unknown extension relationships bytes");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "chart removal after replacement should keep drawing relationships readable");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "chart removal after replacement should keep inbound chart relationship id");
    const auto* chart_link = drawing_relationships->find_by_id("rId2");
    check(chart_link->target == "../charts/chart1.xml",
        "chart removal after replacement should not rewrite direct chart target");
    check(chart_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart removal after replacement should keep direct chart target mode");
    const auto* chart_fragment_link = drawing_relationships->find_by_id("rId4");
    check(chart_fragment_link != nullptr,
        "chart removal after replacement should keep URI-qualified chart relationship id");
    check(chart_fragment_link->target == "../charts/chart1.xml#plotArea",
        "chart removal after replacement should not rewrite URI-qualified chart target");
    check(chart_fragment_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "chart removal after replacement should keep URI-qualified chart target mode");
    check(output_reader.relationships_for(chart_part) == nullptr,
        "chart removal after replacement should not keep owner relationships for absent chart");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "chart removal after replacement should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "chart removal after replacement should keep VML drawing content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "chart removal after replacement should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "chart removal after replacement should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "chart removal after replacement should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "chart removal after replacement should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "chart removal after replacement should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "chart removal after replacement should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "chart removal after replacement should not promote PNG media to an override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation removal shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-removal-drawing-parts")) {
            test_package_editor_removes_vml_drawing_and_preserves_worksheet_links();
            test_package_editor_removes_percent_decoded_drawing_and_preserves_encoded_link();
            test_package_editor_chart_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
