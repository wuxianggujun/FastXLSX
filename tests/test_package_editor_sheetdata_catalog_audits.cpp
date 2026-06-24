#include "test_package_editor_sheetdata_catalog_common.hpp"

void test_package_editor_rejects_invalid_worksheet_replacement_xml_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-invalid-xml-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-invalid-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    const auto check_no_state_change = [&]() {
        check(editor.edit_plan().size() == initial_plan_size,
            "invalid worksheet XML should not change edit plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "invalid worksheet XML should not add audit notes");
        check(editor.edit_plan().relationship_target_audits().size()
                == initial_relationship_target_audit_count,
            "invalid worksheet XML should not add relationship target audits");
        check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                == initial_worksheet_relationship_reference_audit_count,
            "invalid worksheet XML should not add worksheet relationship reference audits");
        check(editor.edit_plan().removed_parts().empty(),
            "invalid worksheet XML should not record removed parts");
        check(editor.edit_plan().package_entries().empty(),
            "invalid worksheet XML should not record package-entry audit");
        check(editor.edit_plan().removed_package_entries().empty(),
            "invalid worksheet XML should not record removed package-entry audit");
        check(!editor.edit_plan().full_calculation_on_load(),
            "invalid worksheet XML should not request recalculation");
        check(editor.edit_plan().calc_chain_action()
                == fastxlsx::detail::CalcChainAction::Preserve,
            "invalid worksheet XML should not change calcChain policy");
        check(editor.manifest().find_part(calc_chain_part) != nullptr,
            "invalid worksheet XML should keep calcChain in the manifest");
        check_manifest_write_mode(editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep worksheet copy-original");
        check_manifest_write_mode(editor, workbook_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep workbook copy-original");
        check_manifest_write_mode(editor, calc_chain_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "invalid worksheet XML should keep calcChain copy-original");
    };

    for (std::string_view invalid_xml : {
            std::string_view(""),
            std::string_view("   \r\n\t"),
            std::string_view("<sheetData/>"),
            std::string_view("<!DOCTYPE worksheet><worksheet/>"),
            std::string_view("<ignored/><worksheet/>"),
            std::string_view("<worksheet/> <!-- trailing comment -->"),
            std::string_view("<worksheet/><?trailing instruction?>"),
            std::string_view("<worksheet><sheetData/></worksheet><worksheet/>"),
            std::string_view("<worksheet><sheetData/>"),
          }) {
        bool failed = false;
        try {
            replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, std::string(invalid_xml));
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "worksheet",
                "invalid worksheet XML failure should name worksheet replacement");
        }
        check(failed,
            "PackageEditor should reject invalid worksheet replacement XML");
        check_no_state_change();
    }

    bool by_name_failed = false;
    try {
        replace_worksheet_part_by_name_from_single_chunk_source(editor, "Sheet1", "<notWorksheet/>");
    } catch (const std::exception& error) {
        by_name_failed = true;
        check_contains(error.what(), "worksheet",
            "by-name invalid worksheet XML failure should name worksheet replacement");
    }
    check(by_name_failed,
        "PackageEditor should reject invalid by-name worksheet replacement XML");
    check_no_state_change();

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
}

void test_package_editor_replaces_worksheet_with_payload_audit_notes()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-payload-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-payload-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetPr filterMode="1"/>)"
        R"(<sheetCalcPr fullCalcOnLoad="1"/>)"
        R"(<dimension ref="A1:B1"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<customSheetViews><customSheetView guid="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"/></customSheetViews>)"
        R"(<sheetFormatPr defaultRowHeight="14.25"/>)"
        R"(<cols><col min="1" max="2" width="18" customWidth="1"/></cols>)"
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="0"><f>SUM(A1:A1)</f><v>9</v></c></row></sheetData>)"
        R"(<sortState ref="A1:B1"><sortCondition ref="A1:A1"/></sortState>)"
        R"(<scenarios><scenario name="Replacement" user="FastXLSX"/></scenarios>)"
        R"(<dataConsolidate function="count"><dataRefs count="1"><dataRef ref="A1:B1" sheet="Sheet1"/></dataRefs></dataConsolidate>)"
        R"(<customProperties><customPr name="FastXLSXReplacement"/></customProperties>)"
        R"(<cellWatches><cellWatch r="B1"/></cellWatches>)"
        R"(<smartTags><cellSmartTags r="B1"><cellSmartTag type="urn:fastxlsx:replacement"/></cellSmartTags></smartTags>)"
        R"(<webPublishItems count="1"><webPublishItem id="2" divId="FastXLSXReplacement" sourceType="range" sourceRef="Sheet1!A1:B1"/></webPublishItems>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<printOptions gridLines="1"/>)"
        R"(<pageMargins left="0.5" right="0.5" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup paperSize="9" orientation="portrait"/>)"
        R"(<headerFooter><oddFooter>&amp;RPage &amp;P</oddFooter></headerFooter>)"
        R"(<rowBreaks count="1" manualBreakCount="1"><brk id="1" max="16383" man="1"/></rowBreaks>)"
        R"(<colBreaks count="1" manualBreakCount="1"><brk id="1" max="1048575" man="1"/></colBreaks>)"
        R"(<phoneticPr fontId="1" type="noConversion"/>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<legacyDrawing r:id="rId7"/>)"
        R"(<picture r:id="rId1"/>)"
        R"(<legacyDrawingHF r:id="rId7"/>)"
        R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rId1"/></oleObjects>)"
        R"(<controls><control shapeId="1" r:id="rId1"/></controls>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet replacement should audit shared string index references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references", "xl/styles.xml"}),
        "worksheet replacement should audit style id references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas", "calcChain policy"}),
        "worksheet replacement should audit formula references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet property metadata", "caller review"}),
        "worksheet replacement should audit sheet property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet calculation metadata", "caller review"}),
        "worksheet replacement should audit sheet calculation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "dimension metadata", "caller review"}),
        "worksheet replacement should audit dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "view metadata", "caller review"}),
        "worksheet replacement should audit sheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "custom view metadata", "caller review"}),
        "worksheet replacement should audit custom view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "default format metadata", "caller review"}),
        "worksheet replacement should audit default format metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column metadata", "caller review"}),
        "worksheet replacement should audit column metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sort-state metadata", "caller review"}),
        "worksheet replacement should audit sort-state metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "scenario metadata", "caller review"}),
        "worksheet replacement should audit scenario metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data consolidation metadata", "caller review"}),
        "worksheet replacement should audit data consolidation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "custom property metadata", "caller review"}),
        "worksheet replacement should audit custom property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "cell watch metadata", "caller review"}),
        "worksheet replacement should audit cell watch metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "smart tag metadata", "caller review"}),
        "worksheet replacement should audit smart tag metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "web publishing metadata", "caller review"}),
        "worksheet replacement should audit web publishing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "print options metadata", "caller review"}),
        "worksheet replacement should audit print options metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page margins metadata", "caller review"}),
        "worksheet replacement should audit page margins metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page setup metadata", "caller review"}),
        "worksheet replacement should audit page setup metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "header/footer metadata", "caller review"}),
        "worksheet replacement should audit header/footer metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "row break metadata", "caller review"}),
        "worksheet replacement should audit row break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column break metadata", "caller review"}),
        "worksheet replacement should audit column break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "phonetic metadata", "caller review"}),
        "worksheet replacement should audit phonetic metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "hyperlink metadata", "worksheet relationships"}),
        "worksheet replacement should audit hyperlink relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "legacy drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit legacy drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "background picture relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit background picture relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "header/footer drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit header/footer drawing relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "OLE object relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit OLE object relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "control relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit control relationship metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "table relationship metadata",
                  "worksheet relationships"}),
        "worksheet replacement should audit table relationship metadata");
    using PayloadAuditKind =
        fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope =
        fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet replacement should record structured sharedStrings payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "worksheet replacement should record structured styles payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::WorksheetReplacement, "f",
              {"formulas", "calcChain policy"}),
        "worksheet replacement should record structured formula payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "dimension",
              {"dimension metadata", "caller review"}),
        "worksheet replacement should record structured dimension payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "sheetViews",
              {"view metadata", "caller review"}),
        "worksheet replacement should record structured sheetViews payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "scenarios",
              {"scenario metadata", "caller review"}),
        "worksheet replacement should record structured scenarios payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "dataConsolidate",
              {"data consolidation metadata", "caller review"}),
        "worksheet replacement should record structured dataConsolidate payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "customProperties",
              {"custom property metadata", "caller review"}),
        "worksheet replacement should record structured customProperties payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "cellWatches",
              {"cell watch metadata", "caller review"}),
        "worksheet replacement should record structured cellWatches payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "smartTags",
              {"smart tag metadata", "caller review"}),
        "worksheet replacement should record structured smartTags payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "webPublishItems",
              {"web publishing metadata", "caller review"}),
        "worksheet replacement should record structured webPublishItems payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "drawing",
              {"drawing relationship metadata", "worksheet relationships"}),
        "worksheet replacement should record structured drawing payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "tableParts",
              {"table relationship metadata", "worksheet relationships"}),
        "worksheet replacement should record structured tableParts payload audit");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet payload audit replacement should stream-rewrite worksheet");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet payload audit replacement should rewrite workbook calc metadata");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "worksheet payload audit replacement should remove calcChain by default");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet payload audit output plan should snapshot sharedStrings note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "style id references", "xl/styles.xml"}),
        "worksheet payload audit output plan should snapshot styles note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "contains formulas", "calcChain policy"}),
        "worksheet payload audit output plan should snapshot formula note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "dimension metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot dimension note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "view metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot sheet view note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "column metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot column note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "scenario metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot scenario note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "page setup metadata", "caller review"}),
        "worksheet payload audit output plan should snapshot page setup note");
    check(has_note_containing(output_plan.notes,
              {"worksheet replacement", "drawing relationship metadata",
                  "worksheet relationships"}),
        "worksheet payload audit output plan should snapshot linked metadata note");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == payload_audits.size(),
        "worksheet payload audit output plan should mirror structured payload audits");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::WorksheetReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "worksheet payload audit output plan should keep structured sharedStrings audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::RelationshipMetadata,
              PayloadAuditScope::WorksheetReplacement, "drawing",
              {"drawing relationship metadata", "worksheet relationships"}),
        "worksheet payload audit output plan should keep structured drawing audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::RangeMetadata,
              PayloadAuditScope::WorksheetReplacement, "scenarios",
              {"scenario metadata", "caller review"}),
        "worksheet payload audit output plan should keep structured scenario audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "worksheet payload audit output should write replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "worksheet payload audit output should preserve worksheet relationships");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "worksheet payload audit output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "worksheet payload audit output should preserve styles bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "worksheet payload audit output should preserve drawing bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "worksheet payload audit output should preserve table bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "worksheet payload audit output should preserve unknown extension bytes");
}

void test_package_editor_worksheet_replacement_audits_relationship_id_mismatch()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-rid-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-rid-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>41</v></c></row></sheetData>)"
        R"(<rowBreaks count="1" manualBreakCount="1"><brk id="1" max="16383" man="1"/></rowBreaks>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<pageSetup r:id="rIdPrinterMissing"/>)"
        R"(<pageSetup r:id="rId2"/>)"
        R"(<drawing r:id="rId2"/>)"
        R"(<drawing r:id="rIdMissing"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rIdMissing", "<drawing>",
                  "do not contain that id", "repair worksheet .rels"}),
        "worksheet replacement should audit replacement relationship ids missing from source .rels");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rIdPrinterMissing", "<pageSetup>",
                  "do not contain that id", "repair worksheet .rels"}),
        "worksheet replacement should audit missing printer settings relationship ids");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement leaves preserved worksheet relationship id rId1",
                  "unreferenced", "stale linked-object relationships"}),
        "worksheet replacement should audit preserved source relationships omitted by replacement XML");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId2", "<drawing>",
                  "relationships/hyperlink", "expected", "relationships/drawing",
                  "review worksheet .rels"}),
        "worksheet replacement should audit relationship ids whose type does not match the element");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId2", "<pageSetup>",
                  "relationships/hyperlink", "expected", "relationships/printerSettings",
                  "review worksheet .rels"}),
        "worksheet replacement should audit printer settings relationship type mismatches");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement references relationship id 1"}),
        "worksheet replacement relationship-id audit should ignore ordinary unqualified id attributes");

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    static constexpr std::string_view hyperlink_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    static constexpr std::string_view printer_settings_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/printerSettings";
    const auto has_reference_audit = [&](const auto& audits,
                                         WorksheetReferenceAuditKind kind,
                                         std::string_view element,
                                         std::string_view relationship_id,
                                         std::string_view expected_type,
                                         std::string_view actual_type,
                                         std::string_view note_text) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part && audit.kind == kind
                    && audit.element == element && audit.relationship_id == relationship_id
                    && audit.expected_relationship_type == expected_type
                    && audit.actual_relationship_type == actual_type
                    && audit.note.find(note_text) != std::string::npos;
            });
    };

    const auto& relationship_reference_audits =
        editor.edit_plan().worksheet_relationship_reference_audits();
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "worksheet replacement should record structured missing relationship-id audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "pageSetup",
              "rIdPrinterMissing", printer_settings_relationship_type, {},
              "do not contain that id"),
        "worksheet replacement should record structured missing printer settings audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "worksheet replacement should record structured unreferenced relationship-id audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "worksheet replacement should record structured relationship type mismatch audit");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "pageSetup", "rId2",
              printer_settings_relationship_type, hyperlink_relationship_type, "expected"),
        "worksheet replacement should record structured printer settings type mismatch audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdMissing", "<drawing>", "repair worksheet .rels"}),
        "worksheet relationship-id audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdPrinterMissing", "<pageSetup>",
                  "repair worksheet .rels"}),
        "printer settings relationship-id audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"preserved worksheet relationship id rId1", "unreferenced"}),
        "worksheet unreferenced relationship audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rId2", "<drawing>", "relationships/hyperlink",
                  "relationships/drawing"}),
        "worksheet relationship type mismatch audit should be visible in planned output notes");
    check(has_note_containing(output_plan.notes,
              {"relationship id rId2", "<pageSetup>", "relationships/hyperlink",
                  "relationships/printerSettings"}),
        "printer settings type mismatch audit should be visible in planned output notes");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == relationship_reference_audits.size(),
        "planned output should mirror structured worksheet relationship reference audits");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "planned output should keep structured missing relationship-id audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "pageSetup",
              "rIdPrinterMissing", printer_settings_relationship_type, {},
              "do not contain that id"),
        "planned output should keep structured missing printer settings audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "planned output should keep structured unreferenced relationship-id audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "planned output should keep structured relationship type mismatch audit");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "pageSetup", "rId2",
              printer_settings_relationship_type, hyperlink_relationship_type, "expected"),
        "planned output should keep structured printer settings type mismatch audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "relationship-id audit output should write the replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "relationship-id audit output should preserve worksheet relationships byte-for-byte");
    const fastxlsx::detail::RelationshipSet* worksheet_relationships =
        output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "relationship-id audit output should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "relationship-id audit output should not prune unreferenced preserved relationships");
    const fastxlsx::detail::Relationship* r_id2 =
        worksheet_relationships->find_by_id("rId2");
    check(r_id2 != nullptr && r_id2->type.find("relationships/hyperlink") != std::string::npos,
        "relationship type mismatch audit output should not rewrite preserved relationship types");
    check(worksheet_relationships->find_by_id("rIdMissing") == nullptr,
        "relationship-id audit output should not synthesize missing relationships");
    check(worksheet_relationships->find_by_id("rIdPrinterMissing") == nullptr,
        "relationship-id audit output should not synthesize printer settings relationships");
}

void test_package_editor_worksheet_relationship_id_audit_respects_relationship_namespace()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-worksheet-rid-namespace-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-rid-namespace-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" )"
        R"(xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships" )"
        R"(xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing x:id="rId2"/>)"
        R"(<hyperlinks><hyperlink ref="A1" x:id="rId2"/></hyperlinks>)"
        R"(<drawing rel:id="rId1"/>)"
        R"(<drawing r:id="rIdMissing"/>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    static constexpr std::string_view hyperlink_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    const auto has_reference_audit = [&](const auto& audits,
                                         WorksheetReferenceAuditKind kind,
                                         std::string_view element,
                                         std::string_view relationship_id,
                                         std::string_view expected_type,
                                         std::string_view actual_type,
                                         std::string_view note_text) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part && audit.kind == kind
                    && audit.element == element && audit.relationship_id == relationship_id
                    && audit.expected_relationship_type == expected_type
                    && audit.actual_relationship_type == actual_type
                    && audit.note.find(note_text) != std::string::npos;
            });
    };

    const auto& relationship_reference_audits =
        editor.edit_plan().worksheet_relationship_reference_audits();
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "namespace-aware relationship-id audit should still record missing r:id references");
    check(has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId2", {},
              hyperlink_relationship_type, "unreferenced"),
        "wrong-namespace x:id should not mark preserved hyperlink relationship ids as referenced");
    check(!has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::TypeMismatch, "drawing", "rId2",
              drawing_relationship_type, hyperlink_relationship_type, "expected"),
        "wrong-namespace x:id should not produce relationship type mismatch audits");
    check(!has_reference_audit(relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId1", {},
              drawing_relationship_type, "unreferenced"),
        "alternate relationship namespace prefix should mark drawing relationship ids as referenced");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"relationship id rId2", "<drawing>", "relationships/hyperlink",
                  "relationships/drawing"}),
        "wrong-namespace x:id should not be described as a drawing relationship reference");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.worksheet_relationship_reference_audits.size()
            == relationship_reference_audits.size(),
        "planned output should mirror namespace-aware worksheet relationship audits");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::MissingRelationshipId, "drawing", "rIdMissing",
              drawing_relationship_type, {}, "do not contain that id"),
        "planned output should keep missing r:id audit after namespace filtering");
    check(has_reference_audit(output_plan.worksheet_relationship_reference_audits,
              WorksheetReferenceAuditKind::UnreferencedRelationshipId, {}, "rId2", {},
              hyperlink_relationship_type, "unreferenced"),
        "planned output should keep wrong-namespace x:id unreferenced audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "namespace-aware relationship-id audit output should write replacement worksheet XML");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "namespace-aware relationship-id audit output should preserve worksheet relationships");
    const fastxlsx::detail::RelationshipSet* worksheet_relationships =
        output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "namespace-aware relationship-id audit output should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdMissing") == nullptr,
        "namespace-aware relationship-id audit output should not synthesize missing relationships");
}

void test_package_editor_worksheet_replacement_audits_missing_relationships_entry()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-worksheet-missing-rels-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-missing-rels-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(</worksheet>)";

    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet);

    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "relationship id rId1", "<drawing>",
                  "source worksheet relationships are missing", "repair worksheet .rels"}),
        "worksheet replacement should audit missing worksheet relationships entry");

    using WorksheetReferenceAuditKind =
        fastxlsx::detail::WorksheetRelationshipReferenceAuditKind;
    static constexpr std::string_view drawing_relationship_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    const auto has_missing_relationships_audit = [&](const auto& audits) {
        return std::any_of(audits.begin(), audits.end(),
            [&](const fastxlsx::detail::WorksheetRelationshipReferenceAudit& audit) {
                return audit.worksheet_part == worksheet_part
                    && audit.kind == WorksheetReferenceAuditKind::MissingRelationships
                    && audit.element == "drawing" && audit.relationship_id == "rId1"
                    && audit.expected_relationship_type == drawing_relationship_type
                    && audit.actual_relationship_type.empty()
                    && audit.note.find("source worksheet relationships are missing")
                        != std::string::npos;
            });
    };
    check(has_missing_relationships_audit(
              editor.edit_plan().worksheet_relationship_reference_audits()),
        "worksheet replacement should record structured missing relationships-entry audit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(has_note_containing(output_plan.notes,
              {"relationship id rId1", "<drawing>",
                  "source worksheet relationships are missing"}),
        "missing worksheet relationships audit should be visible in planned output notes");
    check(has_missing_relationships_audit(
              output_plan.worksheet_relationship_reference_audits),
        "planned output should keep structured missing relationships-entry audit");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_worksheet,
        "missing relationships-entry audit output should write replacement worksheet XML");
    check(output_reader.find_entry("xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "missing relationships-entry audit output should not synthesize worksheet relationships");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "missing relationships-entry audit output should keep relationship graph without worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing relationships-entry audit output should preserve unknown bytes");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "missing relationships-entry audit output should keep stale calcChain omitted");
}

void test_package_editor_reference_policy_fail_blocks_missing_relationship_references_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-missing-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-missing-rels-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(</worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "relationship references blocked by reference policy",
            "missing worksheet relationship references should name reference policy");
    }
    check(failed,
        "PackageEditor should fail missing worksheet relationships when reference policy is Fail");

    check(editor.edit_plan().size() == initial_plan_size,
        "missing relationship policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing relationship policy failure should not add audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing relationship policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing relationship policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "missing relationship policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "missing relationship policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "missing relationship policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing relationship policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "missing relationship policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "missing relationship policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing relationship policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.find_entry("xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "missing relationship policy failure should not synthesize worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "missing relationship policy failure output should preserve unknown bytes");
}

void test_package_editor_reference_policy_fail_blocks_payload_dependencies_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-payload-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-payload-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="s" s="1"><f>SUM(B1:C1)</f><v>0</v></c></row></sheetData><scenarios><scenario name="Blocked"/></scenarios></worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "payload dependencies blocked by reference policy",
            "payload dependency policy failure should name reference policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject worksheet payload-only dependencies");

    check(editor.edit_plan().size() == initial_plan_size,
        "payload policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "payload policy failure should not add audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes"}),
        "payload policy failure should not leak sharedStrings audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references"}),
        "payload policy failure should not leak styles audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas"}),
        "payload policy failure should not leak formula audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "scenario metadata"}),
        "payload policy failure should not leak worksheet metadata audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "payload policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "payload policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "payload policy failure should not add worksheet payload dependency audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "payload policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "payload policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "payload policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "payload policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "payload policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "payload policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "payload policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "payload policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "payload policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "payload policy failure output should preserve unknown bytes");
}

void test_package_editor_reference_policy_fail_blocks_sheet_data_payload_dependencies_without_state_changes()
{
    const CalcSourcePackage source = write_calc_source_package(
        "fastxlsx-package-editor-policy-fail-sheetdata-payload-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-sheetdata-payload-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" t="s" s="1"><f>SUM(B1:C1)</f><v>0</v></c></row></sheetData>)";

    bool failed = false;
    try {
        replace_worksheet_sheet_data_from_single_chunk_source(editor,
            worksheet_part, replacement_sheet_data, fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "payload dependencies blocked by reference policy",
            "sheetData payload policy failure should name reference policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject sheetData payload-only dependencies");

    check(editor.edit_plan().size() == initial_plan_size,
        "sheetData payload policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "sheetData payload policy failure should not add audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "shared string indexes"}),
        "sheetData payload policy failure should not leak sharedStrings audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "style id references"}),
        "sheetData payload policy failure should not leak styles audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas"}),
        "sheetData payload policy failure should not leak formula audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "sheetData payload policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "sheetData payload policy failure should not add worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "sheetData payload policy failure should not add worksheet payload dependency audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "sheetData payload policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "sheetData payload policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "sheetData payload policy failure should not record removed package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "sheetData payload policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "sheetData payload policy failure should not change calcChain action");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "sheetData payload policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave workbook copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sheetData payload policy failure should leave calcChain copy-original");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "sheetData payload policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "sheetData payload policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "sheetData payload policy failure output should preserve unknown bytes");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "sheetdata-catalog-audits")) {
            throw TestFailure("wrong shard for sheetdata-catalog-audits executable");
        }

        test_package_editor_rejects_invalid_worksheet_replacement_xml_without_state_changes();
        test_package_editor_replaces_worksheet_with_payload_audit_notes();
        test_package_editor_worksheet_replacement_audits_relationship_id_mismatch();
        test_package_editor_worksheet_relationship_id_audit_respects_relationship_namespace();
        test_package_editor_worksheet_replacement_audits_missing_relationships_entry();
        test_package_editor_reference_policy_fail_blocks_missing_relationship_references_without_state_changes();
        test_package_editor_reference_policy_fail_blocks_payload_dependencies_without_state_changes();
        test_package_editor_reference_policy_fail_blocks_sheet_data_payload_dependencies_without_state_changes();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
