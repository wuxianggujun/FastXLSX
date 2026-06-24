#include "test_package_editor_core_common.hpp"

void test_package_editor_repeated_part_replacement_updates_final_state()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-repeated-replacement-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeated-replacement-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();

    const std::string first_workbook =
        R"(<workbook><sheets><sheet name="First" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    const std::string final_workbook =
        R"(<workbook><sheets><sheet name="Final" sheetId="1" r:id="rId1"/></sheets></workbook>)";
    editor.replace_part(workbook_part, first_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "first workbook local-DOM rewrite");
    editor.replace_part(workbook_part, final_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final workbook local-DOM rewrite");

    check(editor.edit_plan().size() == initial_plan_size,
        "repeated replacement should upsert the existing edit-plan part entry");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "repeated replacement should keep workbook in the edit plan");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "repeated replacement should keep the latest write mode in the edit plan");
    check(workbook_plan->reason.find("final workbook local-DOM rewrite") != std::string::npos,
        "repeated replacement should keep the latest reason in the edit plan");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "repeated replacement should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "repeated replacement should mirror the latest write mode into the manifest");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "repeated replacement should keep workbook manifest dirty but not generated");
    check(editor.edit_plan().package_entries().size() == 1,
        "repeated replacement should upsert preserved source relationships audit");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr,
        "repeated replacement should audit preserved workbook relationships");
    check(workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved workbook relationships audit should stay copy-original");
    check(workbook_relationships_plan->reason.find("/xl/workbook.xml") != std::string::npos,
        "preserved workbook relationships audit should name the owner part");
    check(workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "preserved workbook relationships audit should keep structured source-relationships role");
    check(workbook_relationships_plan->owner_part == workbook_part.value(),
        "preserved workbook relationships audit should keep structured owner part");
    check(editor.edit_plan().removed_parts().empty(),
        "repeated replacement should not record removed parts");
    check(editor.edit_plan().removed_package_entries().empty(),
        "repeated replacement should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "ordinary repeated replacement should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "ordinary repeated replacement should leave calcChain action unchanged");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == final_workbook,
        "repeated replacement output should write the final replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated replacement output should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "repeated replacement output should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated replacement output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated replacement output should preserve unknown bytes");
}

void test_package_editor_replacement_audits_preserved_root_relationships()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-root-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-root-rels-output.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>)";
    const std::string root_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/opaque.bin"/>)"
        R"(</Relationships>)";
    const std::string unknown = "root-owned opaque bytes";

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"root.xml", "<root/>"},
            {"_rels/root.xml.rels", root_relationships},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName root_part("/root.xml");
    const std::string replacement_root = R"(<root updated="1"/>)";
    replace_part_with_memory_chunks(editor, root_part, replacement_root,
        "test root part rewrite");

    const auto* root_plan = editor.edit_plan().find_part(root_part);
    check(root_plan != nullptr,
        "root replacement should remain visible in the edit plan");
    check(root_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "root replacement should be staged stream-rewrite");
    const auto* root_relationships_plan =
        editor.edit_plan().find_package_entry("_rels/root.xml.rels");
    check(root_relationships_plan != nullptr,
        "root replacement should audit preserved root source relationships");
    check(root_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved root source relationships should be copy-original");
    check(root_relationships_plan->reason.find("/root.xml") != std::string::npos,
        "root relationships audit should name the owner part");
    check(root_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "root relationships audit should keep structured source-relationships role");
    check(root_relationships_plan->owner_part == root_part.value(),
        "root relationships audit should keep structured owner part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("root.xml") == replacement_root,
        "root replacement should write replacement bytes");
    check(output_reader.read_entry("_rels/root.xml.rels") == root_relationships,
        "root replacement should preserve root relationship bytes");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "root replacement should preserve unknown linked bytes");
    const auto* output_root_relationships = output_reader.relationships_for(root_part);
    check(output_root_relationships != nullptr,
        "root replacement output should keep root source relationships readable");
    check(output_root_relationships->find_by_id("rId1") != nullptr,
        "root replacement output should keep root source relationship id");
    check(output_root_relationships->find_by_id("rId1")->target == "custom/opaque.bin",
        "root replacement output root source relationship target mismatch");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* output_graph_root_relationships =
        output_graph.relationships_for(root_part);
    check(output_graph_root_relationships != nullptr,
        "root replacement output relationship graph should include root source relationships");
    check(output_graph_root_relationships->find_by_id("rId1") != nullptr,
        "root replacement output relationship graph should keep root source relationship id");
    check(output_reader.part_index().find_part(
              fastxlsx::detail::PartName("/_rels/root.xml.rels")) == nullptr,
        "root relationships entry should remain metadata-only after replacement");
}

void test_package_editor_sets_document_properties_and_adds_missing_metadata_parts()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(editor.manifest().find_part(app_part) == nullptr,
        "source package should start without extended properties");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Patch <Author>";
    properties.last_modified_by = "Patch & Reviewer";
    properties.title = "Existing metadata";
    properties.description = "Generated by PackageEditor";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.0";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "document properties rewrite should record core properties plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "core properties should be generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "document properties rewrite should record extended properties plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "extended properties should be generated small XML");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties rewrite should keep unknown parts copy-original");
    const auto* content_types_plan =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_plan != nullptr,
        "document properties rewrite should record content types entry rewrite");
    check(content_types_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "document properties content types entry should be local-DOM-rewrite");
    check(content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "document properties content types entry should keep structured audit role");
    check(content_types_plan->owner_part.empty(),
        "document properties content types entry should not carry source owner");
    const auto* package_relationships_plan =
        editor.edit_plan().find_package_entry("_rels/.rels");
    check(package_relationships_plan != nullptr,
        "document properties rewrite should record package relationships entry rewrite");
    check(package_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "document properties package relationships entry should be local-DOM-rewrite");
    check(package_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "document properties package relationships entry should keep structured audit role");
    check(package_relationships_plan->owner_part.empty(),
        "package relationships entry should not carry source owner");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "document properties rewrite should keep core properties in manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->generated && core_manifest_part->dirty
            && !core_manifest_part->preserve_original,
        "document properties rewrite should mark core properties generated");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "document properties rewrite should add extended properties to manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && app_manifest_part->generated && app_manifest_part->dirty
            && !app_manifest_part->preserve_original,
        "document properties rewrite should mark extended properties generated");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "document properties output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "document properties output plan should keep content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "document properties output plan should classify content types metadata");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "document properties output plan should rewrite package relationships");
    check_output_entry_part_context(output_plan, "_rels/.rels", false, "",
        "document properties output plan should keep package relationships as metadata entry");
    const auto* output_package_relationships_plan =
        find_output_entry_plan(output_plan, "_rels/.rels");
    check(output_package_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "document properties output plan should classify package relationships metadata");
    check_output_entry_plan(output_plan, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, true, true, false, false,
        "document properties output plan should regenerate core properties");
    check_output_entry_part_context(output_plan, "docProps/core.xml", true,
        "/docProps/core.xml",
        "document properties output plan should classify core properties as package part");
    check_output_entry_materialized_replacement(output_plan, "docProps/core.xml", true,
        "document properties output plan should expose core properties as materialized package-part replacement");
    check_output_entry_materialized_replacement_reason(output_plan, "docProps/core.xml",
        "generated small-XML package part",
        "document properties output plan should explain core properties package-part materialization");
    check_output_entry_plan(output_plan, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, false, true, false, false,
        "document properties output plan should append generated app properties");
    check_output_entry_part_context(output_plan, "docProps/app.xml", true,
        "/docProps/app.xml",
        "document properties output plan should classify app properties as package part");
    check_output_entry_materialized_replacement(output_plan, "docProps/app.xml", true,
        "document properties output plan should expose app properties as materialized package-part replacement");
    check_output_entry_materialized_replacement_reason(output_plan, "docProps/app.xml",
        "generated small-XML package part",
        "document properties output plan should explain app properties package-part materialization");
    check_output_entry_plan(output_plan, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "document properties output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "document properties output plan should classify unknown entry as package part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "document properties rewrite should add missing app properties entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Patch &lt;Author&gt;</dc:creator>",
        "core properties creator should be generated and escaped");
    check_contains(core_xml, "<cp:lastModifiedBy>Patch &amp; Reviewer</cp:lastModifiedBy>",
        "core properties lastModifiedBy should be generated and escaped");
    check_contains(core_xml, "<dc:title>Existing metadata</dc:title>",
        "core properties title should be generated");
    check_contains(core_xml, "<dc:description>Generated by PackageEditor</dc:description>",
        "core properties description should be generated");

    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "extended properties application should be generated");
    check_contains(app_xml, "<AppVersion>4.0</AppVersion>",
        "extended properties app version should be generated");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "document properties rewrite should keep core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "document properties rewrite should add app content type override");
    check_contains(content_types,
        "application/vnd.openxmlformats-officedocument.extended-properties+xml",
        "document properties rewrite should add app content type");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "document properties rewrite should keep core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "document properties rewrite should add app package relationship");
    check_contains(package_relationships, "relationships/extended-properties",
        "document properties rewrite should add app package relationship type");
    const auto* parsed_app_relationship =
        output_reader.package_relationships().find_by_id("rId3");
    check(parsed_app_relationship != nullptr,
        "document properties rewrite should allocate a new package relationship id");
    check(parsed_app_relationship->target == "docProps/app.xml",
        "document properties rewrite app relationship target mismatch");

    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "document properties rewrite should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "document properties rewrite should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "document properties rewrite should preserve unknown bytes");
}

void test_package_editor_document_properties_preserves_custom_properties_part()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-custom-source.xlsx");
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>)"
        R"(<Override PartName="/docProps/custom.xml" ContentType="application/vnd.openxmlformats-officedocument.custom-properties+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rIdCustomProperties" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties" Target="docProps/custom.xml"/>)"
        R"(</Relationships>)";
    const std::string custom_properties =
        R"(<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/custom-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">)"
        R"(<property fmtid="{D5CDD505-2E9C-101B-9397-08002B2CF9AE}" pid="2" name="FastXLSXMarker"><vt:lpwstr>preserve custom property</vt:lpwstr></property>)"
        R"(</Properties>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/custom.xml", custom_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-custom-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName custom_part("/docProps/custom.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Core Rewriter";
    properties.application = "Extended Rewriter";
    editor.set_document_properties(properties);

    const auto* custom_plan = editor.edit_plan().find_part(custom_part);
    check(custom_plan != nullptr,
        "custom properties part should remain visible in document properties edit plan");
    check(custom_plan != nullptr
            && custom_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom properties part should remain copy-original during core/app rewrite");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "core properties should remain visible with custom properties present");
    check(core_plan != nullptr
            && core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "core properties should still be generated small XML with custom properties present");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "extended properties should remain visible with custom properties present");
    check(app_plan != nullptr
            && app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "extended properties should still be generated small XML with custom properties present");
    const auto* unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(unknown_plan != nullptr,
        "unknown part should remain visible with custom properties present");
    check(unknown_plan != nullptr
            && unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "custom docprops rewrite should keep unrelated unknown part copy-original");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "custom docprops rewrite should audit content types rewrite for missing app props");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "custom docprops rewrite should audit package relationships rewrite for missing app props");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "custom docprops output plan should match entry preview size");
    check(output_plan.relationship_target_audits.empty(),
        "custom docprops output plan should not invent relationship target audits");
    check(output_plan.removed_parts.empty(),
        "custom docprops output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "custom docprops output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom docprops output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "custom docprops output plan should keep content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan != nullptr
            && output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "custom docprops output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "custom docprops output plan should rewrite package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "custom docprops output plan should keep package relationships as metadata entry");
    const auto* output_package_relationships_plan =
        find_output_entry_plan(output_plan.entries, "_rels/.rels");
    check(output_package_relationships_plan != nullptr
            && output_package_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::PackageRelationships,
        "custom docprops output plan should classify package relationships metadata");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, true, true, false, false,
        "custom docprops output plan should regenerate core properties");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "custom docprops output plan should classify core properties as package part");
    check_output_entry_plan(output_plan.entries, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::GenerateSmallXml, false, true, false, false,
        "custom docprops output plan should append generated app properties");
    check_output_entry_part_context(output_plan.entries, "docProps/app.xml", true,
        app_part.value(),
        "custom docprops output plan should classify app properties as package part");
    check_output_entry_plan(output_plan.entries, "docProps/custom.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom docprops output plan should preserve custom properties part");
    check_output_entry_part_context(output_plan.entries, "docProps/custom.xml", true,
        custom_part.value(),
        "custom docprops output plan should classify custom properties as package part");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "custom docprops output plan should preserve unrelated unknown part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "custom docprops output plan should classify unknown entry as package part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/custom.xml") == custom_properties,
        "document properties rewrite should preserve custom properties bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "custom docprops rewrite should preserve unknown bytes");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/custom.xml",
        "custom docprops rewrite should preserve custom properties content type override");
    check_contains(content_types,
        "application/vnd.openxmlformats-officedocument.custom-properties+xml",
        "custom docprops rewrite should preserve custom properties content type");
    check_contains(content_types, "/docProps/app.xml",
        "custom docprops rewrite should still add app properties content type");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/custom.xml\"",
        "custom docprops rewrite should preserve custom properties package relationship");
    check_contains(package_relationships, "relationships/custom-properties",
        "custom docprops rewrite should preserve custom properties relationship type");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "custom docprops rewrite should still add app package relationship");

    const auto* custom_relationship =
        output_reader.package_relationships().find_by_id("rIdCustomProperties");
    check(custom_relationship != nullptr,
        "custom docprops rewrite should keep parsed custom properties relationship id");
    check(custom_relationship != nullptr
            && custom_relationship->target == "docProps/custom.xml",
        "custom docprops rewrite should keep parsed custom properties relationship target");
    check(custom_relationship != nullptr
            && custom_relationship->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
        "custom docprops rewrite should keep custom properties relationship internal");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.package_relationships().find_by_id("rIdCustomProperties") != nullptr,
        "relationship graph should keep package custom properties relationship");
    const auto* custom_content_type = output_reader.content_types().override_for(custom_part);
    check(custom_content_type != nullptr,
        "custom docprops rewrite should keep custom properties override registered");
}

void test_package_editor_document_properties_adds_missing_core_and_app_parts()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-missing-source.xlsx");
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-missing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    check(editor.manifest().find_part(core_part) == nullptr,
        "missing-docprops source should start without core properties");
    check(editor.manifest().find_part(app_part) == nullptr,
        "missing-docprops source should start without extended properties");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Generated Core";
    properties.application = "Generated App";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "missing-docprops rewrite should record generated core properties");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "missing core properties should be generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "missing-docprops rewrite should record generated extended properties");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "missing extended properties should be generated small XML");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "missing-docprops rewrite should audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "missing-docprops rewrite should audit package relationships rewrite");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "missing-docprops output should add core properties entry");
    check(entries.find("docProps/app.xml") != entries.end(),
        "missing-docprops output should add app properties entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Generated Core</dc:creator>",
        "missing-docprops output should generate core properties XML");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Generated App</Application>",
        "missing-docprops output should generate extended properties XML");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "missing-docprops output should add core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "missing-docprops output should add app content type override");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"xl/workbook.xml\"",
        "missing-docprops output should preserve officeDocument package relationship");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "missing-docprops output should add core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "missing-docprops output should add app package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "missing-docprops output should allocate core package relationship id");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "missing-docprops output should allocate app package relationship id");

    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "missing-docprops rewrite should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "missing-docprops rewrite should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing-docprops rewrite should preserve unknown bytes");
}

void test_package_editor_part_replacement_overrides_generated_document_properties()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-override-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-override-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Generated Creator";
    properties.application = "Generated App";
    editor.set_document_properties(properties);

    const std::string final_core =
        R"(<cp:coreProperties><dc:creator>Final Core</dc:creator></cp:coreProperties>)";
    const std::string final_app =
        R"(<Properties><Application>Final App</Application></Properties>)";
    editor.replace_part(core_part, final_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final core properties replacement");
    editor.replace_part(app_part, final_app,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "final extended properties replacement");

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops override should keep core properties in the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "docprops override should replace generated core write mode");
    check(core_plan->reason.find("final core properties replacement") != std::string::npos,
        "docprops override should keep final core replacement reason");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops override should keep app properties in the edit plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "docprops override should replace generated app write mode");
    check(app_plan->reason.find("final extended properties replacement") != std::string::npos,
        "docprops override should keep final app replacement reason");

    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops override should keep core properties in the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite
            && core_manifest_part->dirty && !core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops override should mark core properties as ordinary rewrite");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "docprops override should keep app properties in the manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite
            && app_manifest_part->dirty && !app_manifest_part->generated
            && !app_manifest_part->preserve_original,
        "docprops override should mark app properties as ordinary rewrite");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops override should preserve content types audit for added app part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops override should preserve package relationships audit for added app part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops override output should still add missing app entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == final_core,
        "docprops override output should write final core replacement bytes");
    check(output_reader.read_entry("docProps/app.xml") == final_app,
        "docprops override output should write final app replacement bytes");
    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/app.xml",
        "docprops override output should keep app content type override");
    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "docprops override output should keep app package relationship");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops override output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops override output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops override output should preserve unknown bytes");
}

void test_package_editor_document_properties_override_prior_part_replacement()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-helper-override-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-helper-override-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    const std::string prior_core =
        R"(<cp:coreProperties><dc:creator>Prior Core</dc:creator></cp:coreProperties>)";
    editor.replace_part(core_part, prior_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior ordinary core properties replacement");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Helper Creator";
    properties.title = "Helper Title";
    properties.application = "Helper App";
    properties.app_version = "7.1";
    editor.set_document_properties(properties);

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops helper override should keep core properties in the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper override should replace prior ordinary core write mode");
    check(core_plan->reason.find("core document properties generated small XML")
            != std::string::npos,
        "docprops helper override should keep helper core reason");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops helper override should add app properties to the edit plan");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper override should keep app write mode generated");

    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops helper override should keep core properties in the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->dirty && core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops helper override should mark core properties as generated");
    const auto* app_manifest_part = editor.manifest().find_part(app_part);
    check(app_manifest_part != nullptr,
        "docprops helper override should add app properties to the manifest");
    check(app_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && app_manifest_part->dirty && app_manifest_part->generated
            && !app_manifest_part->preserve_original,
        "docprops helper override should mark app properties as generated");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops helper override should audit content types for added app part");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops helper override should audit package relationships for added app part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops helper override output should still add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Helper Creator</dc:creator>",
        "docprops helper override output should write generated core properties");
    check_contains(core_xml, "<dc:title>Helper Title</dc:title>",
        "docprops helper override output should write generated core title");
    check_not_contains(core_xml, "Prior Core",
        "docprops helper override output should not write stale ordinary core bytes");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Helper App</Application>",
        "docprops helper override output should write generated app properties");
    check_contains(app_xml, "<AppVersion>7.1</AppVersion>",
        "docprops helper override output should write generated app version");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops helper override output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops helper override output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops helper override output should preserve unknown bytes");
}

void test_package_editor_document_properties_override_prior_part_removal()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-helper-removal-source.xlsx");
    const std::string core_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdCore1" Type="http://example.com/fastxlsx/core-note" Target="https://example.com/core-note" TargetMode="External"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/_rels/core.xml.rels", core_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-helper-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");

    editor.remove_part(core_part, "temporary core properties removal");
    check(editor.edit_plan().find_removed_part(core_part) != nullptr,
        "docprops helper removal setup should record removed core properties");
    check(editor.manifest().find_part(core_part) == nullptr,
        "docprops helper removal setup should remove core properties from the manifest");
    check(editor.edit_plan().find_removed_package_entry("docProps/_rels/core.xml.rels")
            != nullptr,
        "docprops helper removal setup should omit core owner relationships");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Restored Creator";
    properties.title = "Restored Title";
    properties.application = "Restored App";
    editor.set_document_properties(properties);

    check(editor.edit_plan().find_removed_part(core_part) == nullptr,
        "docprops helper should clear stale removed core properties audit");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr,
        "docprops helper should restore core properties to the edit plan");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper should restore removed core as generated small XML");
    const auto* app_plan = editor.edit_plan().find_part(app_part);
    check(app_plan != nullptr,
        "docprops helper should add extended properties after prior removal");
    check(app_plan->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "docprops helper should keep extended properties generated");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "docprops helper should restore core properties to the manifest");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && core_manifest_part->dirty && core_manifest_part->generated
            && !core_manifest_part->preserve_original,
        "docprops helper should mark restored core properties generated");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "docprops helper after removal should keep content types audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "docprops helper after removal should keep package relationships audit");
    const auto* removed_core_relationships =
        editor.edit_plan().find_removed_package_entry("docProps/_rels/core.xml.rels");
    check(removed_core_relationships != nullptr,
        "docprops helper should not restore prior removed core owner relationships");
    check(removed_core_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "docprops helper should keep removed core owner relationships as source-owned audit");
    check(removed_core_relationships->owner_part == core_part.value(),
        "docprops helper should keep removed core owner part in the audit");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "docprops helper after removal output should restore core properties entry");
    check(entries.find("docProps/app.xml") != entries.end(),
        "docprops helper after removal output should add app properties entry");
    check(entries.find("docProps/_rels/core.xml.rels") == entries.end(),
        "docprops helper after removal output should not restore removed core owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Restored Creator</dc:creator>",
        "docprops helper after removal output should write generated core XML");
    check_contains(core_xml, "<dc:title>Restored Title</dc:title>",
        "docprops helper after removal output should write generated title");
    check_not_contains(core_xml, "Original",
        "docprops helper after removal output should not keep stale core bytes");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>Restored App</Application>",
        "docprops helper after removal output should write generated app XML");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "docprops helper after removal output should restore core content type override");
    check_contains(content_types, "/docProps/app.xml",
        "docprops helper after removal output should add app content type override");
    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "docprops helper after removal output should keep core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "docprops helper after removal output should add app package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "docprops helper after removal output should keep core relationship readable");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "docprops helper after removal output should add app relationship readable");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "docprops helper after removal output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "docprops helper after removal output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "docprops helper after removal output should preserve unknown bytes");
}

void test_package_editor_document_properties_failure_preserves_state()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-conflict-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/wrong-core.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-conflict-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();

    bool failed = false;
    try {
        editor.set_document_properties({});
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "document properties rewrite should reject conflicting package relationships");
    check(editor.edit_plan().size() == initial_plan_size,
        "document properties failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "document properties failure should not change edit plan notes");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties failure should leave core properties copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "document properties failure should leave core manifest copy-original");
    check(editor.edit_plan().find_part(app_part) == nullptr,
        "document properties failure should not add app properties to the edit plan");
    check(editor.manifest().find_part(app_part) == nullptr,
        "document properties failure should not add app properties to the manifest");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") == entries.end(),
        "document properties failure output should not add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "document properties failure output should preserve core properties bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "document properties failure output should preserve package relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "document properties failure output should preserve content types bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "document properties failure output should preserve unknown bytes");
}

void test_package_editor_document_properties_app_relationship_failure_preserves_state()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-docprops-app-conflict-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/wrong-app.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-docprops-app-conflict-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();

    bool failed = false;
    try {
        editor.set_document_properties({});
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "document properties rewrite should reject conflicting extended package relationships");
    check(editor.edit_plan().size() == initial_plan_size,
        "extended relationship conflict should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "extended relationship conflict should not change edit plan notes");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "extended relationship conflict should leave core properties copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "extended relationship conflict should leave core manifest copy-original");
    check(editor.edit_plan().find_part(app_part) == nullptr,
        "extended relationship conflict should not add app properties to the edit plan");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "extended relationship conflict should not audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "extended relationship conflict should not audit package relationships rewrite");
    check(editor.manifest().find_part(app_part) == nullptr,
        "extended relationship conflict should not add app properties to the manifest");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/app.xml") == entries.end(),
        "extended relationship conflict output should not add app properties");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "extended relationship conflict output should preserve core properties bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "extended relationship conflict output should preserve package relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "extended relationship conflict output should preserve content types bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "extended relationship conflict output should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "extended relationship conflict output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "extended relationship conflict output should preserve unknown bytes");
}

void test_package_editor_combines_document_properties_and_worksheet_rewrite()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-combined-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-combined-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Patch Author";
    properties.last_modified_by = "Patch Reviewer";
    properties.title = "Combined patch";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.1";
    editor.set_document_properties(properties);

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>123</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "combined patch should keep core properties generated");
    check(editor.edit_plan().find_part(app_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "combined patch should keep app properties generated");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "combined patch should stream-rewrite worksheet");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "combined patch should local-DOM-rewrite workbook metadata");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "combined patch should record calcChain removal");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "combined patch should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("docProps/core.xml") != entries.end(),
        "combined patch should write core properties");
    check(entries.find("docProps/app.xml") != entries.end(),
        "combined patch should write app properties");
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "combined patch should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "combined patch should write replacement worksheet XML");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "combined patch should preserve unknown entry bytes");

    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Patch Author</dc:creator>",
        "combined patch should write core creator");
    check_contains(core_xml, "<dc:title>Combined patch</dc:title>",
        "combined patch should write core title");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "combined patch should write app properties");

    const std::string content_types = output_reader.read_entry("[Content_Types].xml");
    check_contains(content_types, "/docProps/core.xml",
        "combined patch should include core content type");
    check_contains(content_types, "/docProps/app.xml",
        "combined patch should include app content type");
    check_not_contains(content_types, "calcChain+xml",
        "combined patch content types should remove calcChain");
    check(output_reader.content_types().override_for(core_part) != nullptr,
        "combined patch parsed content types should include core properties");
    check(output_reader.content_types().override_for(app_part) != nullptr,
        "combined patch parsed content types should include app properties");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "combined patch parsed content types should omit calcChain");

    const std::string package_relationships = output_reader.read_entry("_rels/.rels");
    check_contains(package_relationships, "Target=\"docProps/core.xml\"",
        "combined patch should include core package relationship");
    check_contains(package_relationships, "Target=\"docProps/app.xml\"",
        "combined patch should include app package relationship");
    check(output_reader.package_relationships().find_by_id("rId1") != nullptr,
        "combined patch should keep office document package relationship");
    check(output_reader.package_relationships().find_by_id("rId2") != nullptr,
        "combined patch should add core package relationship id");
    check(output_reader.package_relationships().find_by_id("rId3") != nullptr,
        "combined patch should add app package relationship id");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/worksheet",
        "combined patch should keep worksheet workbook relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "combined patch should remove calcChain workbook relationship");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "combined patch should request full calculation on load");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "core-docprops")) {
            test_package_editor_repeated_part_replacement_updates_final_state();
            test_package_editor_replacement_audits_preserved_root_relationships();
            test_package_editor_sets_document_properties_and_adds_missing_metadata_parts();
            test_package_editor_document_properties_preserves_custom_properties_part();
            test_package_editor_document_properties_adds_missing_core_and_app_parts();
            test_package_editor_part_replacement_overrides_generated_document_properties();
            test_package_editor_document_properties_override_prior_part_replacement();
            test_package_editor_document_properties_override_prior_part_removal();
            test_package_editor_document_properties_failure_preserves_state();
            test_package_editor_document_properties_app_relationship_failure_preserves_state();
            test_package_editor_combines_document_properties_and_worksheet_rewrite();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
