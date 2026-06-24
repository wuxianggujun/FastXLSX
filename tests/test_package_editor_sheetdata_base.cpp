#include "test_package_editor_sheetdata_common.hpp"

void test_package_editor_replaces_worksheet_sheet_data_and_preserves_metadata()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package("fastxlsx-package-editor-sheetdata-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName vml_drawing_part("/xl/drawings/vmlDrawing1.vml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="1"><f>SUM(A1:A1)</f><v>777</v></c></row><row r="2"/></sheetData>)";
    replace_worksheet_sheet_data_from_single_chunk_source(editor, worksheet_part, replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheetData replacement should keep worksheet in the edit plan");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should plan worksheet as local-DOM-rewrite");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "sheetData replacement reason should disclose the bounded local rewrite helper");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "sheetData replacement should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should plan workbook calc metadata as local-DOM-rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheetData replacement should request full calculation on load");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "sheetData replacement should keep the default calcChain remove policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sheet property metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sheet calculation metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet calculation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "sheetData replacement should audit preserved dimension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "view metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "custom view metadata", "caller review"}),
        "sheetData replacement should audit preserved custom view metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "default format metadata", "caller review"}),
        "sheetData replacement should audit preserved default format metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "column metadata", "caller review"}),
        "sheetData replacement should audit preserved column metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "protection metadata", "caller review"}),
        "sheetData replacement should audit preserved sheet protection metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "protected-range metadata", "caller review"}),
        "sheetData replacement should audit preserved protected range metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "sort-state metadata", "caller review"}),
        "sheetData replacement should audit preserved sort-state metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "autoFilter metadata", "caller review"}),
        "sheetData replacement should audit preserved autoFilter metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "merged-cell metadata", "caller review"}),
        "sheetData replacement should audit preserved merged-cell metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "scenario metadata", "caller review"}),
        "sheetData replacement should audit preserved scenario metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "data consolidation metadata", "caller review"}),
        "sheetData replacement should audit preserved data consolidation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "custom property metadata", "caller review"}),
        "sheetData replacement should audit preserved custom property metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "cell watch metadata", "caller review"}),
        "sheetData replacement should audit preserved cell watch metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "smart tag metadata", "caller review"}),
        "sheetData replacement should audit preserved smart tag metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "web publishing metadata", "caller review"}),
        "sheetData replacement should audit preserved web publishing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "conditional formatting metadata", "caller review"}),
        "sheetData replacement should audit preserved conditional formatting metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "data validation metadata", "caller review"}),
        "sheetData replacement should audit preserved data validation metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "hyperlink metadata", "caller review"}),
        "sheetData replacement should audit preserved hyperlink metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "ignored-error metadata", "caller review"}),
        "sheetData replacement should audit preserved ignored-error metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "print options metadata", "caller review"}),
        "sheetData replacement should audit preserved print options metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page margins metadata", "caller review"}),
        "sheetData replacement should audit preserved page margins metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "sheetData replacement should audit preserved page setup metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer metadata", "caller review"}),
        "sheetData replacement should audit preserved header/footer metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "row break metadata", "caller review"}),
        "sheetData replacement should audit preserved row break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "column break metadata", "caller review"}),
        "sheetData replacement should audit preserved column break metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "phonetic metadata", "caller review"}),
        "sheetData replacement should audit preserved phonetic metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "drawing reference metadata", "caller review"}),
        "sheetData replacement should audit preserved drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "legacy drawing reference metadata", "caller review"}),
        "sheetData replacement should audit preserved legacy drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "background picture reference metadata", "caller review"}),
        "sheetData replacement should audit preserved background picture metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "header/footer drawing reference metadata",
                  "caller review"}),
        "sheetData replacement should audit preserved header/footer drawing metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "OLE object reference metadata", "caller review"}),
        "sheetData replacement should audit preserved OLE object metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "control reference metadata", "caller review"}),
        "sheetData replacement should audit preserved control metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "table reference metadata", "caller review"}),
        "sheetData replacement should audit preserved table metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "extension metadata", "caller review"}),
        "sheetData replacement should audit preserved worksheet extension metadata");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData replacement should audit replacement shared string references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "style id references", "xl/styles.xml"}),
        "sheetData replacement should audit replacement style id references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "sheetData replacement should audit replacement formula references");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "sheetData replacement should audit that the current helper is bounded local rewrite");
    using PayloadAuditKind =
        fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope =
        fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    const auto& payload_audits =
        editor.edit_plan().worksheet_payload_dependency_audits();
    struct ExpectedPreservedPayloadAudit {
        PayloadAuditKind kind;
        std::string_view element;
        std::vector<std::string_view> note_needles;
    };
    const auto has_expected_preserved_payload_audit =
        [&](const auto& audits, const ExpectedPreservedPayloadAudit& expected) {
            for (const fastxlsx::detail::WorksheetPayloadDependencyAudit& audit : audits) {
                if (audit.worksheet_part != worksheet_part || audit.kind != expected.kind
                    || audit.scope != PayloadAuditScope::PreservedWorksheetMetadata
                    || audit.element != expected.element) {
                    continue;
                }

                bool matched = true;
                for (std::string_view needle : expected.note_needles) {
                    if (audit.note.find(needle) == std::string::npos) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return true;
                }
            }
            return false;
        };
    const std::vector<ExpectedPreservedPayloadAudit> preserved_metadata_audits = {
        {PayloadAuditKind::RangeMetadata, "sheetPr",
            {"sheet property metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetCalcPr",
            {"sheet calculation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dimension",
            {"dimension metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetViews",
            {"view metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "customSheetViews",
            {"custom view metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetFormatPr",
            {"default format metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "cols",
            {"column metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sheetProtection",
            {"protection metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "protectedRanges",
            {"protected-range metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "sortState",
            {"sort-state metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "autoFilter",
            {"autoFilter metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "mergeCells",
            {"merged-cell metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "scenarios",
            {"scenario metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dataConsolidate",
            {"data consolidation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "customProperties",
            {"custom property metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "cellWatches",
            {"cell watch metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "smartTags",
            {"smart tag metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "webPublishItems",
            {"web publishing metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "hyperlinks",
            {"hyperlink metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "dataValidations",
            {"data validation metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "conditionalFormatting",
            {"conditional formatting metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "ignoredErrors",
            {"ignored-error metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "printOptions",
            {"print options metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "pageMargins",
            {"page margins metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "pageSetup",
            {"page setup metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "headerFooter",
            {"header/footer metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "rowBreaks",
            {"row break metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "colBreaks",
            {"column break metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "phoneticPr",
            {"phonetic metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "drawing",
            {"drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "legacyDrawing",
            {"legacy drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "picture",
            {"background picture reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "legacyDrawingHF",
            {"header/footer drawing reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "oleObjects",
            {"OLE object reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "controls",
            {"control reference metadata", "caller review"}},
        {PayloadAuditKind::RelationshipMetadata, "tableParts",
            {"table reference metadata", "caller review"}},
        {PayloadAuditKind::RangeMetadata, "extLst",
            {"extension metadata", "caller review"}},
    };
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData replacement should record structured sharedStrings payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "sheetData replacement should record structured styles payload audit");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "sheetData replacement should record structured formula payload audit");
    for (const ExpectedPreservedPayloadAudit& expected : preserved_metadata_audits) {
        check(has_expected_preserved_payload_audit(payload_audits, expected),
            "sheetData replacement should record structured preserved worksheet metadata audits");
    }
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should mirror worksheet local-DOM-rewrite in the manifest");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheetData replacement should mirror workbook metadata rewrite in the manifest");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "sheetData replacement should remove calcChain from the output manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "sheetData output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "sheetData output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "sheetData output plan should snapshot structured relationship audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == payload_audits.size(),
        "sheetData output plan should mirror structured payload dependency audits");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData output plan should keep structured sharedStrings payload audit");
    for (const ExpectedPreservedPayloadAudit& expected : preserved_metadata_audits) {
        check(has_expected_preserved_payload_audit(
                  output_plan.worksheet_payload_dependency_audits, expected),
            "sheetData output plan should keep structured preserved worksheet metadata audits");
    }
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "dimension metadata", "caller review"}),
        "sheetData output plan should snapshot preserved metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "sheet calculation metadata", "caller review"}),
        "sheetData output plan should snapshot preserved sheet calculation metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "column metadata", "caller review"}),
        "sheetData output plan should snapshot preserved column metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "sort-state metadata", "caller review"}),
        "sheetData output plan should snapshot preserved sort-state metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "page setup metadata", "caller review"}),
        "sheetData output plan should snapshot preserved page setup metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "header/footer metadata", "caller review"}),
        "sheetData output plan should snapshot preserved header/footer metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "legacy drawing reference metadata",
                  "caller review"}),
        "sheetData output plan should snapshot preserved legacy drawing metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "background picture reference metadata",
                  "caller review"}),
        "sheetData output plan should snapshot preserved picture metadata notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "shared string indexes", "xl/sharedStrings.xml"}),
        "sheetData output plan should snapshot sharedStrings review notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "style id references", "xl/styles.xml"}),
        "sheetData output plan should snapshot styles review notes");
    check(has_note_containing(output_plan.notes,
              {"sheetData replacement", "contains formulas", "calcChain policy"}),
        "sheetData output plan should snapshot formula review notes");
    check(has_note_containing(output_plan.notes,
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "sheetData output plan should snapshot bounded local rewrite boundary notes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should local-DOM-rewrite worksheet");
    const auto* output_worksheet_entry_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml");
    check(output_worksheet_entry_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "sheetData output plan should disclose bounded local worksheet rewrite in the reason");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "sheetData output plan should classify worksheet as package part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should local-DOM-rewrite workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "sheetData output plan should classify workbook as package part");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "sheetData output plan should classify content types as metadata entry");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "sheetData output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheetData output plan should rewrite workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels", false,
        "",
        "sheetData output plan should classify workbook relationships as metadata entry");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sheetData output plan should classify workbook source relationships");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "sheetData output plan should keep workbook owner context");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        false, "",
        "sheetData output plan should classify worksheet relationships as metadata entry");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "sheetData output plan should classify worksheet source relationships");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "sheetData output plan should keep worksheet owner context");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "sheetData output plan should omit stale calcChain");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        calc_chain_part.value(),
        "sheetData output plan should keep omitted calcChain as package part");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve drawing");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/drawing1.xml",
        worksheet_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "../drawings/drawing1.xml",
        "sheetData output plan should keep drawing relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve legacy drawing VML");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        true, vml_drawing_part.value(),
        "sheetData output plan should classify legacy drawing VML as package part");
    check_output_entry_relationship_context(output_plan.entries, "xl/drawings/vmlDrawing1.vml",
        worksheet_part.value(), "rId7",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
        "../drawings/vmlDrawing1.vml#shape1",
        "sheetData output plan should keep legacy drawing relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/media/image1.png",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve image media");
    check_output_entry_relationship_context(output_plan.entries, "xl/media/image1.png",
        drawing_part.value(), "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "../media/image1.png",
        "sheetData output plan should keep image relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/tables/table1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve table");
    check_output_entry_relationship_context(output_plan.entries, "xl/tables/table1.xml",
        worksheet_part.value(), "rId3",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
        "../tables/table1.xml",
        "sheetData output plan should keep table relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve sharedStrings");
    check_output_entry_relationship_context(output_plan.entries, "xl/sharedStrings.xml", "",
        "", "", "",
        "sheetData output plan should not invent sharedStrings relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve styles");
    check_output_entry_relationship_context(output_plan.entries, "xl/styles.xml", "", "",
        "", "",
        "sheetData output plan should not invent styles relationship audit");
    check_output_entry_plan(output_plan.entries, "xl/vbaProject.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve VBA");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheetData output plan should preserve unknown extension");
    check_output_entry_relationship_context(output_plan.entries, "custom/opaque-extension.bin",
        worksheet_part.value(), "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin",
        "sheetData output plan should keep unknown extension relationship audit");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheetData replacement output should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string expected_worksheet =
        std::string(
            R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
            R"(<sheetPr filterMode="1"/>)"
            R"(<sheetCalcPr fullCalcOnLoad="1"/>)"
            R"(<dimension ref="A1:B2"/>)"
            R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
            R"(<customSheetViews><customSheetView guid="{11111111-2222-3333-4444-555555555555}"/></customSheetViews>)"
            R"(<sheetFormatPr defaultRowHeight="15"/>)"
            R"(<cols><col min="1" max="1" width="12" customWidth="1"/></cols>)")
        + replacement_sheet_data
        + R"(<sheetProtection sheet="1" objects="1" scenarios="1"/>)"
          R"(<protectedRanges><protectedRange name="Inputs" sqref="A1:B2"/></protectedRanges>)"
          R"(<sortState ref="A1:B2"><sortCondition ref="A1:A2"/></sortState>)"
          R"(<autoFilter ref="A1:B2"/>)"
          R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
          R"(<scenarios><scenario name="Base" user="FastXLSX"/></scenarios>)"
          R"(<dataConsolidate function="sum" ref3D="1"><dataRefs count="1"><dataRef ref="A1:B2" sheet="Sheet1"/></dataRefs></dataConsolidate>)"
          R"(<customProperties><customPr name="FastXLSX"/></customProperties>)"
          R"(<cellWatches><cellWatch r="A1"/></cellWatches>)"
          R"(<smartTags><cellSmartTags r="A1"><cellSmartTag type="urn:fastxlsx:smart"/></cellSmartTags></smartTags>)"
          R"(<webPublishItems count="1"><webPublishItem id="1" divId="FastXLSX" sourceType="sheet" sourceRef="Sheet1!A1:B2"/></webPublishItems>)"
          R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
          R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
          R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
          R"(<printOptions horizontalCentered="1"/>)"
          R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
          R"(<pageSetup orientation="landscape"/>)"
          R"(<headerFooter><oddHeader>&amp;LFastXLSX</oddHeader></headerFooter>)"
          R"(<rowBreaks count="1" manualBreakCount="1"><brk id="2" max="16383" man="1"/></rowBreaks>)"
          R"(<colBreaks count="1" manualBreakCount="1"><brk id="1" max="1048575" man="1"/></colBreaks>)"
          R"(<phoneticPr fontId="1" type="noConversion"/>)"
          R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
          R"(<drawing r:id="rId1"/>)"
          R"(<legacyDrawing r:id="rId7"/>)"
          R"(<picture r:id="rId1"/>)"
          R"(<legacyDrawingHF r:id="rId7"/>)"
          R"(<oleObjects><oleObject progId="Forms.CommandButton.1" r:id="rId1"/></oleObjects>)"
          R"(<controls><control shapeId="1" r:id="rId1"/></controls>)"
          R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
          R"(<extLst><ext uri="{fastxlsx-test}"><fx:opaque xmlns:fx="urn:fastxlsx:test"/></ext></extLst>)"
          R"(</worksheet>)";
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == expected_worksheet,
        "sheetData replacement should preserve worksheet metadata around sheetData");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), R"(<v>1</v>)",
        "sheetData replacement should remove the old sheetData rows");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "sheetData replacement should preserve worksheet relationships bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "sheetData replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rId1") != nullptr,
        "sheetData replacement should keep drawing relationship readable");
    check(worksheet_relationships->find_by_id("rId2") != nullptr,
        "sheetData replacement should keep hyperlink relationship readable");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "sheetData replacement should keep table relationship readable");
    const auto* legacy_drawing_relationship = worksheet_relationships->find_by_id("rId7");
    check(legacy_drawing_relationship != nullptr,
        "sheetData replacement should keep legacy drawing relationship readable");
    check(legacy_drawing_relationship->target == "../drawings/vmlDrawing1.vml#shape1",
        "sheetData replacement should preserve legacy drawing relationship target");
    check(legacy_drawing_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "sheetData replacement should keep legacy drawing relationship internal");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    const auto* graph_worksheet_relationships =
        output_graph.relationships_for(worksheet_part);
    check(graph_worksheet_relationships != nullptr,
        "sheetData replacement should keep worksheet relationships in graph");
    const auto* graph_legacy_drawing_relationship =
        graph_worksheet_relationships->find_by_id("rId7");
    check(graph_legacy_drawing_relationship != nullptr,
        "sheetData replacement graph should keep legacy drawing relationship");
    check(graph_legacy_drawing_relationship->target
            == "../drawings/vmlDrawing1.vml#shape1",
        "sheetData replacement graph should preserve legacy drawing target");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "sheetData replacement should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "sheetData replacement should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "sheetData replacement should preserve legacy drawing bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "sheetData replacement should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "sheetData replacement should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "sheetData replacement should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "sheetData replacement should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "sheetData replacement should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "sheetData replacement should remove calcChain content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "sheetData replacement should preserve table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "sheetData replacement should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "sheetData replacement should preserve styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "sheetData replacement should preserve VBA content type override");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheetData replacement should keep unknown extension on default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "sheetData replacement should not promote media defaults to overrides");

    const std::string workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_not_contains(workbook_relationships, "relationships/calcChain",
        "sheetData replacement should remove calcChain workbook relationship");
    check_contains(workbook_relationships, "relationships/sharedStrings",
        "sheetData replacement should preserve sharedStrings workbook relationship");
    check_contains(workbook_relationships, "relationships/styles",
        "sheetData replacement should preserve styles workbook relationship");
    check_contains(workbook_relationships, "relationships/vbaProject",
        "sheetData replacement should preserve VBA workbook relationship");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml,
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "sheetData replacement should preserve workbook defined names");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "sheetData replacement should request full calculation in workbook XML");
}

void test_package_editor_sheet_data_patch_without_calc_chain_keeps_relationship_metadata_copy_original()
{
    SourcePackage source;
    source.path = output_path("fastxlsx-package-editor-sheetdata-no-calcchain-source.xlsx");
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
        R"(<Relationship Id="rIdWorkbook" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdSheet" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rIdSheet"/></sheets>)"
        R"(</workbook>)";
    source.worksheet =
        R"(<worksheet><dimension ref="A1:A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    source.unknown = std::string("opaque\0bytes", 12);
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
        output_path("fastxlsx-package-editor-sheetdata-no-calcchain-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="2"><c r="A2"><v>2</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "no-calcChain sheetData patch should not record calcChain removal");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "no-calcChain sheetData patch should not rewrite content types");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "no-calcChain sheetData patch should not rewrite package relationships");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") != nullptr,
        "no-calcChain sheetData patch should audit preserved workbook relationships");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "no-calcChain sheetData patch should not invent worksheet relationships audit");
    check(editor.edit_plan().full_calculation_on_load(),
        "no-calcChain sheetData patch should still request workbook recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "no-calcChain sheetData patch should keep default calcChain action");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "no-calcChain sheetData patch should local-DOM-rewrite worksheet");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "no-calcChain sheetData patch should local-DOM-rewrite workbook calc metadata");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "no-calcChain sheetData patch should not add calcChain to manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "no-calcChain sheetData output plan should rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "no-calcChain sheetData output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "no-calcChain sheetData output plan should preserve workbook relationships");
    check(find_output_entry_plan(
              output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels") == nullptr,
        "no-calcChain sheetData output plan should not create worksheet relationships");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "no-calcChain sheetData output plan should not create calcChain output");
    check(output_plan.removed_parts.empty(),
        "no-calcChain sheetData output plan should not omit parts");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "no-calcChain sheetData output should not create calcChain");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "no-calcChain sheetData output should not create worksheet relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "no-calcChain sheetData output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "no-calcChain sheetData output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "no-calcChain sheetData output should preserve workbook relationships bytes");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "no-calcChain sheetData output should keep worksheet relationships absent");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "no-calcChain sheetData output should keep calcChain content type absent");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "no-calcChain sheetData output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "no-calcChain sheetData output should remove old sheetData rows");
    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "no-calcChain sheetData output should request full calculation");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "no-calcChain sheetData output should preserve unknown bytes");
}

void test_package_editor_patches_fastxlsx_writer_sheet_data_roundtrip()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-writer-roundtrip-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-roundtrip-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto editable = workbook.add_worksheet("Patch Source");
        auto untouched = workbook.add_worksheet("Untouched");

        editable.append_row({
            fastxlsx::CellView::text("old text").with_style(text_style),
            fastxlsx::CellView::number(7.0),
        });
        editable.append_row({
            fastxlsx::CellView::boolean(true),
        });

        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
            fastxlsx::CellView::number(99.0),
        });

        workbook.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source_path);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "writer source should not contain calcChain");
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "writer source should contain shared strings");
    check(source_entries.find("xl/styles.xml") != source_entries.end(),
        "writer source should contain styles");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const std::vector<fastxlsx::detail::WorkbookSheetReference> source_sheets =
        source_reader.workbook_sheets();
    check(source_sheets.size() == 2,
        "PackageReader should resolve workbook sheets from a FastXLSX writer package");

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");

    check(source_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "PackageReader should locate the editable writer sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "PackageReader should locate the untouched writer sheet by name");

    const std::string editable_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet1.xml");
    const std::size_t writer_prolog_end = editable_sheet_before.find("<worksheet");
    check(writer_prolog_end != std::string::npos && writer_prolog_end > 0,
        "writer source worksheet should have an XML declaration/prolog before the root element");
    const std::string writer_prolog = editable_sheet_before.substr(0, writer_prolog_end);
    check_contains(writer_prolog, "<?xml",
        "writer source worksheet prolog should include the XML declaration");
    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");
    const std::string core_properties_before =
        source_reader.read_entry("docProps/core.xml");
    const std::string app_properties_before =
        source_reader.read_entry("docProps/app.xml");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="s"><v>0</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Patch Source", replacement_sheet_data);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    using WorkbookAuditKind = fastxlsx::detail::WorkbookPayloadDependencyAuditKind;
    using WorkbookAuditScope = fastxlsx::detail::WorkbookPayloadDependencyAuditScope;

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "writer roundtrip sheetData replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer roundtrip sheetData replacement should plan worksheet local-DOM rewrite");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "writer roundtrip sheetData replacement should disclose bounded local rewrite");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "writer roundtrip sheetData replacement should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer roundtrip sheetData replacement should rewrite workbook as small XML");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "writer roundtrip sheetData replacement should not invent a removed calcChain payload");
    check(editor.edit_plan().full_calculation_on_load(),
        "writer roundtrip sheetData replacement should request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Remove,
        "writer roundtrip sheetData replacement should keep default calcChain remove policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "writer roundtrip sheetData replacement should audit bounded local rewrite scope");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "writer roundtrip sheetData replacement should audit sharedStrings references");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "writer roundtrip sheetData replacement should audit style references");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "writer roundtrip sheetData replacement should audit workbook calc metadata");
    check(has_workbook_payload_audit(editor.edit_plan().workbook_payload_dependency_audits(),
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "writer roundtrip sheetData replacement should audit workbook definedNames review");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "writer roundtrip output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "writer roundtrip output plan should carry calcChain remove policy");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer roundtrip output plan should local-DOM-rewrite the target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer roundtrip output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve untouched writer worksheet");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep content types unchanged");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep package relationships unchanged");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should keep workbook relationships unchanged");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer roundtrip output plan should preserve styles");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "writer roundtrip output plan should not create calcChain");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "writer roundtrip output plan should keep sharedStrings payload audit");
    check(has_payload_audit(output_plan.worksheet_payload_dependency_audits,
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "writer roundtrip output plan should keep styles payload audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::CalcMetadata,
              WorkbookAuditScope::WorksheetRewrite, "calcPr",
              {"worksheet rewrite", "calcPr"}),
        "writer roundtrip output plan should keep workbook calc metadata audit");
    check(has_workbook_payload_audit(output_plan.workbook_payload_dependency_audits,
              workbook_part, WorkbookAuditKind::DefinedNames,
              WorkbookAuditScope::WorksheetRewrite, "definedNames",
              {"worksheet rewrite", "definedNames"}),
        "writer roundtrip output plan should keep workbook definedNames audit");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "writer roundtrip output should not contain calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "writer roundtrip output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "writer roundtrip output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml.rfind(writer_prolog, 0) == 0,
        "writer roundtrip output should preserve the worksheet XML declaration/prolog");
    check(worksheet_xml.find("<worksheet") == writer_prolog.size(),
        "writer roundtrip output should keep the worksheet root immediately after the prolog");
    check_contains(worksheet_xml, replacement_sheet_data,
        "writer roundtrip output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "writer roundtrip output should remove old second cell");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "writer roundtrip output should remove old second row");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "writer roundtrip output should request full recalculation in workbook XML");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "writer roundtrip output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "writer roundtrip output should preserve styles content type override");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/sharedStrings",
        "writer roundtrip output should preserve sharedStrings workbook relationship");
    check_contains(output_reader.read_entry("xl/_rels/workbook.xml.rels"),
        "relationships/styles",
        "writer roundtrip output should preserve styles workbook relationship");
    check_entry_bytes(output_reader, "docProps/core.xml", core_properties_before);
    check_entry_bytes(output_reader, "docProps/app.xml", app_properties_before);
}

void test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-template-fill-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-template-fill-output.xlsx");

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto template_sheet = workbook.add_worksheet("Template Fill");
        auto untouched = workbook.add_worksheet("Untouched");

        template_sheet.append_row({
            fastxlsx::CellView::text("{{customer}}").with_style(text_style),
            fastxlsx::CellView::text("{{total}}").with_style(text_style),
        });
        template_sheet.append_row({
            fastxlsx::CellView::text("{{notes}}").with_style(text_style),
        });
        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
        });

        workbook.close();
    }

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");

    check(source_reader.worksheet_part_by_sheet_name("Template Fill") == worksheet_part,
        "template-fill fixture should locate the editable sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "template-fill fixture should locate the untouched sheet by name");
    check(source_reader.find_entry("xl/calcChain.xml") == nullptr,
        "template-fill source should not contain calcChain");

    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");

    check_contains(shared_strings_before, "{{customer}}",
        "template-fill source should keep placeholder text in sharedStrings");
    check_contains(shared_strings_before, "{{notes}}",
        "template-fill source should keep all placeholders in sharedStrings");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="inlineStr"><is><t>Acme Corp</t></is></c><c r="B1"><v>1234</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Template Fill", replacement_sheet_data);

    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "template-fill patch should resolve the target worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "template-fill patch should use the current bounded local helper");
    check(worksheet_plan->reason.find("bounded local sheetData replacement")
            != std::string::npos,
        "template-fill patch should disclose the bounded local rewrite reason");

    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "template-fill patch should plan workbook calc metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "template-fill patch should rewrite workbook calc metadata as small XML");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "template-fill patch should not invent a removed calcChain payload");
    check(editor.edit_plan().full_calculation_on_load(),
        "template-fill patch should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"bounded local worksheet XML rewrite", "not the large-file streaming"}),
        "template-fill patch should audit that it is not the future streaming transformer");
    check(has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "template-fill patch should audit style references in replacement cells");
    check(!has_payload_audit(editor.edit_plan().worksheet_payload_dependency_audits(),
              worksheet_part, PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "inline-string template fill should not claim shared string index migration");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "template-fill output plan should request full calculation");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "template-fill output plan should local-DOM-rewrite the target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve the untouched worksheet");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve sharedStrings bytes");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "template-fill output plan should preserve styles bytes");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "template-fill output should not create calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Template Fill") == worksheet_part,
        "template-fill output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "template-fill output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "template-fill output should write caller-supplied replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "template-fill output should remove old placeholder rows from the target sheet");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "template-fill output should request workbook recalculation");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check_contains(output_reader.read_entry("xl/sharedStrings.xml"), "{{customer}}",
        "template-fill output should preserve old placeholder sharedStrings instead of pruning");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "template-fill output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "template-fill output should preserve styles content type override");
}

void test_package_editor_patches_writer_sheet_data_and_preserves_unknown_entry()
{
    const std::filesystem::path writer_source_path =
        output_path("fastxlsx-package-editor-writer-unknown-base.xlsx");
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-writer-unknown-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-unknown-output.xlsx");
    const std::string unknown_bytes = std::string("writer-opaque\0payload", 21);

    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        auto workbook = fastxlsx::WorkbookWriter::create(writer_source_path, options);
        const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
        auto editable = workbook.add_worksheet("Patch Source");
        auto untouched = workbook.add_worksheet("Untouched");

        editable.append_row({
            fastxlsx::CellView::text("old text").with_style(text_style),
            fastxlsx::CellView::number(7.0),
        });
        untouched.append_row({
            fastxlsx::CellView::text("keep me").with_style(text_style),
            fastxlsx::CellView::number(99.0),
        });

        workbook.close();
    }

    const fastxlsx::detail::PackageReader writer_reader =
        fastxlsx::detail::PackageReader::open(writer_source_path);
    std::string augmented_content_types =
        writer_reader.read_entry("[Content_Types].xml");
    check(augmented_content_types.find(R"(Default Extension="bin")")
            == std::string::npos,
        "writer package fixture should not already contain a bin default");
    const std::size_t content_types_close = augmented_content_types.rfind("</Types>");
    check(content_types_close != std::string::npos,
        "writer package fixture should contain a closing Types element");
    augmented_content_types.insert(content_types_close,
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)");

    std::vector<fastxlsx::detail::PackageEntry> augmented_entries;
    for (const fastxlsx::detail::PackageReaderEntry& entry : writer_reader.entries()) {
        std::string data = writer_reader.read_entry(entry.name);
        if (entry.name == "[Content_Types].xml") {
            data = augmented_content_types;
        }
        augmented_entries.emplace_back(entry.name, std::move(data));
    }
    augmented_entries.emplace_back("custom/opaque.bin", unknown_bytes);
    fastxlsx::detail::write_package(source_path, augmented_entries,
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName untouched_part("/xl/worksheets/sheet2.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(source_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "augmented writer package should locate the editable sheet by name");
    check(source_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "augmented writer package should locate the untouched sheet by name");
    check(source_reader.find_entry("xl/calcChain.xml") == nullptr,
        "augmented writer package should not contain calcChain");
    check(source_reader.content_types().default_for("bin") != nullptr,
        "augmented writer package should expose unknown bin content type default");
    check(source_reader.content_types().override_for(unknown_part) == nullptr,
        "augmented writer package should not promote unknown bin entry to override");

    const std::string editable_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet1.xml");
    const std::size_t writer_prolog_end = editable_sheet_before.find("<worksheet");
    check(writer_prolog_end != std::string::npos && writer_prolog_end > 0,
        "augmented writer source worksheet should preserve the XML declaration");
    const std::string writer_prolog = editable_sheet_before.substr(0, writer_prolog_end);
    const std::string untouched_sheet_before =
        source_reader.read_entry("xl/worksheets/sheet2.xml");
    const std::string content_types_before =
        source_reader.read_entry("[Content_Types].xml");
    const std::string package_relationships_before =
        source_reader.read_entry("_rels/.rels");
    const std::string workbook_relationships_before =
        source_reader.read_entry("xl/_rels/workbook.xml.rels");
    const std::string shared_strings_before =
        source_reader.read_entry("xl/sharedStrings.xml");
    const std::string styles_before =
        source_reader.read_entry("xl/styles.xml");
    const std::string core_properties_before =
        source_reader.read_entry("docProps/core.xml");
    const std::string app_properties_before =
        source_reader.read_entry("docProps/app.xml");
    check_entry_bytes(source_reader, "custom/opaque.bin", unknown_bytes);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const std::string replacement_sheet_data =
        R"(<sheetData><row r="1"><c r="A1" s="1" t="s"><v>0</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Patch Source", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer unknown sheetData patch should local-DOM-rewrite target worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer unknown sheetData patch should rewrite workbook calc metadata");
    const auto* unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(unknown_plan != nullptr
            && unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "writer unknown sheetData patch should keep unknown part copy-original");
    check(editor.edit_plan().find_removed_part(calc_chain_part) == nullptr,
        "writer unknown sheetData patch should not invent calcChain removal");
    check(editor.edit_plan().full_calculation_on_load(),
        "writer unknown sheetData patch should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer unknown output plan should local-DOM-rewrite target worksheet");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer unknown output plan should rewrite workbook calc metadata");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet2.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve untouched worksheet");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve sharedStrings");
    check_output_entry_plan(output_plan.entries, "xl/styles.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve styles");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer unknown output plan should preserve unknown entry");
    check(find_output_entry_plan(output_plan.entries, "xl/calcChain.xml") == nullptr,
        "writer unknown output plan should not create calcChain");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "writer unknown output should not contain calcChain");
    check(output_entries.find("custom/opaque.bin") != output_entries.end(),
        "writer unknown output should preserve the unknown entry");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Patch Source") == worksheet_part,
        "writer unknown output should keep editable sheet lookup readable");
    check(output_reader.worksheet_part_by_sheet_name("Untouched") == untouched_part,
        "writer unknown output should keep untouched sheet lookup readable");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check(worksheet_xml.rfind(writer_prolog, 0) == 0,
        "writer unknown output should preserve the worksheet XML declaration/prolog");
    check_contains(worksheet_xml, replacement_sheet_data,
        "writer unknown output should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "writer unknown output should remove the old target sheetData cells");
    check_contains(output_reader.read_entry("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "writer unknown output should request full recalculation in workbook XML");

    check_entry_bytes(output_reader, "xl/worksheets/sheet2.xml", untouched_sheet_before);
    check_entry_bytes(output_reader, "[Content_Types].xml", content_types_before);
    check_entry_bytes(output_reader, "_rels/.rels", package_relationships_before);
    check_entry_bytes(
        output_reader, "xl/_rels/workbook.xml.rels", workbook_relationships_before);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", shared_strings_before);
    check_entry_bytes(output_reader, "xl/styles.xml", styles_before);
    check_entry_bytes(output_reader, "docProps/core.xml", core_properties_before);
    check_entry_bytes(output_reader, "docProps/app.xml", app_properties_before);
    check_entry_bytes(output_reader, "custom/opaque.bin", unknown_bytes);
    check(output_reader.content_types().default_for("bin") != nullptr,
        "writer unknown output should preserve unknown bin content type default");
    check(output_reader.content_types().override_for(unknown_part) == nullptr,
        "writer unknown output should not promote unknown bin entry to override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "writer unknown output should preserve sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "writer unknown output should preserve styles content type override");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata")) {
            test_package_editor_replaces_worksheet_sheet_data_and_preserves_metadata();
            test_package_editor_sheet_data_patch_without_calc_chain_keeps_relationship_metadata_copy_original();
            test_package_editor_patches_fastxlsx_writer_sheet_data_roundtrip();
            test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch();
            test_package_editor_patches_writer_sheet_data_and_preserves_unknown_entry();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
