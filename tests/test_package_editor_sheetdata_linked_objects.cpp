#include "test_package_editor_sheetdata_linked_common.hpp"

void test_package_editor_sheet_data_patch_preserves_worksheet_owned_object_parts()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-objects-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-objects-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "OLE sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "OLE sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "OLE sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "OLE sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "OLE sheetData replacement should audit preserved OLE object metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "control sheetData replacement should audit preserved control metadata");

    const auto* ole_plan = editor.edit_plan().find_part(ole_part);
    check(ole_plan != nullptr,
        "worksheet-owned OLE part should remain visible in the edit plan");
    check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned OLE part should remain copy-original");
    check(ole_plan->reason.find("worksheet relationship rIdOle") != std::string::npos
            && ole_plan->reason.find("relationships/oleObject") != std::string::npos
            && ole_plan->reason.find("/xl/embeddings/oleObject1.bin") != std::string::npos,
        "worksheet-owned OLE copy reason should come from worksheet relationship traversal");
    check(ole_plan->relationship_owner_part == worksheet_part.value(),
        "worksheet-owned OLE audit should keep structured relationship owner");
    check(ole_plan->relationship_id == "rIdOle",
        "worksheet-owned OLE audit should keep structured relationship id");
    check(ole_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
        "worksheet-owned OLE audit should keep structured relationship type");
    check(ole_plan->relationship_target == "../embeddings/oleObject1.bin",
        "worksheet-owned OLE audit should keep structured relationship target");
    const auto* control_plan = editor.edit_plan().find_part(control_part);
    check(control_plan != nullptr,
        "worksheet-owned control part should remain visible in the edit plan");
    check(control_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned control part should remain copy-original");
    check(control_plan->reason.find("worksheet relationship rIdControl") != std::string::npos
            && control_plan->reason.find("relationships/control") != std::string::npos
            && control_plan->reason.find("/xl/ctrlProps/control1.xml") != std::string::npos,
        "worksheet-owned control copy reason should come from worksheet relationship traversal");
    check(control_plan->relationship_owner_part == worksheet_part.value(),
        "worksheet-owned control audit should keep structured relationship owner");
    check(control_plan->relationship_id == "rIdControl",
        "worksheet-owned control audit should keep structured relationship id");
    check(control_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
        "worksheet-owned control audit should keep structured relationship type");
    check(control_plan->relationship_target == "../ctrlProps/control1.xml",
        "worksheet-owned control audit should keep structured relationship target");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "OLE/control sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "OLE/control sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "OLE/control sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "OLE/control sheetData output plan should not invent relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "OLE/control sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "OLE/control sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "OLE/control sheetData output plan should snapshot preserved OLE notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "OLE/control sheetData output plan should snapshot preserved control notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "OLE/control sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "OLE/control sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "OLE/control sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "OLE/control sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "OLE/control sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "OLE/control sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE/control sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "OLE/control sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE sheetData output plan should preserve worksheet-owned OLE bytes");
    check_output_entry_part_context(output_plan.entries, "xl/embeddings/oleObject1.bin",
        true, ole_part.value(),
        "OLE sheetData output plan should classify OLE as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/embeddings/oleObject1.bin", worksheet_part.value(), "rIdOle",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
        "../embeddings/oleObject1.bin",
        "OLE sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "control sheetData output plan should preserve worksheet-owned control properties");
    check_output_entry_part_context(output_plan.entries, "xl/ctrlProps/control1.xml",
        true, control_part.value(),
        "control sheetData output plan should classify control properties as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/ctrlProps/control1.xml", worksheet_part.value(), "rIdControl",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
        "../ctrlProps/control1.xml",
        "control sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "OLE sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "OLE/control sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "OLE/control sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "OLE/control sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "OLE/control sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
          R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "OLE sheetData replacement should preserve OLE metadata around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "OLE sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
        "OLE sheetData replacement should byte-preserve worksheet-owned OLE payload");
    check(output_reader.read_entry("xl/ctrlProps/control1.xml") == control_properties,
        "control sheetData replacement should byte-preserve worksheet-owned control properties");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "OLE sheetData replacement should keep worksheet relationships readable");
    const auto* output_ole_relationship = output_relationships->find_by_id("rIdOle");
    check(output_ole_relationship != nullptr,
        "OLE sheetData replacement should keep OLE relationship readable");
    check(output_ole_relationship->target == "../embeddings/oleObject1.bin",
        "OLE sheetData replacement should preserve OLE relationship target");
    check(output_ole_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "OLE sheetData replacement should keep OLE relationship internal");
    const auto* output_control_relationship = output_relationships->find_by_id("rIdControl");
    check(output_control_relationship != nullptr,
        "control sheetData replacement should keep control relationship readable");
    check(output_control_relationship->target == "../ctrlProps/control1.xml",
        "control sheetData replacement should preserve control relationship target");
    check(output_control_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "control sheetData replacement should keep control relationship internal");
    check(output_reader.content_types().override_for(ole_part) != nullptr,
        "OLE sheetData replacement should preserve OLE content type override");
    check(output_reader.content_types().override_for(control_part) != nullptr,
        "control sheetData replacement should preserve control content type override");
}

void test_package_editor_removes_worksheet_owned_object_parts_with_inbound_audit()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-remove-worksheet-objects-source.xlsx");
    const std::filesystem::path ole_output =
        output_path("fastxlsx-package-editor-remove-worksheet-ole-output.xlsx");
    const std::filesystem::path control_output =
        output_path("fastxlsx-package-editor-remove-worksheet-control-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(ole_part, "explicit worksheet-owned OLE object removal");

        check(editor.edit_plan().find_part(ole_part) == nullptr,
            "worksheet-owned OLE removal should clear the active edit-plan part");
        const auto* removed_ole = editor.edit_plan().find_removed_part(ole_part);
        check(removed_ole != nullptr,
            "worksheet-owned OLE removal should record removed-part audit");
        check(removed_ole->reason.find("OLE object") != std::string::npos,
            "worksheet-owned OLE removal should retain the removal reason");
        check(removed_ole->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned OLE removal should audit preserved inbound relationships");
        check(removed_ole->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include owner part");
        check(removed_ole->reason.find("rIdOle") != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include relationship id");
        check(removed_ole->reason.find("../embeddings/oleObject1.bin")
                != std::string::npos,
            "worksheet-owned OLE removal inbound audit should include original target");
        check(removed_ole->inbound_relationships.size() == 1,
            "worksheet-owned OLE removal should keep structured inbound audit");
        const auto& ole_inbound = removed_ole->inbound_relationships.front();
        check(ole_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned OLE removal should keep inbound owner part");
        check(ole_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned OLE removal should keep inbound owner relationship entry");
        check(ole_inbound.relationship_id == "rIdOle",
            "worksheet-owned OLE removal should keep inbound relationship id");
        check(ole_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "worksheet-owned OLE removal should keep inbound relationship type");
        check(ole_inbound.relationship_target == "../embeddings/oleObject1.bin",
            "worksheet-owned OLE removal should keep inbound raw target");
        check(ole_inbound.target_part == ole_part,
            "worksheet-owned OLE removal should keep normalized target part");
        check(editor.manifest().find_part(ole_part) == nullptr,
            "worksheet-owned OLE removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE removal should remove the OLE content type override");
        check(editor.manifest().content_types().override_for(control_part) != nullptr,
            "worksheet-owned OLE removal should keep control content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned OLE removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned OLE removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned OLE removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned OLE removal output plan should omit OLE part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/embeddings/oleObject1.bin", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdOle",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject",
            "../embeddings/oleObject1.bin", ole_part,
            "worksheet-owned OLE removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned OLE removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE removal output plan should preserve control properties");

        editor.save_as(ole_output);

        const auto entries = fastxlsx::test::read_zip_entries(ole_output);
        check(entries.find("xl/embeddings/oleObject1.bin") == entries.end(),
            "worksheet-owned OLE removal output should omit OLE part");
        check(entries.find("xl/embeddings/_rels/oleObject1.bin.rels") == entries.end(),
            "worksheet-owned OLE removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(ole_output);
        check(output_reader.content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE removal output should remove OLE content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/embeddings/oleObject1.bin",
            "worksheet-owned OLE removal content types XML should omit OLE override");
        check(output_reader.content_types().override_for(control_part) != nullptr,
            "worksheet-owned OLE removal output should keep control content type override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "worksheet-owned OLE removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "worksheet-owned OLE removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "worksheet-owned OLE removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "worksheet-owned OLE removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned OLE removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml")
                == control_properties,
            "worksheet-owned OLE removal should preserve control properties bytes");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "worksheet-owned OLE removal should not create owner relationships for absent OLE");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned OLE removal should keep worksheet relationships readable");
        const auto* ole_link = output_relationships->find_by_id("rIdOle");
        check(ole_link != nullptr,
            "worksheet-owned OLE removal should keep inbound OLE relationship id");
        check(ole_link->target == "../embeddings/oleObject1.bin",
            "worksheet-owned OLE removal should not rewrite inbound OLE target");
        check(ole_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "worksheet-owned OLE removal should keep inbound OLE target mode");
        check(output_relationships->find_by_id("rIdControl") != nullptr,
            "worksheet-owned OLE removal should keep control relationship");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(control_part, "explicit worksheet-owned control property removal");

        check(editor.edit_plan().find_part(control_part) == nullptr,
            "worksheet-owned control removal should clear the active edit-plan part");
        const auto* removed_control =
            editor.edit_plan().find_removed_part(control_part);
        check(removed_control != nullptr,
            "worksheet-owned control removal should record removed-part audit");
        check(removed_control->reason.find("control property") != std::string::npos,
            "worksheet-owned control removal should retain the removal reason");
        check(removed_control->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned control removal should audit preserved inbound relationships");
        check(removed_control->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "worksheet-owned control removal inbound audit should include owner part");
        check(removed_control->reason.find("rIdControl") != std::string::npos,
            "worksheet-owned control removal inbound audit should include relationship id");
        check(removed_control->reason.find("../ctrlProps/control1.xml")
                != std::string::npos,
            "worksheet-owned control removal inbound audit should include original target");
        check(removed_control->inbound_relationships.size() == 1,
            "worksheet-owned control removal should keep structured inbound audit");
        const auto& control_inbound = removed_control->inbound_relationships.front();
        check(control_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned control removal should keep inbound owner part");
        check(control_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned control removal should keep inbound owner relationship entry");
        check(control_inbound.relationship_id == "rIdControl",
            "worksheet-owned control removal should keep inbound relationship id");
        check(control_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "worksheet-owned control removal should keep inbound relationship type");
        check(control_inbound.relationship_target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal should keep inbound raw target");
        check(control_inbound.target_part == control_part,
            "worksheet-owned control removal should keep normalized target part");
        check(editor.manifest().find_part(control_part) == nullptr,
            "worksheet-owned control removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal should remove the control content type override");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal should keep OLE content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned control removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned control removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned control removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned control removal output plan should omit control properties");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/ctrlProps/control1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdControl",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "../ctrlProps/control1.xml", control_part,
            "worksheet-owned control removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned control removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal output plan should preserve OLE part");

        editor.save_as(control_output);

        const auto entries = fastxlsx::test::read_zip_entries(control_output);
        check(entries.find("xl/ctrlProps/control1.xml") == entries.end(),
            "worksheet-owned control removal output should omit control properties");
        check(entries.find("xl/ctrlProps/_rels/control1.xml.rels") == entries.end(),
            "worksheet-owned control removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(control_output);
        check(output_reader.content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal output should remove control content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/ctrlProps/control1.xml",
            "worksheet-owned control removal content types XML should omit control override");
        check(output_reader.content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal output should keep OLE content type override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "worksheet-owned control removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "worksheet-owned control removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "worksheet-owned control removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "worksheet-owned control removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned control removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "worksheet-owned control removal should preserve OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "worksheet-owned control removal should not create owner relationships for absent control");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned control removal should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "worksheet-owned control removal should keep inbound control relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal should not rewrite inbound control target");
        check(control_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "worksheet-owned control removal should keep inbound control target mode");
        check(output_relationships->find_by_id("rIdOle") != nullptr,
            "worksheet-owned control removal should keep OLE relationship");
    }
}

void test_package_editor_worksheet_owned_object_part_same_path_ordering()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-worksheet-object-order-source.xlsx");
    const std::filesystem::path ole_restore_output =
        output_path("fastxlsx-package-editor-replace-after-remove-worksheet-ole-output.xlsx");
    const std::filesystem::path control_remove_output =
        output_path("fastxlsx-package-editor-remove-after-replace-worksheet-control-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/embeddings/oleObject1.bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>)"
        R"(<Override PartName="/xl/ctrlProps/control1.xml" ContentType="application/vnd.ms-excel.controlproperties+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rIdOle"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rIdControl"/></controls>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOle" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject" Target="../embeddings/oleObject1.bin"/>)"
        R"(<Relationship Id="rIdControl" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/control" Target="../ctrlProps/control1.xml"/>)"
        R"(</Relationships>)";
    const std::string ole_object = std::string("OLE\0OBJECT\0opaque", 17);
    const std::string control_properties =
        R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="1"/>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/embeddings/oleObject1.bin", ole_object},
            {"xl/ctrlProps/control1.xml", control_properties},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName ole_part("/xl/embeddings/oleObject1.bin");
    const fastxlsx::detail::PartName control_part("/xl/ctrlProps/control1.xml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(ole_part, "temporary worksheet-owned OLE removal");
        check(editor.edit_plan().find_removed_part(ole_part) != nullptr,
            "worksheet-owned OLE restore setup should record removed-part audit");
        check(editor.manifest().find_part(ole_part) == nullptr,
            "worksheet-owned OLE restore setup should remove the manifest part");
        check(editor.manifest().content_types().override_for(ole_part) == nullptr,
            "worksheet-owned OLE restore setup should remove the content type override");

        const std::string restored_ole = std::string("RESTORED\0OLE", 12);
        replace_part_with_memory_chunks(editor, ole_part, restored_ole,
            "restored worksheet-owned OLE after removal");

        check(editor.edit_plan().find_removed_part(ole_part) == nullptr,
            "worksheet-owned OLE replacement after removal should clear stale removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE replacement after removal should not invent owner relationships omission");
        check(editor.edit_plan().removed_parts().empty(),
            "worksheet-owned OLE replacement after removal should leave no removed parts");
        const auto* ole_plan = editor.edit_plan().find_part(ole_part);
        check(ole_plan != nullptr,
            "worksheet-owned OLE replacement after removal should restore active edit-plan part");
        check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned OLE replacement after removal should keep final write mode");
        check(ole_plan->reason.find("after removal") != std::string::npos,
            "worksheet-owned OLE replacement after removal should keep final reason");
        check_manifest_write_mode(editor, ole_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned OLE replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned OLE replacement after removal should restore the OLE content type override");
        const auto* restored_content_types =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(restored_content_types != nullptr,
            "worksheet-owned OLE replacement after removal should keep content types audit");
        check(restored_content_types->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "worksheet-owned OLE replacement after removal should restore content types copy-original audit");
        check(restored_content_types->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE replacement after removal should keep content types audit role");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "worksheet-owned OLE replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "worksheet-owned OLE replacement after removal output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned OLE replacement after removal output plan should not invent relationship audits");
        check(output_plan.removed_parts.empty(),
            "worksheet-owned OLE replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "worksheet-owned OLE replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "worksheet-owned OLE replacement after removal output plan should rewrite OLE part");
        check_output_entry_part_context(output_plan.entries,
            "xl/embeddings/oleObject1.bin", true, ole_part.value(),
            "worksheet-owned OLE replacement after removal output plan should classify OLE part");
        const auto* output_ole_plan =
            find_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin");
        check(output_ole_plan->reason.find("after removal") != std::string::npos,
            "worksheet-owned OLE replacement after removal output plan should keep reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "worksheet-owned OLE replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned OLE replacement after removal output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned OLE replacement after removal output plan should preserve sibling control properties");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "worksheet-owned OLE replacement after removal output plan should not invent owner relationships");

        editor.save_as(ole_restore_output);

        const auto entries = fastxlsx::test::read_zip_entries(ole_restore_output);
        check(entries.find("xl/embeddings/oleObject1.bin") != entries.end(),
            "worksheet-owned OLE replacement after removal output should restore OLE part");
        check(entries.find("xl/embeddings/_rels/oleObject1.bin.rels") == entries.end(),
            "worksheet-owned OLE replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(ole_restore_output);
        check_preserved_source_entries(editor.reader(), output_reader, ole_part.zip_path());
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == restored_ole,
            "worksheet-owned OLE replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "worksheet-owned OLE replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned OLE replacement after removal should not prune worksheet relationships");
        check(output_reader.content_types().override_for(ole_part) != nullptr,
            "worksheet-owned OLE replacement after removal should restore OLE content type override");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "worksheet-owned OLE replacement after removal should not create owner relationships");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string replacement_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="2"/>)";
        replace_part_with_memory_chunks(editor, control_part, replacement_control,
            "queued worksheet-owned control replacement");
        const auto* prior_control_plan = editor.edit_plan().find_part(control_part);
        check(prior_control_plan != nullptr,
            "worksheet-owned control removal-after-replacement setup should queue replacement");
        check(prior_control_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "worksheet-owned control removal-after-replacement setup should local-DOM-rewrite control");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "worksheet-owned control replacement setup should not rewrite content types");

        editor.remove_part(control_part,
            "final worksheet-owned control removal after replacement");

        check(editor.edit_plan().find_part(control_part) == nullptr,
            "worksheet-owned control removal after replacement should clear active replacement");
        const auto* removed_control =
            editor.edit_plan().find_removed_part(control_part);
        check(removed_control != nullptr,
            "worksheet-owned control removal after replacement should record removed-part audit");
        check(removed_control->reason.find("after replacement") != std::string::npos,
            "worksheet-owned control removal after replacement should keep final reason");
        check(removed_control->reason.find("inbound relationship preserved")
                != std::string::npos,
            "worksheet-owned control removal after replacement should audit preserved inbound relationship");
        check(removed_control->inbound_relationships.size() == 1,
            "worksheet-owned control removal after replacement should keep structured inbound audit");
        const auto& control_inbound = removed_control->inbound_relationships.front();
        check(control_inbound.owner_part == worksheet_part.value(),
            "worksheet-owned control removal after replacement should keep inbound owner part");
        check(control_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "worksheet-owned control removal after replacement should keep inbound owner entry");
        check(control_inbound.relationship_id == "rIdControl",
            "worksheet-owned control removal after replacement should keep inbound relationship id");
        check(control_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "worksheet-owned control removal after replacement should keep inbound relationship type");
        check(control_inbound.relationship_target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement should keep inbound raw target");
        check(control_inbound.target_part == control_part,
            "worksheet-owned control removal after replacement should keep normalized inbound target");
        check(editor.manifest().find_part(control_part) == nullptr,
            "worksheet-owned control removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement should remove content type override");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "worksheet-owned control removal after replacement should keep sibling OLE content type");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "worksheet-owned control removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "worksheet-owned control removal after replacement content types audit should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal after replacement content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal after replacement should not invent owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "worksheet-owned control removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "worksheet-owned control removal after replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "worksheet-owned control removal after replacement output plan should not invent target audits");
        check(output_plan.removed_parts.size() == 1,
            "worksheet-owned control removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == control_part,
            "worksheet-owned control removal after replacement output plan should expose removed control part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "worksheet-owned control removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "worksheet-owned control removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "worksheet-owned control removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "worksheet-owned control removal after replacement output plan should omit control part");
        check_output_entry_part_context(output_plan.entries, "xl/ctrlProps/control1.xml",
            true, control_part.value(),
            "worksheet-owned control removal after replacement output plan should classify omitted control part");
        const auto* output_control_plan =
            find_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml");
        check(output_control_plan->reason.find("after replacement") != std::string::npos,
            "worksheet-owned control removal after replacement output plan should keep final removal reason");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/ctrlProps/control1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdControl",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/control",
            "../ctrlProps/control1.xml", control_part,
            "worksheet-owned control removal after replacement output plan should keep inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "worksheet-owned control removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "worksheet-owned control removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "worksheet-owned control removal after replacement output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "worksheet-owned control removal after replacement output plan should preserve sibling OLE part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "worksheet-owned control removal after replacement output plan should not invent owner relationships");

        editor.save_as(control_remove_output);

        const auto entries = fastxlsx::test::read_zip_entries(control_remove_output);
        check(entries.find("xl/ctrlProps/control1.xml") == entries.end(),
            "worksheet-owned control removal after replacement output should omit control part");
        check(entries.find("xl/ctrlProps/_rels/control1.xml.rels") == entries.end(),
            "worksheet-owned control removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(control_remove_output);
        check(output_reader.content_types().override_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement output should remove control content type override");
        check_not_contains(output_reader.read_entry("[Content_Types].xml"),
            "/xl/ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement content types should omit control override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "worksheet-owned control removal after replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "worksheet-owned control removal after replacement should preserve sibling OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "worksheet-owned control removal after replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "worksheet-owned control removal after replacement should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "worksheet-owned control removal after replacement should keep inbound relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "worksheet-owned control removal after replacement should not rewrite inbound target");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-worksheet-ole-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_ole = std::string("STALE\0OLE", 9);
        const std::string final_ole = std::string("FINAL\0OLE", 9);
        replace_part_with_memory_chunks(editor, ole_part, stale_ole,
            "stale repeated worksheet-owned OLE replacement");
        replace_part_with_memory_chunks(editor, ole_part, final_ole,
            "final repeated worksheet-owned OLE replacement");

        const auto* ole_plan = editor.edit_plan().find_part(ole_part);
        check(ole_plan != nullptr,
            "repeated worksheet-owned OLE replacement should keep an active edit-plan part");
        check(ole_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned OLE replacement should keep final write mode");
        check(ole_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned OLE replacement should keep final reason");
        check(ole_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned OLE replacement should drop stale reason");
        check_manifest_write_mode(editor, ole_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned OLE replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().override_for(ole_part) != nullptr,
            "repeated worksheet-owned OLE replacement should keep OLE content type override");
        check(editor.edit_plan().find_removed_part(ole_part) == nullptr,
            "repeated worksheet-owned OLE replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated worksheet-owned OLE replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated worksheet-owned OLE replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(control_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated worksheet-owned OLE replacement should keep sibling control copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated worksheet-owned OLE replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated worksheet-owned OLE replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated worksheet-owned OLE replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated worksheet-owned OLE replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated worksheet-owned OLE replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated worksheet-owned OLE replacement output plan should rewrite OLE part");
        const auto* output_ole_plan =
            find_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin");
        check(output_ole_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned OLE replacement output plan should keep final reason");
        check(output_ole_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned OLE replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned OLE replacement output plan should preserve sibling control");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/embeddings/_rels/oleObject1.bin.rels") == nullptr,
            "repeated worksheet-owned OLE replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader, ole_part.zip_path());
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == final_ole,
            "repeated worksheet-owned OLE replacement should write final bytes");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") != stale_ole,
            "repeated worksheet-owned OLE replacement should not write stale bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated worksheet-owned OLE replacement should preserve content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated worksheet-owned OLE replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") == control_properties,
            "repeated worksheet-owned OLE replacement should preserve sibling control bytes");
        check(output_reader.relationships_for(ole_part) == nullptr,
            "repeated worksheet-owned OLE replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated worksheet-owned OLE replacement should keep worksheet relationships readable");
        const auto* ole_link = output_relationships->find_by_id("rIdOle");
        check(ole_link != nullptr,
            "repeated worksheet-owned OLE replacement should keep inbound relationship id");
        check(ole_link->target == "../embeddings/oleObject1.bin",
            "repeated worksheet-owned OLE replacement should not rewrite inbound target");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-worksheet-control-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="2"/>)";
        const std::string final_control =
            R"(<controlPr xmlns="http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" shapeId="3"/>)";
        replace_part_with_memory_chunks(editor, control_part, stale_control,
            "stale repeated worksheet-owned control replacement");
        replace_part_with_memory_chunks(editor, control_part, final_control,
            "final repeated worksheet-owned control replacement");

        const auto* control_plan = editor.edit_plan().find_part(control_part);
        check(control_plan != nullptr,
            "repeated worksheet-owned control replacement should keep an active edit-plan part");
        check(control_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned control replacement should keep final write mode");
        check(control_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned control replacement should keep final reason");
        check(control_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned control replacement should drop stale reason");
        check_manifest_write_mode(editor, control_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated worksheet-owned control replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().override_for(control_part) != nullptr,
            "repeated worksheet-owned control replacement should keep control content type override");
        check(editor.edit_plan().find_removed_part(control_part) == nullptr,
            "repeated worksheet-owned control replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated worksheet-owned control replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated worksheet-owned control replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(ole_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated worksheet-owned control replacement should keep sibling OLE copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated worksheet-owned control replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated worksheet-owned control replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated worksheet-owned control replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated worksheet-owned control replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated worksheet-owned control replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated worksheet-owned control replacement output plan should rewrite control part");
        const auto* output_control_plan =
            find_output_entry_plan(output_plan.entries, "xl/ctrlProps/control1.xml");
        check(output_control_plan->reason.find("final repeated") != std::string::npos,
            "repeated worksheet-owned control replacement output plan should keep final reason");
        check(output_control_plan->reason.find("stale repeated") == std::string::npos,
            "repeated worksheet-owned control replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/embeddings/oleObject1.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated worksheet-owned control replacement output plan should preserve sibling OLE");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/ctrlProps/_rels/control1.xml.rels") == nullptr,
            "repeated worksheet-owned control replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader,
            control_part.zip_path());
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") == final_control,
            "repeated worksheet-owned control replacement should write final XML");
        check(output_reader.read_entry("xl/ctrlProps/control1.xml") != stale_control,
            "repeated worksheet-owned control replacement should not write stale XML");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated worksheet-owned control replacement should preserve content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated worksheet-owned control replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/embeddings/oleObject1.bin") == ole_object,
            "repeated worksheet-owned control replacement should preserve sibling OLE bytes");
        check(output_reader.relationships_for(control_part) == nullptr,
            "repeated worksheet-owned control replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated worksheet-owned control replacement should keep worksheet relationships readable");
        const auto* control_link = output_relationships->find_by_id("rIdControl");
        check(control_link != nullptr,
            "repeated worksheet-owned control replacement should keep inbound relationship id");
        check(control_link->target == "../ctrlProps/control1.xml",
            "repeated worksheet-owned control replacement should not rewrite inbound target");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-linked-object-parts")) {
            test_package_editor_sheet_data_patch_preserves_worksheet_owned_object_parts();
            test_package_editor_removes_worksheet_owned_object_parts_with_inbound_audit();
            test_package_editor_worksheet_owned_object_part_same_path_ordering();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
