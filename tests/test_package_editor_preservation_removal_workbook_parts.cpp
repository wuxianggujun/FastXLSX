#include "test_package_editor_preservation_removal_common.hpp"

void test_package_editor_removes_table_and_preserves_worksheet_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.remove_part(table_part, "explicit table part removal");

    check(editor.edit_plan().find_part(table_part) == nullptr,
        "explicit table removal should clear the active edit-plan part");
    const auto* removed_table = editor.edit_plan().find_removed_part(table_part);
    check(removed_table != nullptr,
        "explicit table removal should record removed-part audit");
    check(removed_table->reason.find("table part") != std::string::npos,
        "explicit table removal should retain the removal reason");
    check(removed_table->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit table removal should audit preserved inbound relationships");
    check(removed_table->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit table removal inbound audit should include owner part");
    check(removed_table->reason.find("rId3") != std::string::npos,
        "explicit table removal inbound audit should include relationship id");
    check(removed_table->reason.find("../tables/table1.xml") != std::string::npos,
        "explicit table removal inbound audit should include original target");
    check(removed_table->inbound_relationships.size() == 1,
        "explicit table removal should keep structured inbound audit");
    const auto& table_inbound = removed_table->inbound_relationships.front();
    check(table_inbound.owner_part == worksheet_part.value(),
        "explicit table removal should keep inbound owner part");
    check(table_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit table removal should keep inbound owner relationship entry");
    check(table_inbound.relationship_id == "rId3",
        "explicit table removal should keep inbound relationship id");
    check(table_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "explicit table removal should keep inbound relationship type");
    check(table_inbound.relationship_target == "../tables/table1.xml",
        "explicit table removal should keep inbound raw target");
    check(table_inbound.target_part == table_part,
        "explicit table removal should keep normalized target part");
    check(editor.manifest().find_part(table_part) == nullptr,
        "explicit table removal should remove the part from the manifest");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit table removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit table removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit table removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/tables/_rels/table1.xml.rels")
            == nullptr,
        "explicit table removal should not invent missing table owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/tables/table1.xml") == entries.end(),
        "explicit table removal output should omit table part");
    check(entries.find("xl/tables/_rels/table1.xml.rels") == entries.end(),
        "explicit table removal output should not invent table owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(table_part) == nullptr,
        "explicit table removal output should remove table content type override");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit table removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit table removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(table_part) == nullptr,
        "explicit table removal should not keep owner relationships for absent table");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit table removal should keep worksheet relationships readable");
    const auto* table_link = worksheet_relationships->find_by_id("rId3");
    check(table_link != nullptr,
        "explicit table removal should keep inbound table relationship id");
    check(table_link->target == "../tables/table1.xml",
        "explicit table removal should not rewrite inbound table relationship target");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit table removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit table removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit table removal should preserve media bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit table removal should preserve chart bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit table removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit table removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit table removal should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit table removal should preserve unknown extension bytes");
}

void test_package_editor_removes_shared_strings_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-sharedstrings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-sharedstrings-output.xlsx");

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

    editor.remove_part(shared_strings_part, "explicit sharedStrings part removal");

    check(editor.edit_plan().find_part(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should clear the active edit-plan part");
    const auto* removed_shared_strings =
        editor.edit_plan().find_removed_part(shared_strings_part);
    check(removed_shared_strings != nullptr,
        "explicit sharedStrings removal should record removed-part audit");
    check(removed_shared_strings->reason.find("sharedStrings") != std::string::npos,
        "explicit sharedStrings removal should retain the removal reason");
    check(removed_shared_strings->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit sharedStrings removal should audit preserved inbound relationships");
    check(removed_shared_strings->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include owner part");
    check(removed_shared_strings->reason.find("rId3") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include relationship id");
    check(removed_shared_strings->reason.find("sharedStrings.xml") != std::string::npos,
        "explicit sharedStrings removal inbound audit should include original target");
    check(removed_shared_strings->inbound_relationships.size() == 1,
        "explicit sharedStrings removal should keep structured inbound audit");
    const auto& shared_strings_inbound =
        removed_shared_strings->inbound_relationships.front();
    check(shared_strings_inbound.owner_part == workbook_part.value(),
        "explicit sharedStrings removal should keep inbound owner part");
    check(shared_strings_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit sharedStrings removal should keep inbound owner relationship entry");
    check(shared_strings_inbound.relationship_id == "rId3",
        "explicit sharedStrings removal should keep inbound relationship id");
    check(shared_strings_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings",
        "explicit sharedStrings removal should keep inbound relationship type");
    check(shared_strings_inbound.relationship_target == "sharedStrings.xml",
        "explicit sharedStrings removal should keep inbound raw target");
    check(shared_strings_inbound.target_part == shared_strings_part,
        "explicit sharedStrings removal should keep normalized target part");
    check(editor.manifest().find_part(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit sharedStrings removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit sharedStrings removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit sharedStrings removal content types audit should keep structured role");
    const auto* removed_shared_strings_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(removed_shared_strings_relationships != nullptr,
        "explicit sharedStrings removal should audit omitted owner relationships");
    check(removed_shared_strings_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit sharedStrings owner relationships omission should keep source relationship role");
    check(removed_shared_strings_relationships->owner_part == shared_strings_part.value(),
        "explicit sharedStrings owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels")
            == nullptr,
        "explicit sharedStrings removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep table copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit sharedStrings removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/sharedStrings.xml") == entries.end(),
        "explicit sharedStrings removal output should omit sharedStrings part");
    check(entries.find("xl/_rels/sharedStrings.xml.rels") == entries.end(),
        "explicit sharedStrings removal output should omit owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal output should remove content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/sharedStrings.xml",
        "explicit sharedStrings removal content types XML should omit sharedStrings override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit sharedStrings removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit sharedStrings removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit sharedStrings removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(shared_strings_part) == nullptr,
        "explicit sharedStrings removal should not keep owner relationships for absent sharedStrings");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit sharedStrings removal should keep workbook relationships readable");
    const auto* shared_strings_link = workbook_relationships->find_by_id("rId3");
    check(shared_strings_link != nullptr,
        "explicit sharedStrings removal should keep inbound sharedStrings relationship id");
    check(shared_strings_link->target == "sharedStrings.xml",
        "explicit sharedStrings removal should not rewrite inbound sharedStrings target");
    check(shared_strings_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit sharedStrings removal should keep inbound sharedStrings target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit sharedStrings removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit sharedStrings removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit sharedStrings removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit sharedStrings removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit sharedStrings removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit sharedStrings removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit sharedStrings removal should preserve table bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit sharedStrings removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit sharedStrings removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit sharedStrings removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit sharedStrings removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit sharedStrings removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit sharedStrings removal should keep styles content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit sharedStrings removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit sharedStrings removal should not promote PNG media to an override");
}

void test_package_editor_removes_styles_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-styles-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-styles-output.xlsx");

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

    editor.remove_part(styles_part, "explicit styles part removal");

    check(editor.edit_plan().find_part(styles_part) == nullptr,
        "explicit styles removal should clear the active edit-plan part");
    const auto* removed_styles = editor.edit_plan().find_removed_part(styles_part);
    check(removed_styles != nullptr,
        "explicit styles removal should record removed-part audit");
    check(removed_styles->reason.find("styles part") != std::string::npos,
        "explicit styles removal should retain the removal reason");
    check(removed_styles->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit styles removal should audit preserved inbound relationships");
    check(removed_styles->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit styles removal inbound audit should include owner part");
    check(removed_styles->reason.find("rId4") != std::string::npos,
        "explicit styles removal inbound audit should include relationship id");
    check(removed_styles->reason.find("styles.xml") != std::string::npos,
        "explicit styles removal inbound audit should include original target");
    check(removed_styles->inbound_relationships.size() == 1,
        "explicit styles removal should keep structured inbound audit");
    const auto& styles_inbound = removed_styles->inbound_relationships.front();
    check(styles_inbound.owner_part == workbook_part.value(),
        "explicit styles removal should keep inbound owner part");
    check(styles_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit styles removal should keep inbound owner relationship entry");
    check(styles_inbound.relationship_id == "rId4",
        "explicit styles removal should keep inbound relationship id");
    check(styles_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
        "explicit styles removal should keep inbound relationship type");
    check(styles_inbound.relationship_target == "styles.xml",
        "explicit styles removal should keep inbound raw target");
    check(styles_inbound.target_part == styles_part,
        "explicit styles removal should keep normalized target part");
    check(editor.manifest().find_part(styles_part) == nullptr,
        "explicit styles removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(styles_part) == nullptr,
        "explicit styles removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit styles removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit styles removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit styles removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/styles.xml.rels")
            == nullptr,
        "explicit styles removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/styles.xml.rels") == nullptr,
        "explicit styles removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit styles removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/styles.xml") == entries.end(),
        "explicit styles removal output should omit styles part");
    check(entries.find("xl/_rels/styles.xml.rels") == entries.end(),
        "explicit styles removal output should not invent styles owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(styles_part) == nullptr,
        "explicit styles removal output should remove styles content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/styles.xml",
        "explicit styles removal content types XML should omit styles override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit styles removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit styles removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit styles removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(styles_part) == nullptr,
        "explicit styles removal should not create owner relationships for absent styles");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit styles removal should keep workbook relationships readable");
    const auto* styles_link = workbook_relationships->find_by_id("rId4");
    check(styles_link != nullptr,
        "explicit styles removal should keep inbound styles relationship id");
    check(styles_link->target == "styles.xml",
        "explicit styles removal should not rewrite inbound styles target");
    check(styles_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit styles removal should keep inbound styles target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit styles removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit styles removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit styles removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit styles removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit styles removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit styles removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit styles removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit styles removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit styles removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit styles removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit styles removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit styles removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit styles removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit styles removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit styles removal should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit styles removal should keep chart content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit styles removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit styles removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit styles removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit styles removal should not promote PNG media to an override");
}

void test_package_editor_removes_vba_project_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-vba-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-vba-output.xlsx");

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

    editor.remove_part(vba_part, "explicit VBA project part removal");

    check(editor.edit_plan().find_part(vba_part) == nullptr,
        "explicit VBA removal should clear the active edit-plan part");
    const auto* removed_vba = editor.edit_plan().find_removed_part(vba_part);
    check(removed_vba != nullptr,
        "explicit VBA removal should record removed-part audit");
    check(removed_vba->reason.find("VBA project") != std::string::npos,
        "explicit VBA removal should retain the removal reason");
    check(removed_vba->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit VBA removal should audit preserved inbound relationships");
    check(removed_vba->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit VBA removal inbound audit should include owner part");
    check(removed_vba->reason.find("rId2") != std::string::npos,
        "explicit VBA removal inbound audit should include relationship id");
    check(removed_vba->reason.find("vbaProject.bin") != std::string::npos,
        "explicit VBA removal inbound audit should include original target");
    check(removed_vba->inbound_relationships.size() == 1,
        "explicit VBA removal should keep structured inbound audit");
    const auto& vba_inbound = removed_vba->inbound_relationships.front();
    check(vba_inbound.owner_part == workbook_part.value(),
        "explicit VBA removal should keep inbound owner part");
    check(vba_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit VBA removal should keep inbound owner relationship entry");
    check(vba_inbound.relationship_id == "rId2",
        "explicit VBA removal should keep inbound relationship id");
    check(vba_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject",
        "explicit VBA removal should keep inbound relationship type");
    check(vba_inbound.relationship_target == "vbaProject.bin",
        "explicit VBA removal should keep inbound raw target");
    check(vba_inbound.target_part == vba_part,
        "explicit VBA removal should keep normalized target part");
    check(editor.manifest().find_part(vba_part) == nullptr,
        "explicit VBA removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(vba_part) == nullptr,
        "explicit VBA removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit VBA removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit VBA removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit VBA removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/_rels/vbaProject.bin.rels")
            == nullptr,
        "explicit VBA removal should not invent missing owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/_rels/vbaProject.bin.rels") == nullptr,
        "explicit VBA removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep styles copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit VBA removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/vbaProject.bin") == entries.end(),
        "explicit VBA removal output should omit VBA project part");
    check(entries.find("xl/_rels/vbaProject.bin.rels") == entries.end(),
        "explicit VBA removal output should not invent VBA owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(vba_part) == nullptr,
        "explicit VBA removal output should remove VBA content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/vbaProject.bin",
        "explicit VBA removal content types XML should omit VBA override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit VBA removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit VBA removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit VBA removal should not prune inbound workbook relationships");
    check(output_reader.relationships_for(vba_part) == nullptr,
        "explicit VBA removal should not create owner relationships for absent VBA project");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit VBA removal should keep workbook relationships readable");
    const auto* vba_link = workbook_relationships->find_by_id("rId2");
    check(vba_link != nullptr,
        "explicit VBA removal should keep inbound VBA relationship id");
    check(vba_link->target == "vbaProject.bin",
        "explicit VBA removal should not rewrite inbound VBA target");
    check(vba_link->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit VBA removal should keep inbound VBA target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit VBA removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit VBA removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit VBA removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit VBA removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit VBA removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit VBA removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit VBA removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit VBA removal should preserve VML drawing bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit VBA removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit VBA removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit VBA removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit VBA removal should preserve styles bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit VBA removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit VBA removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit VBA removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit VBA removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit VBA removal should keep styles content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit VBA removal should keep table content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit VBA removal should keep chart content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit VBA removal should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit VBA removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit VBA removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit VBA removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit VBA removal should not promote PNG media to an override");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation removal shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-removal-workbook-parts")) {
            test_package_editor_removes_table_and_preserves_worksheet_links();
            test_package_editor_removes_shared_strings_and_preserves_workbook_links();
            test_package_editor_removes_styles_and_preserves_workbook_links();
            test_package_editor_removes_vba_project_and_preserves_workbook_links();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
