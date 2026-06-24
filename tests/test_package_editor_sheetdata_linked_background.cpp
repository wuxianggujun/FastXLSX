#include "test_package_editor_sheetdata_linked_common.hpp"

void test_package_editor_sheet_data_patch_preserves_background_picture_and_header_footer_vml()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-picture-vml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-picture-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
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
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "picture sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "picture sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "picture sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "picture sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "picture sheetData replacement should audit preserved background picture metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "picture sheetData replacement should audit preserved header/footer drawing metadata");

    const auto* picture_plan = editor.edit_plan().find_part(background_picture_part);
    check(picture_plan != nullptr,
        "worksheet-owned background picture part should remain visible in the edit plan");
    check(picture_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned background picture part should remain copy-original");
    check(picture_plan->reason.find("worksheet relationship rIdPicture")
            != std::string::npos
            && picture_plan->reason.find("relationships/image") != std::string::npos
            && picture_plan->reason.find("/xl/media/background.png")
                != std::string::npos,
        "background picture copy reason should come from worksheet relationship traversal");
    check(picture_plan->relationship_owner_part == worksheet_part.value(),
        "background picture audit should keep structured relationship owner");
    check(picture_plan->relationship_id == "rIdPicture",
        "background picture audit should keep structured relationship id");
    check(picture_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "background picture audit should keep structured relationship type");
    check(picture_plan->relationship_target == "../media/background.png",
        "background picture audit should keep structured relationship target");

    const auto* header_footer_plan = editor.edit_plan().find_part(header_footer_vml_part);
    check(header_footer_plan != nullptr,
        "worksheet-owned header/footer VML part should remain visible in the edit plan");
    check(header_footer_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned header/footer VML part should remain copy-original");
    check(header_footer_plan->reason.find("worksheet relationship rIdHeaderFooter")
            != std::string::npos
            && header_footer_plan->reason.find("relationships/vmlDrawing")
                != std::string::npos
            && header_footer_plan->reason.find("/xl/drawings/vmlDrawingHF1.vml")
                != std::string::npos,
        "header/footer VML copy reason should come from worksheet relationship traversal");
    check(header_footer_plan->relationship_owner_part == worksheet_part.value(),
        "header/footer VML audit should keep structured relationship owner");
    check(header_footer_plan->relationship_id == "rIdHeaderFooter",
        "header/footer VML audit should keep structured relationship id");
    check(header_footer_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "header/footer VML audit should keep structured relationship type");
    check(header_footer_plan->relationship_target == "../drawings/vmlDrawingHF1.vml",
        "header/footer VML audit should keep structured relationship target");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "picture/VML sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "picture/VML sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "picture/VML sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "picture/VML sheetData output plan should not invent relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "picture/VML sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "picture/VML sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "picture/VML sheetData output plan should snapshot preserved picture notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "picture/VML sheetData output plan should snapshot preserved header/footer VML notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "picture/VML sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "picture/VML sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "picture/VML sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "picture/VML sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "picture/VML sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "picture/VML sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture/VML sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "picture/VML sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/media/background.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet-owned background picture");
    check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
        true, background_picture_part.value(),
        "picture sheetData output plan should classify background picture as a part");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/background.png",
        worksheet_part.value(), "rIdPicture",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/background.png",
        "picture sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet-owned header/footer VML");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
        true, header_footer_vml_part.value(),
        "picture sheetData output plan should classify header/footer VML as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(), "rIdHeaderFooter",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "../drawings/vmlDrawingHF1.vml",
        "picture sheetData output plan should keep header/footer VML relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "picture sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "picture/VML sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "picture/VML sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "picture/VML sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "picture/VML sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
          R"(<picture r:id="rIdPicture"/>)"
          R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "picture sheetData replacement should preserve picture metadata around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "picture sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/media/background.png") == background_picture,
        "picture sheetData replacement should byte-preserve background picture");
    check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml") == header_footer_vml,
        "picture sheetData replacement should byte-preserve header/footer VML drawing");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "picture sheetData replacement should keep worksheet relationships readable");
    const auto* output_picture_relationship =
        output_relationships->find_by_id("rIdPicture");
    check(output_picture_relationship != nullptr,
        "picture sheetData replacement should keep background picture relationship readable");
    check(output_picture_relationship->target == "../media/background.png",
        "picture sheetData replacement should preserve background picture target");
    check(output_picture_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "picture sheetData replacement should keep background picture relationship internal");
    const auto* output_header_footer_relationship =
        output_relationships->find_by_id("rIdHeaderFooter");
    check(output_header_footer_relationship != nullptr,
        "picture sheetData replacement should keep header/footer VML relationship readable");
    check(output_header_footer_relationship->target == "../drawings/vmlDrawingHF1.vml",
        "picture sheetData replacement should preserve header/footer VML target");
    check(output_header_footer_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "picture sheetData replacement should keep header/footer VML relationship internal");
    check(output_reader.content_types().default_for("png") != nullptr,
        "picture sheetData replacement should preserve PNG content type default");
    check(output_reader.content_types().override_for(background_picture_part) == nullptr,
        "picture sheetData replacement should not promote background picture default");
    check(output_reader.content_types().override_for(header_footer_vml_part) != nullptr,
        "picture sheetData replacement should preserve header/footer VML content type override");
}

void test_package_editor_sheet_data_patch_preserves_page_setup_printer_settings()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-sheetdata-printer-settings-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-printer-settings-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/printerSettings/printerSettings1.bin" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.printerSettings"/>)"
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
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup paperSize="9" orientation="landscape" r:id="rIdPrinter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPrinter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings" Target="../printerSettings/printerSettings1.bin"/>)"
        R"(</Relationships>)";
    const std::string printer_settings =
        std::string("opaque-printer-settings\0bytes", 30);

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/printerSettings/printerSettings1.bin", printer_settings},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName printer_settings_part(
        "/xl/printerSettings/printerSettings1.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "printer settings sheetData replacement should keep worksheet in edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "printer settings sheetData replacement should local-DOM-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "printer settings sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "printer settings sheetData replacement should local-DOM-rewrite workbook calc metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "printer settings sheetData replacement should audit preserved page setup metadata");

    const auto* printer_settings_plan =
        editor.edit_plan().find_part(printer_settings_part);
    check(printer_settings_plan != nullptr,
        "worksheet-owned printer settings part should remain visible in the edit plan");
    check(printer_settings_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet-owned printer settings part should remain copy-original");
    check(printer_settings_plan->reason.find("worksheet relationship rIdPrinter")
            != std::string::npos
            && printer_settings_plan->reason.find("relationships/printerSettings")
                != std::string::npos
            && printer_settings_plan->reason.find(
                   "/xl/printerSettings/printerSettings1.bin")
                != std::string::npos,
        "printer settings copy reason should come from worksheet relationship traversal");
    check(printer_settings_plan->relationship_owner_part == worksheet_part.value(),
        "printer settings audit should keep structured relationship owner");
    check(printer_settings_plan->relationship_id == "rIdPrinter",
        "printer settings audit should keep structured relationship id");
    check(printer_settings_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "printer settings audit should keep structured relationship type");
    check(printer_settings_plan->relationship_target
            == "../printerSettings/printerSettings1.bin",
        "printer settings audit should keep structured relationship target");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "printer settings sheetData replacement should not flag a valid preserved pageSetup relationship");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan =
        editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "printer settings sheetData output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "printer settings sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "printer settings sheetData output plan should not invent dependency audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "printer settings output plan should not invent pageSetup relationship-id audits");
    check(output_plan.removed_parts.empty(),
        "printer settings sheetData output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "printer settings sheetData output plan should not expose removed package entries");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "printer settings sheetData output plan should snapshot preserved page setup notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "printer settings sheetData output plan should rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "printer settings sheetData output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "printer settings sheetData output plan should rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml",
        true, workbook_part.value(),
        "printer settings sheetData output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "printer settings sheetData output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "printer settings sheetData output plan should classify package relationships as metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "printer settings sheetData output plan should classify workbook relationships as metadata");
    check_output_entry_plan(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve printer settings part");
    check_output_entry_part_context(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin", true, printer_settings_part.value(),
        "printer settings sheetData output plan should classify printer settings as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/printerSettings/printerSettings1.bin", worksheet_part.value(),
        "rIdPrinter",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "../printerSettings/printerSettings1.bin",
        "printer settings sheetData output plan should keep worksheet relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "printer settings sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "printer settings sheetData output plan should classify worksheet relationships as metadata");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "printer settings sheetData output plan should classify worksheet relationships metadata");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "printer settings sheetData output plan should keep worksheet relationships owner context");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "printer settings sheetData output plan should not invent calcChain output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<dimension ref="A1:B2"/>)")
        + replacement_sheet_data
        + R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
          R"(<pageSetup paperSize="9" orientation="landscape" r:id="rIdPrinter"/>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "printer settings sheetData replacement should preserve pageSetup around sheetData");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == worksheet_relationships,
        "printer settings sheetData replacement should byte-preserve worksheet relationships");
    check(output_reader.read_entry("xl/printerSettings/printerSettings1.bin")
            == printer_settings,
        "printer settings sheetData replacement should byte-preserve printer settings");
    check(output_reader.content_types().override_for(printer_settings_part) != nullptr,
        "printer settings sheetData replacement should preserve printer settings content type");

    const auto* output_relationships = output_reader.relationships_for(worksheet_part);
    check(output_relationships != nullptr,
        "printer settings sheetData replacement should keep worksheet relationships readable");
    const auto* output_printer_relationship =
        output_relationships->find_by_id("rIdPrinter");
    check(output_printer_relationship != nullptr,
        "printer settings sheetData replacement should keep printer settings relationship readable");
    check(output_printer_relationship->target
            == "../printerSettings/printerSettings1.bin",
        "printer settings sheetData replacement should preserve printer settings target");
    check(output_printer_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "printer settings sheetData replacement should keep printer settings relationship internal");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* graph_worksheet_relationships =
        output_graph.relationships_for(worksheet_part);
    check(graph_worksheet_relationships != nullptr,
        "printer settings sheetData replacement should keep worksheet graph relationships");
    const auto* graph_printer_relationship =
        graph_worksheet_relationships->find_by_id("rIdPrinter");
    check(graph_printer_relationship != nullptr,
        "printer settings sheetData replacement should keep printer settings relationship in graph");
    check(graph_printer_relationship->type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings",
        "printer settings sheetData replacement graph should preserve printer settings type");
    check(graph_printer_relationship->target
            == "../printerSettings/printerSettings1.bin",
        "printer settings sheetData replacement graph should preserve printer settings target");
}

void test_package_editor_removes_background_picture_and_header_footer_vml_with_inbound_audit()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-remove-picture-vml-source.xlsx");
    const std::filesystem::path picture_output =
        output_path("fastxlsx-package-editor-remove-background-picture-output.xlsx");
    const std::filesystem::path header_footer_vml_output =
        output_path("fastxlsx-package-editor-remove-header-footer-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
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
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(background_picture_part,
            "explicit background picture part removal");

        check(editor.edit_plan().find_part(background_picture_part) == nullptr,
            "background picture removal should clear the active edit-plan part");
        const auto* removed_picture =
            editor.edit_plan().find_removed_part(background_picture_part);
        check(removed_picture != nullptr,
            "background picture removal should record removed-part audit");
        check(removed_picture->reason.find("background picture") != std::string::npos,
            "background picture removal should retain the removal reason");
        check(removed_picture->reason.find("inbound relationship preserved")
                != std::string::npos,
            "background picture removal should audit preserved inbound relationships");
        check(removed_picture->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "background picture removal inbound audit should include owner part");
        check(removed_picture->reason.find("rIdPicture") != std::string::npos,
            "background picture removal inbound audit should include relationship id");
        check(removed_picture->reason.find("../media/background.png")
                != std::string::npos,
            "background picture removal inbound audit should include original target");
        check(removed_picture->inbound_relationships.size() == 1,
            "background picture removal should keep structured inbound audit");
        const auto& picture_inbound = removed_picture->inbound_relationships.front();
        check(picture_inbound.owner_part == worksheet_part.value(),
            "background picture removal should keep inbound owner part");
        check(picture_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "background picture removal should keep inbound owner relationship entry");
        check(picture_inbound.relationship_id == "rIdPicture",
            "background picture removal should keep inbound relationship id");
        check(picture_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "background picture removal should keep inbound relationship type");
        check(picture_inbound.relationship_target == "../media/background.png",
            "background picture removal should keep inbound raw target");
        check(picture_inbound.target_part == background_picture_part,
            "background picture removal should keep normalized target part");
        check(editor.manifest().find_part(background_picture_part) == nullptr,
            "background picture removal should remove the part from the manifest");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture removal should retain PNG default content type");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture removal should not add a media content type override");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "background picture removal should not rewrite default-only content types");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "background picture removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "background picture removal output plan should omit picture part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/media/background.png", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdPicture",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            "../media/background.png", background_picture_part,
            "background picture removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture removal output plan should preserve header/footer VML");

        editor.save_as(picture_output);

        const auto entries = fastxlsx::test::read_zip_entries(picture_output);
        check(entries.find("xl/media/background.png") == entries.end(),
            "background picture removal output should omit picture part");
        check(entries.find("xl/media/_rels/background.png.rels") == entries.end(),
            "background picture removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(picture_output);
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "background picture removal should preserve content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "background picture removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "background picture removal output should not promote PNG media to override");
        check(output_reader.content_types().override_for(header_footer_vml_part)
                != nullptr,
            "background picture removal output should keep header/footer VML override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "background picture removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "background picture removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "background picture removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "background picture removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "background picture removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "background picture removal should preserve header/footer VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "background picture removal should not create owner relationships for absent media");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "background picture removal should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "background picture removal should keep inbound picture relationship id");
        check(picture_link->target == "../media/background.png",
            "background picture removal should not rewrite inbound picture target");
        check(picture_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "background picture removal should keep inbound picture target mode");
        check(output_relationships->find_by_id("rIdHeaderFooter") != nullptr,
            "background picture removal should keep header/footer VML relationship");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(header_footer_vml_part,
            "explicit header/footer VML drawing part removal");

        check(editor.edit_plan().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal should clear the active edit-plan part");
        const auto* removed_vml =
            editor.edit_plan().find_removed_part(header_footer_vml_part);
        check(removed_vml != nullptr,
            "header/footer VML removal should record removed-part audit");
        check(removed_vml->reason.find("header/footer VML") != std::string::npos,
            "header/footer VML removal should retain the removal reason");
        check(removed_vml->reason.find("inbound relationship preserved")
                != std::string::npos,
            "header/footer VML removal should audit preserved inbound relationships");
        check(removed_vml->reason.find("/xl/worksheets/sheet1.xml")
                != std::string::npos,
            "header/footer VML removal inbound audit should include owner part");
        check(removed_vml->reason.find("rIdHeaderFooter") != std::string::npos,
            "header/footer VML removal inbound audit should include relationship id");
        check(removed_vml->reason.find("../drawings/vmlDrawingHF1.vml")
                != std::string::npos,
            "header/footer VML removal inbound audit should include original target");
        check(removed_vml->inbound_relationships.size() == 1,
            "header/footer VML removal should keep structured inbound audit");
        const auto& vml_inbound = removed_vml->inbound_relationships.front();
        check(vml_inbound.owner_part == worksheet_part.value(),
            "header/footer VML removal should keep inbound owner part");
        check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "header/footer VML removal should keep inbound owner relationship entry");
        check(vml_inbound.relationship_id == "rIdHeaderFooter",
            "header/footer VML removal should keep inbound relationship id");
        check(vml_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "header/footer VML removal should keep inbound relationship type");
        check(vml_inbound.relationship_target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal should keep inbound raw target");
        check(vml_inbound.target_part == header_footer_vml_part,
            "header/footer VML removal should keep normalized target part");
        check(editor.manifest().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal should remove the part from the manifest");
        check(editor.manifest().content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal should remove the content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "header/footer VML removal should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "header/footer VML removal content types rewrite should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal should not invent missing owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(output_plan.relationship_target_audits.empty(),
            "header/footer VML removal output plan should not invent target audits");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "header/footer VML removal output plan should omit VML part");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdHeaderFooter",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawingHF1.vml", header_footer_vml_part,
            "header/footer VML removal output plan should keep worksheet inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "header/footer VML removal output plan should rewrite content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal output plan should preserve background picture");

        editor.save_as(header_footer_vml_output);

        const auto entries = fastxlsx::test::read_zip_entries(header_footer_vml_output);
        check(entries.find("xl/drawings/vmlDrawingHF1.vml") == entries.end(),
            "header/footer VML removal output should omit VML part");
        check(entries.find("xl/drawings/_rels/vmlDrawingHF1.vml.rels") == entries.end(),
            "header/footer VML removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(header_footer_vml_output);
        check(output_reader.content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal output should remove VML content type override");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/drawings/vmlDrawingHF1.vml",
            "header/footer VML removal content types XML should omit VML override");
        check(output_reader.content_types().default_for("png") != nullptr,
            "header/footer VML removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal output should not promote PNG media to override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "header/footer VML removal should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "header/footer VML removal should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "header/footer VML removal should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "header/footer VML removal should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "header/footer VML removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/media/background.png") == background_picture,
            "header/footer VML removal should preserve background picture bytes");
        check(output_reader.relationships_for(header_footer_vml_part) == nullptr,
            "header/footer VML removal should not create owner relationships for absent VML");

        const auto* output_relationships = output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "header/footer VML removal should keep worksheet relationships readable");
        const auto* vml_link = output_relationships->find_by_id("rIdHeaderFooter");
        check(vml_link != nullptr,
            "header/footer VML removal should keep inbound VML relationship id");
        check(vml_link->target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal should not rewrite inbound VML target");
        check(vml_link->target_mode
                == fastxlsx::detail::Relationship::TargetMode::Internal,
            "header/footer VML removal should keep inbound VML target mode");
        check(output_relationships->find_by_id("rIdPicture") != nullptr,
            "header/footer VML removal should keep background picture relationship");
    }
}

void test_package_editor_background_picture_and_header_footer_vml_same_path_ordering()
{
    const std::filesystem::path source =
        output_path("fastxlsx-package-editor-picture-vml-order-source.xlsx");
    const std::filesystem::path picture_restore_output =
        output_path("fastxlsx-package-editor-replace-after-remove-background-picture-output.xlsx");
    const std::filesystem::path header_footer_vml_remove_output =
        output_path("fastxlsx-package-editor-remove-after-replace-header-footer-vml-output.xlsx");

    const std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="png" ContentType="image/png"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/drawings/vmlDrawingHF1.vml" ContentType="application/vnd.openxmlformats-officedocument.vmlDrawing"/>)"
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
        R"(<headerFooter><oddHeader>&amp;G</oddHeader></headerFooter>)"
        R"(<picture r:id="rIdPicture"/>)"
        R"(<legacyDrawingHF r:id="rIdHeaderFooter"/>)"
        R"(</worksheet>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdPicture" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/background.png"/>)"
        R"(<Relationship Id="rIdHeaderFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing" Target="../drawings/vmlDrawingHF1.vml"/>)"
        R"(</Relationships>)";
    const std::string background_picture = "opaque-background-image-bytes";
    const std::string header_footer_vml = R"(<xml><v:shape id="headerPicture"/></xml>)";

    fastxlsx::detail::write_package(source,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/media/background.png", background_picture},
            {"xl/drawings/vmlDrawingHF1.vml", header_footer_vml},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName background_picture_part("/xl/media/background.png");
    const fastxlsx::detail::PartName header_footer_vml_part(
        "/xl/drawings/vmlDrawingHF1.vml");

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        editor.remove_part(background_picture_part,
            "temporary worksheet-owned background picture removal");
        check(editor.edit_plan().find_removed_part(background_picture_part) != nullptr,
            "background picture restore setup should record removed-part audit");
        check(editor.manifest().find_part(background_picture_part) == nullptr,
            "background picture restore setup should remove the manifest part");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture restore setup should retain the PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture restore setup should not create a media override");

        const std::string restored_picture = "restored-background-image-bytes";
        replace_part_with_memory_chunks(editor, background_picture_part, restored_picture,
            "restored worksheet-owned background picture after removal");

        check(editor.edit_plan().find_removed_part(background_picture_part) == nullptr,
            "background picture replacement after removal should clear stale removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture replacement after removal should not invent owner relationships omission");
        check(editor.edit_plan().removed_parts().empty(),
            "background picture replacement after removal should leave no removed parts");
        const auto* picture_plan =
            editor.edit_plan().find_part(background_picture_part);
        check(picture_plan != nullptr,
            "background picture replacement after removal should restore active edit-plan part");
        check(picture_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "background picture replacement after removal should keep final write mode");
        check(picture_plan->reason.find("after removal") != std::string::npos,
            "background picture replacement after removal should keep final reason");
        check_manifest_write_mode(editor, background_picture_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "background picture replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "background picture replacement after removal should retain PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "background picture replacement after removal should not promote PNG media to override");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "background picture replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "background picture replacement after removal output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "background picture replacement after removal output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "background picture replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "background picture replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "background picture replacement after removal output plan should rewrite picture part");
        check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
            true, background_picture_part.value(),
            "background picture replacement after removal output plan should classify picture part");
        const auto* output_picture_plan =
            find_output_entry_plan(output_plan.entries, "xl/media/background.png");
        check(output_picture_plan->reason.find("after removal") != std::string::npos,
            "background picture replacement after removal output plan should keep reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "background picture replacement after removal output plan should classify content types as metadata");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "background picture replacement after removal output plan should preserve header/footer VML");
        check_output_entry_part_context(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", true, header_footer_vml_part.value(),
            "background picture replacement after removal output plan should classify sibling VML part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/media/_rels/background.png.rels") == nullptr,
            "background picture replacement after removal output plan should not invent owner relationships");

        editor.save_as(picture_restore_output);

        const auto entries = fastxlsx::test::read_zip_entries(picture_restore_output);
        check(entries.find("xl/media/background.png") != entries.end(),
            "background picture replacement after removal output should restore picture part");
        check(entries.find("xl/media/_rels/background.png.rels") == entries.end(),
            "background picture replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(picture_restore_output);
        check_preserved_source_entries(editor.reader(), output_reader,
            background_picture_part.zip_path());
        check(output_reader.read_entry("xl/media/background.png") == restored_picture,
            "background picture replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "background picture replacement after removal should preserve source content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "background picture replacement after removal output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part) == nullptr,
            "background picture replacement after removal output should not promote PNG media to override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "background picture replacement after removal should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "background picture replacement after removal should preserve header/footer VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "background picture replacement after removal should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "background picture replacement after removal should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "background picture replacement after removal should keep inbound relationship id");
        check(picture_link->target == "../media/background.png",
            "background picture replacement after removal should not rewrite inbound target");
    }

    {
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string replacement_vml =
            R"(<xml><v:shape id="queuedHeaderPicture"/></xml>)";
        replace_part_with_memory_chunks(editor, header_footer_vml_part, replacement_vml,
            "queued worksheet-owned header/footer VML replacement");
        const auto* prior_vml_plan =
            editor.edit_plan().find_part(header_footer_vml_part);
        check(prior_vml_plan != nullptr,
            "header/footer VML removal-after-replacement setup should queue replacement");
        check(prior_vml_plan->write_mode
                == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "header/footer VML removal-after-replacement setup should local-DOM-rewrite VML");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "header/footer VML replacement setup should not rewrite content types");

        editor.remove_part(header_footer_vml_part,
            "final worksheet-owned header/footer VML removal after replacement");

        check(editor.edit_plan().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should clear active replacement");
        const auto* removed_vml =
            editor.edit_plan().find_removed_part(header_footer_vml_part);
        check(removed_vml != nullptr,
            "header/footer VML removal after replacement should record removed-part audit");
        check(removed_vml->reason.find("after replacement") != std::string::npos,
            "header/footer VML removal after replacement should keep final reason");
        check(removed_vml->reason.find("inbound relationship preserved")
                != std::string::npos,
            "header/footer VML removal after replacement should audit preserved inbound relationship");
        check(removed_vml->inbound_relationships.size() == 1,
            "header/footer VML removal after replacement should keep structured inbound audit");
        const auto& vml_inbound = removed_vml->inbound_relationships.front();
        check(vml_inbound.owner_part == worksheet_part.value(),
            "header/footer VML removal after replacement should keep inbound owner part");
        check(vml_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
            "header/footer VML removal after replacement should keep inbound owner entry");
        check(vml_inbound.relationship_id == "rIdHeaderFooter",
            "header/footer VML removal after replacement should keep inbound relationship id");
        check(vml_inbound.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "header/footer VML removal after replacement should keep inbound relationship type");
        check(vml_inbound.relationship_target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement should keep inbound raw target");
        check(vml_inbound.target_part == header_footer_vml_part,
            "header/footer VML removal after replacement should keep normalized inbound target");
        check(editor.manifest().find_part(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal after replacement should remove content type override");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "header/footer VML removal after replacement should keep PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal after replacement should not promote PNG media to override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "header/footer VML removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "header/footer VML removal after replacement content types audit should be local-DOM-rewrite");
        check(content_types_entry->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal after replacement content types audit should keep structured role");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal after replacement should not invent owner relationships omission");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "header/footer VML removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "header/footer VML removal after replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "header/footer VML removal after replacement output plan should not invent target audits");
        check(output_plan.removed_parts.size() == 1,
            "header/footer VML removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == header_footer_vml_part,
            "header/footer VML removal after replacement output plan should expose removed VML part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "header/footer VML removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "header/footer VML removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "header/footer VML removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "header/footer VML removal after replacement output plan should omit VML part");
        check_output_entry_part_context(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", true, header_footer_vml_part.value(),
            "header/footer VML removal after replacement output plan should classify omitted VML part");
        const auto* output_vml_plan =
            find_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml");
        check(output_vml_plan->reason.find("after replacement") != std::string::npos,
            "header/footer VML removal after replacement output plan should keep final removal reason");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/drawings/vmlDrawingHF1.vml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdHeaderFooter",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
            "../drawings/vmlDrawingHF1.vml", header_footer_vml_part,
            "header/footer VML removal after replacement output plan should keep inbound audit");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "header/footer VML removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
            false, "",
            "header/footer VML removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "header/footer VML removal after replacement output plan should keep content types audit role");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "header/footer VML removal after replacement output plan should preserve background picture");
        check_output_entry_part_context(output_plan.entries, "xl/media/background.png",
            true, background_picture_part.value(),
            "header/footer VML removal after replacement output plan should classify sibling picture part");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/drawings/_rels/vmlDrawingHF1.vml.rels") == nullptr,
            "header/footer VML removal after replacement output plan should not invent owner relationships");

        editor.save_as(header_footer_vml_remove_output);

        const auto entries =
            fastxlsx::test::read_zip_entries(header_footer_vml_remove_output);
        check(entries.find("xl/drawings/vmlDrawingHF1.vml") == entries.end(),
            "header/footer VML removal after replacement output should omit VML part");
        check(entries.find("xl/drawings/_rels/vmlDrawingHF1.vml.rels") == entries.end(),
            "header/footer VML removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(header_footer_vml_remove_output);
        check(output_reader.content_types().override_for(header_footer_vml_part)
                == nullptr,
            "header/footer VML removal after replacement output should remove VML content type override");
        check_not_contains(output_reader.read_entry("[Content_Types].xml"),
            "/xl/drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement content types should omit VML override");
        check(output_reader.content_types().default_for("png") != nullptr,
            "header/footer VML removal after replacement output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part)
                == nullptr,
            "header/footer VML removal after replacement output should not promote PNG media to override");
        check(output_reader.read_entry("_rels/.rels") == package_relationships,
            "header/footer VML removal after replacement should preserve package relationships bytes");
        check(output_reader.read_entry("xl/workbook.xml") == workbook,
            "header/footer VML removal after replacement should preserve workbook bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == workbook_relationships,
            "header/footer VML removal after replacement should preserve workbook relationships bytes");
        check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
            "header/footer VML removal after replacement should preserve worksheet bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "header/footer VML removal after replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/media/background.png") == background_picture,
            "header/footer VML removal after replacement should preserve background picture bytes");
        check(output_reader.relationships_for(header_footer_vml_part) == nullptr,
            "header/footer VML removal after replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "header/footer VML removal after replacement should keep worksheet relationships readable");
        const auto* vml_link = output_relationships->find_by_id("rIdHeaderFooter");
        check(vml_link != nullptr,
            "header/footer VML removal after replacement should keep inbound relationship id");
        check(vml_link->target == "../drawings/vmlDrawingHF1.vml",
            "header/footer VML removal after replacement should not rewrite inbound target");
        check(output_relationships->find_by_id("rIdPicture") != nullptr,
            "header/footer VML removal after replacement should keep background picture relationship");
    }

    {
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-repeat-background-picture-output.xlsx");
        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source);

        const std::string stale_picture = "stale-background-image-bytes";
        const std::string final_picture = "final-background-image-bytes";
        replace_part_with_memory_chunks(editor, background_picture_part, stale_picture,
            "stale repeated worksheet-owned background picture replacement");
        replace_part_with_memory_chunks(editor, background_picture_part, final_picture,
            "final repeated worksheet-owned background picture replacement");

        const auto* picture_plan =
            editor.edit_plan().find_part(background_picture_part);
        check(picture_plan != nullptr,
            "repeated background picture replacement should keep an active edit-plan part");
        check(picture_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated background picture replacement should keep final write mode");
        check(picture_plan->reason.find("final repeated") != std::string::npos,
            "repeated background picture replacement should keep final reason");
        check(picture_plan->reason.find("stale repeated") == std::string::npos,
            "repeated background picture replacement should drop stale reason");
        check_manifest_write_mode(editor, background_picture_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "repeated background picture replacement should mirror final write mode into manifest");
        check(editor.manifest().content_types().default_for("png") != nullptr,
            "repeated background picture replacement should keep PNG default");
        check(editor.manifest().content_types().override_for(background_picture_part)
                == nullptr,
            "repeated background picture replacement should not promote PNG media to override");
        check(editor.edit_plan().find_removed_part(background_picture_part) == nullptr,
            "repeated background picture replacement should not leave removed-part audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/media/_rels/background.png.rels") == nullptr,
            "repeated background picture replacement should not leave owner relationships omission");
        check(editor.edit_plan().find_package_entry("xl/media/_rels/background.png.rels")
                == nullptr,
            "repeated background picture replacement should not invent owner relationships audit");
        check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
            "repeated background picture replacement should not rewrite content types audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "repeated background picture replacement should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(header_footer_vml_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "repeated background picture replacement should keep sibling header/footer VML copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan =
            editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "repeated background picture replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "repeated background picture replacement output plan should preserve calcChain policy");
        check(output_plan.relationship_target_audits.empty(),
            "repeated background picture replacement output plan should not invent target audits");
        check(output_plan.removed_parts.empty(),
            "repeated background picture replacement output plan should not expose removed parts");
        check(output_plan.removed_package_entries.empty(),
            "repeated background picture replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/media/background.png",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "repeated background picture replacement output plan should rewrite picture part");
        const auto* output_picture_plan =
            find_output_entry_plan(output_plan.entries, "xl/media/background.png");
        check(output_picture_plan->reason.find("final repeated") != std::string::npos,
            "repeated background picture replacement output plan should keep final reason");
        check(output_picture_plan->reason.find("stale repeated") == std::string::npos,
            "repeated background picture replacement output plan should drop stale reason");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve content types");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawingHF1.vml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "repeated background picture replacement output plan should preserve sibling VML");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/media/_rels/background.png.rels") == nullptr,
            "repeated background picture replacement output plan should not invent owner relationships");

        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader,
            background_picture_part.zip_path());
        check(output_reader.read_entry("xl/media/background.png") == final_picture,
            "repeated background picture replacement should write final bytes");
        check(output_reader.read_entry("xl/media/background.png") != stale_picture,
            "repeated background picture replacement should not write stale bytes");
        check(output_reader.read_entry("[Content_Types].xml") == content_types,
            "repeated background picture replacement should preserve content types bytes");
        check(output_reader.content_types().default_for("png") != nullptr,
            "repeated background picture replacement output should preserve PNG default");
        check(output_reader.content_types().override_for(background_picture_part) == nullptr,
            "repeated background picture replacement output should not promote PNG media to override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == worksheet_relationships,
            "repeated background picture replacement should not prune worksheet relationships");
        check(output_reader.read_entry("xl/drawings/vmlDrawingHF1.vml")
                == header_footer_vml,
            "repeated background picture replacement should preserve sibling VML bytes");
        check(output_reader.relationships_for(background_picture_part) == nullptr,
            "repeated background picture replacement should not create owner relationships");
        const auto* output_relationships =
            output_reader.relationships_for(worksheet_part);
        check(output_relationships != nullptr,
            "repeated background picture replacement should keep worksheet relationships readable");
        const auto* picture_link = output_relationships->find_by_id("rIdPicture");
        check(picture_link != nullptr,
            "repeated background picture replacement should keep inbound relationship id");
        check(picture_link->target == "../media/background.png",
            "repeated background picture replacement should not rewrite inbound target");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-linked-background-vml")) {
            test_package_editor_sheet_data_patch_preserves_background_picture_and_header_footer_vml();
            test_package_editor_sheet_data_patch_preserves_page_setup_printer_settings();
            test_package_editor_removes_background_picture_and_header_footer_vml_with_inbound_audit();
            test_package_editor_background_picture_and_header_footer_vml_same_path_ordering();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
