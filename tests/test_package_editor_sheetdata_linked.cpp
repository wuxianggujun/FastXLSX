#include "test_package_editor_sheetdata_linked_common.hpp"

void test_package_editor_replaces_worksheet_and_preserves_linked_object_parts()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-linked-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
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
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>99</v></c></row></sheetData>)"
        R"(<sheetProtection sheet="1"/>)"
        R"(<protectedRanges><protectedRange name="Locked" sqref="A1:B2"/></protectedRanges>)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
        R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<legacyDrawing r:id="rId7"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(<extLst><ext uri="{fastxlsx-test}"/></extLst>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "linked drawing part should remain visible in the edit plan");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked drawing part should remain copy-original");
    check(drawing_plan->reason.find("worksheet relationship rId1")
            != std::string::npos,
        "linked drawing copy reason should come from worksheet relationship traversal");
    check(drawing_plan->reason.find("relationships/drawing") != std::string::npos,
        "linked drawing copy reason should include relationship type");
    check(drawing_plan->relationship_owner_part == worksheet_part.value(),
        "linked drawing copy audit should keep structured relationship owner");
    check(drawing_plan->relationship_id == "rId1",
        "linked drawing copy audit should keep structured relationship id");
    check(drawing_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "linked drawing copy audit should keep structured relationship type");
    check(drawing_plan->relationship_target == "../drawings/drawing1.xml",
        "linked drawing copy audit should keep structured relationship target");
    const auto* chart_plan = editor.edit_plan().find_part(chart_part);
    check(chart_plan != nullptr,
        "linked chart part should remain visible in the edit plan");
    check(chart_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked chart part should remain copy-original");
    check(chart_plan->reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && chart_plan->reason.find("rId2") != std::string::npos,
        "linked chart copy reason should come from drawing relationship traversal");
    check(chart_plan->relationship_owner_part == drawing_part.value(),
        "linked chart copy audit should keep drawing-owned relationship owner");
    check(chart_plan->relationship_id == "rId2",
        "linked chart copy audit should keep drawing-owned relationship id");
    check(chart_plan->relationship_target == "../charts/chart1.xml",
        "linked chart copy audit should keep drawing-owned relationship target");
    const auto* image_plan = editor.edit_plan().find_part(image_part);
    check(image_plan != nullptr,
        "linked image part should remain visible in the edit plan");
    check(image_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked image part should remain copy-original");
    check(image_plan->reason.find("/xl/drawings/drawing1.xml") != std::string::npos
            && image_plan->reason.find("rId1") != std::string::npos,
        "linked image copy reason should come from drawing relationship traversal");
    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "linked table part should remain visible in the edit plan");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked table part should remain copy-original");
    check(table_plan->reason.find("worksheet relationship rId3") != std::string::npos,
        "linked table copy reason should come from worksheet relationship traversal");
    const auto* vml_drawing_plan = editor.edit_plan().find_part(vml_drawing_part);
    check(vml_drawing_plan != nullptr,
        "URI-qualified base target should remain visible in the edit plan");
    check(vml_drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "URI-qualified base target should remain copy-original");
    check(vml_drawing_plan->reason.find("rId7") != std::string::npos
            && vml_drawing_plan->reason.find("/xl/drawings/vmlDrawing1.vml")
                != std::string::npos,
        "URI-qualified base target copy reason should come from relationship traversal");
    const auto* percent_encoded_drawing_plan =
        editor.edit_plan().find_part(percent_encoded_drawing_part);
    check(percent_encoded_drawing_plan != nullptr,
        "percent-decoded target should remain visible in the edit plan");
    check(percent_encoded_drawing_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "percent-decoded target should remain copy-original");
    check(percent_encoded_drawing_plan->reason.find("rId8") != std::string::npos
            && percent_encoded_drawing_plan->reason.find("/xl/drawings/drawing space.xml")
                != std::string::npos,
        "percent-decoded target copy reason should come from relationship traversal");
    const auto* shared_strings_plan = editor.edit_plan().find_part(shared_strings_part);
    check(shared_strings_plan != nullptr,
        "sharedStrings part should remain visible in the edit plan");
    check(shared_strings_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings part should remain copy-original");
    check(shared_strings_plan->reason.find("shared strings") != std::string::npos,
        "sharedStrings copy reason should come from dependency analysis");
    check(shared_strings_plan->relationship_owner_part.empty(),
        "sharedStrings static dependency should not carry relationship owner metadata");
    const auto* styles_plan = editor.edit_plan().find_part(styles_part);
    check(styles_plan != nullptr,
        "styles part should remain visible in the edit plan");
    check(styles_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles part should remain copy-original");
    check(styles_plan->reason.find("style ids") != std::string::npos,
        "styles copy reason should come from dependency analysis");
    check(styles_plan->relationship_owner_part.empty(),
        "styles static dependency should not carry relationship owner metadata");
    const auto* vba_plan = editor.edit_plan().find_part(vba_part);
    check(vba_plan != nullptr,
        "vba part should remain visible in the edit plan");
    check(vba_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "vba part should remain copy-original");
    const auto* opaque_extension_plan = editor.edit_plan().find_part(opaque_extension_part);
    check(opaque_extension_plan != nullptr,
        "reachable unknown extension part should remain visible in the edit plan");
    check(opaque_extension_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "reachable unknown extension part should remain copy-original");
    check(opaque_extension_plan->reason.find("rId9") != std::string::npos
            && opaque_extension_plan->reason.find(
                   "https://fastxlsx.invalid/relationships/opaque-extension")
                != std::string::npos
            && opaque_extension_plan->reason.find("/custom/opaque-extension.bin")
                != std::string::npos,
        "reachable unknown extension copy reason should come from relationship traversal");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "protection metadata", "caller review"}),
        "worksheet replacement should audit protection metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "protected-range metadata", "caller review"}),
        "worksheet replacement should audit protected range metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "autoFilter metadata", "caller review"}),
        "worksheet replacement should audit autoFilter metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "merged-cell metadata", "caller review"}),
        "worksheet replacement should audit merged-cell metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data validation metadata", "caller review"}),
        "worksheet replacement should audit data validation metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "conditional formatting metadata", "caller review"}),
        "worksheet replacement should audit conditional formatting metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "ignored-error metadata", "caller review"}),
        "worksheet replacement should audit ignored-error metadata in replacement payload");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "extension metadata", "caller review"}),
        "worksheet replacement should audit extension metadata in replacement payload");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "linked-object output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "linked-object output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "linked-object output plan should snapshot structured relationship audits");
    check(has_note_containing(output_plan.notes,
              {"worksheet relationships are preserved", "policy review"}),
        "linked-object output plan should snapshot dependency audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "autoFilter metadata", "caller review"}),
        "linked-object output plan should snapshot replacement autoFilter audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "data validation metadata", "caller review"}),
        "linked-object output plan should snapshot replacement data validation audit notes");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "extension metadata", "caller review"}),
        "linked-object output plan should snapshot replacement extension audit notes");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/drawing1.xml",
        worksheet_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml",
        "linked drawing output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/charts/chart1.xml",
        drawing_part.value(), "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml",
        "linked chart output plan should keep drawing relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/image1.png",
        drawing_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/image1.png",
        "linked image output plan should keep drawing relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/tables/table1.xml",
        worksheet_part.value(), "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "../tables/table1.xml",
        "linked table output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "custom/opaque-extension.bin",
        worksheet_part.value(), "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin",
        "linked unknown extension output plan should keep worksheet relationship audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/sharedStrings.xml", "", "",
        "", "",
        "sharedStrings output plan should not invent relationship-derived audit");
    check_output_entry_relationship_context(output_plan.entries, "xl/styles.xml", "", "", "", "",
        "styles output plan should not invent relationship-derived audit");
    const auto* worksheet_relationships_plan =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved worksheet relationships");
    check(worksheet_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved worksheet relationships should be copy-original in package-entry audit");
    check(worksheet_relationships_plan->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "preserved worksheet relationships audit should name the owner part");
    const auto* drawing_relationships_plan =
        editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved drawing relationships");
    check(drawing_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved drawing relationships should be copy-original in package-entry audit");
    check(drawing_relationships_plan->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "preserved drawing relationships audit should name the owner part");
    const auto* shared_strings_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved sharedStrings relationships");
    check(shared_strings_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved sharedStrings relationships should be copy-original in package-entry audit");
    check(shared_strings_relationships_plan->reason.find("/xl/sharedStrings.xml")
            != std::string::npos,
        "preserved sharedStrings relationships audit should name the owner part");
    const auto* opaque_extension_relationships_plan =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_extension_relationships_plan != nullptr,
        "linked-object worksheet rewrite should audit preserved unknown extension relationships");
    check(opaque_extension_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved unknown extension relationships should be copy-original in package-entry audit");
    check(opaque_extension_relationships_plan->reason.find("/custom/opaque-extension.bin")
            != std::string::npos,
        "preserved unknown extension relationships audit should name the owner part");
    bool found_external_relationship_note = false;
    bool found_external_relationship_detail_note = false;
    bool found_uri_qualified_relationship_note = false;
    bool found_uri_qualified_relationship_detail_note = false;
    bool found_invalid_internal_relationship_note = false;
    bool found_invalid_internal_relationship_detail_note = false;
    bool found_unresolved_internal_relationship_note = false;
    bool found_unresolved_internal_relationship_detail_note = false;
    for (const std::string& note : editor.edit_plan().notes()) {
        if (note.find("external relationship targets") != std::string::npos) {
            found_external_relationship_note = true;
        }
        if (note.find("external relationship targets are preserved in owner .rels")
                != std::string::npos
            && note.find("/xl/worksheets/sheet1.xml") != std::string::npos
            && note.find("rId2") != std::string::npos
            && note.find("relationships/hyperlink") != std::string::npos
            && note.find("https://example.invalid/link") != std::string::npos) {
            found_external_relationship_detail_note = true;
        }
        if (note.find("URI-qualified internal relationship targets") != std::string::npos) {
            found_uri_qualified_relationship_note = true;
        }
        if (note.find("URI-qualified internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId7") != std::string::npos
            && note.find("relationships/vmlDrawing") != std::string::npos
            && note.find("../drawings/vmlDrawing1.vml#shape1") != std::string::npos
            && note.find("/xl/drawings/vmlDrawing1.vml") != std::string::npos) {
            found_uri_qualified_relationship_detail_note = true;
        }
        if (note.find("invalid internal relationship targets") != std::string::npos) {
            found_invalid_internal_relationship_note = true;
        }
        if (note.find("invalid internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId6") != std::string::npos
            && note.find("../../../outside.bin") != std::string::npos) {
            found_invalid_internal_relationship_detail_note = true;
        }
        if (note.find("unresolved internal relationship targets") != std::string::npos) {
            found_unresolved_internal_relationship_note = true;
        }
        if (note.find("unresolved internal relationship targets require package structure review")
                != std::string::npos
            && note.find("rId4") != std::string::npos
            && note.find("../comments/comment1.xml") != std::string::npos
            && note.find("/xl/comments/comment1.xml") != std::string::npos) {
            found_unresolved_internal_relationship_detail_note = true;
        }
    }
    check(found_external_relationship_note,
        "linked-object worksheet rewrite should audit external relationship targets");
    check(found_external_relationship_detail_note,
        "linked-object worksheet rewrite should include external relationship details");
    check(found_uri_qualified_relationship_note,
        "linked-object worksheet rewrite should audit URI-qualified internal targets");
    check(found_uri_qualified_relationship_detail_note,
        "linked-object worksheet rewrite should include URI-qualified relationship details");
    check(found_invalid_internal_relationship_note,
        "linked-object worksheet rewrite should audit invalid internal targets");
    check(found_invalid_internal_relationship_detail_note,
        "linked-object worksheet rewrite should include invalid relationship details");
    check(found_unresolved_internal_relationship_note,
        "linked-object worksheet rewrite should audit unresolved internal targets");
    check(found_unresolved_internal_relationship_detail_note,
        "linked-object worksheet rewrite should include unresolved relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"external relationship targets are preserved in owner .rels",
                  "/xl/drawings/drawing1.xml", "rId3",
                  "https://drawing.example.invalid/link"}),
        "linked-object worksheet rewrite should include drawing-owned external relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"external relationship targets are preserved in owner .rels",
                  "/custom/opaque-extension.bin", "rIdOpaqueExternal",
                  "https://example.invalid/opaque-extension-audit"}),
        "linked-object worksheet rewrite should include unknown extension-owned external relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"URI-qualified internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId4", "../charts/chart1.xml#plotArea",
                  "/xl/charts/chart1.xml"}),
        "linked-object worksheet rewrite should include drawing-owned URI-qualified relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"unresolved internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId5", "../embeddings/oleObject1.bin",
                  "/xl/embeddings/oleObject1.bin"}),
        "linked-object worksheet rewrite should include drawing-owned unresolved relationship details");
    check(has_note_containing(editor.edit_plan().notes(),
              {"invalid internal relationship targets require package structure review",
                  "/xl/drawings/drawing1.xml", "rId6", "../../../outside-from-drawing.bin"}),
        "linked-object worksheet rewrite should include drawing-owned invalid relationship details");
    bool found_structured_worksheet_external_audit = false;
    bool found_structured_drawing_external_audit = false;
    bool found_structured_drawing_uri_audit = false;
    bool found_structured_drawing_unresolved_audit = false;
    bool found_structured_drawing_invalid_audit = false;
    bool found_structured_unknown_external_audit = false;
    for (const fastxlsx::detail::RelationshipTargetAudit& audit :
        editor.edit_plan().relationship_target_audits()) {
        if (audit.owner_part == worksheet_part && audit.relationship_id == "rId2"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://example.invalid/link"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_worksheet_external_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId3"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"
            && audit.target == "https://drawing.example.invalid/link"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_drawing_external_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId4"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"
            && audit.target == "../charts/chart1.xml#plotArea"
            && audit.normalized_target == "/xl/charts/chart1.xml"
            && audit.note.find("has base part /xl/charts/chart1.xml")
                != std::string::npos) {
            found_structured_drawing_uri_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId5"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../embeddings/oleObject1.bin"
            && audit.normalized_target == "/xl/embeddings/oleObject1.bin"
            && audit.note.find("resolves to unregistered part /xl/embeddings/oleObject1.bin")
                != std::string::npos) {
            found_structured_drawing_unresolved_audit = true;
        }
        if (audit.owner_part == drawing_part && audit.relationship_id == "rId6"
            && audit.relationship_type
                == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/oleObject"
            && audit.target == "../../../outside-from-drawing.bin"
            && audit.normalized_target.empty()
            && audit.note.find("cannot be normalized as a package part")
                != std::string::npos) {
            found_structured_drawing_invalid_audit = true;
        }
        if (audit.owner_part == opaque_extension_part
            && audit.relationship_id == "rIdOpaqueExternal"
            && audit.relationship_type
                == "https://fastxlsx.invalid/relationships/opaque-extension-audit"
            && audit.target == "https://example.invalid/opaque-extension-audit"
            && audit.normalized_target.empty()
            && audit.note.find("external relationship targets") != std::string::npos) {
            found_structured_unknown_external_audit = true;
        }
    }
    check(found_structured_worksheet_external_audit,
        "linked-object worksheet rewrite should preserve structured worksheet-owned external audit");
    check(found_structured_drawing_external_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned external audit");
    check(found_structured_drawing_uri_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned URI-qualified audit");
    check(found_structured_drawing_unresolved_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned unresolved audit");
    check(found_structured_drawing_invalid_audit,
        "linked-object worksheet rewrite should preserve structured drawing-owned invalid audit");
    check(found_structured_unknown_external_audit,
        "linked-object worksheet rewrite should preserve structured unknown-extension-owned external audit");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "linked-object worksheet rewrite should still remove stale calcChain");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "linked-object worksheet rewrite should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "linked-object worksheet rewrite should replace only the worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet relationships should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "drawing XML should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "drawing relationships should be byte-preserved");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "chart XML should be byte-preserved");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "media bytes should be byte-preserved");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "table XML should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "URI-qualified base target bytes should be byte-preserved");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "percent-decoded relationship target bytes should be byte-preserved");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "sharedStrings XML should be byte-preserved");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "sharedStrings relationships should be byte-preserved");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "styles XML should be byte-preserved");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "VBA project bytes should be byte-preserved");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "unknown extension bytes should be byte-preserved");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "unknown extension relationships should be byte-preserved");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "package relationships should be byte-preserved");

    const auto* sheet_relationships = output_reader.relationships_for(worksheet_part);
    check(sheet_relationships != nullptr,
        "preserved worksheet relationships should remain readable");
    check(sheet_relationships->find_by_id("rId1") != nullptr,
        "preserved drawing relationship should remain readable");
    check(sheet_relationships->find_by_id("rId2") != nullptr,
        "preserved external hyperlink relationship should remain readable");
    check(sheet_relationships->find_by_id("rId3") != nullptr,
        "preserved table relationship should remain readable");
    check(sheet_relationships->find_by_id("rId4") != nullptr,
        "preserved unresolved internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId5") != nullptr,
        "preserved URI-qualified internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId6") != nullptr,
        "preserved invalid internal relationship should remain readable");
    check(sheet_relationships->find_by_id("rId7") != nullptr,
        "preserved URI-qualified base target relationship should remain readable");
    check(sheet_relationships->find_by_id("rId8") != nullptr,
        "preserved percent-encoded target relationship should remain readable");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "preserved drawing relationships should remain readable");
    check(drawing_relationships->find_by_id("rId1") != nullptr,
        "preserved image relationship should remain readable");
    check(drawing_relationships->find_by_id("rId2") != nullptr,
        "preserved chart relationship should remain readable");
    check(drawing_relationships->find_by_id("rId3") != nullptr,
        "preserved drawing-owned external relationship should remain readable");
    check(drawing_relationships->find_by_id("rId4") != nullptr,
        "preserved drawing-owned URI-qualified relationship should remain readable");
    check(drawing_relationships->find_by_id("rId5") != nullptr,
        "preserved drawing-owned unresolved relationship should remain readable");
    check(drawing_relationships->find_by_id("rId6") != nullptr,
        "preserved drawing-owned invalid relationship should remain readable");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "workbook relationships should preserve sharedStrings relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "workbook relationships should preserve styles relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "workbook relationships should preserve VBA relationship");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "workbook relationships should remove only calcChain relationship");
    const auto* workbook_relationship_set = output_reader.relationships_for(workbook_part);
    check(workbook_relationship_set != nullptr,
        "preserved workbook relationships should remain readable");
    check(workbook_relationship_set->find_by_id("rId3") != nullptr,
        "preserved sharedStrings relationship should remain readable");
    check(workbook_relationship_set->find_by_id("rId4") != nullptr,
        "preserved styles relationship should remain readable");
    const auto* shared_strings_relationships =
        output_reader.relationships_for(shared_strings_part);
    check(shared_strings_relationships != nullptr,
        "preserved sharedStrings relationships should remain readable");
    check(shared_strings_relationships->find_by_id("rIdSharedExternal") != nullptr,
        "preserved sharedStrings owner relationships should remain attached to sharedStrings");
    const auto* opaque_extension_relationships =
        output_reader.relationships_for(opaque_extension_part);
    check(opaque_extension_relationships != nullptr,
        "preserved unknown extension relationships should remain readable");
    check(opaque_extension_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "preserved unknown extension owner relationships should remain attached to the extension part");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "content types should remove calcChain override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "content types should preserve sharedStrings override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "content types should preserve styles override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "content types should preserve VBA override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "content types should preserve table override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "content types should preserve PNG default");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "content types should not promote PNG media defaults to overrides");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, R"(PartName="/xl/media/image1.png")",
        "rewritten content types should not add unnecessary image overrides");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "workbook XML should preserve defined names while updating calc metadata");
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "workbook XML should add calcPr when it was absent");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-linked")) {
            test_package_editor_replaces_worksheet_and_preserves_linked_object_parts();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
