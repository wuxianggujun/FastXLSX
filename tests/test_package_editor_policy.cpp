#include "test_package_editor_policy_common.hpp"

void test_package_editor_repeated_worksheet_rewrite_upserts_relationship_target_audits()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-repeated-sheet-audit-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeated-sheet-audit-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string first_replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>201</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, first_replacement_sheet);

    const std::size_t first_note_count = editor.edit_plan().notes().size();
    const std::size_t first_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t first_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    check(first_note_count > 0,
        "first linked worksheet rewrite should record audit notes");
    check(first_relationship_target_audit_count > 0,
        "first linked worksheet rewrite should record relationship target audits");
    check(first_worksheet_relationship_reference_audit_count > 0,
        "first linked worksheet rewrite should record worksheet relationship reference audits");

    const std::string second_replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>202</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, second_replacement_sheet);

    check(editor.edit_plan().notes().size() == first_note_count,
        "repeated linked worksheet rewrite should not duplicate identical audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == first_relationship_target_audit_count,
        "repeated linked worksheet rewrite should upsert relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == first_worksheet_relationship_reference_audit_count,
        "repeated linked worksheet rewrite should upsert worksheet relationship reference audits");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated linked worksheet rewrite should keep worksheet stream-rewrite mode");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "repeated linked worksheet rewrite should keep stale calcChain removed-part audit");
    check(editor.manifest().find_part(calc_chain_part) == nullptr,
        "repeated linked worksheet rewrite should keep calcChain removed from manifest");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.worksheet_relationship_reference_audits.size()
            == first_worksheet_relationship_reference_audit_count,
        "repeated linked worksheet rewrite output plan should mirror upserted worksheet relationship reference audits");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == second_replacement_sheet,
        "repeated linked worksheet rewrite should write the latest worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated linked worksheet rewrite should still preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "repeated linked worksheet rewrite should still preserve unknown extension bytes");
    check(output_reader.find_entry("xl/calcChain.xml") == nullptr,
        "repeated linked worksheet rewrite output should keep stale calcChain omitted");
}

void test_package_editor_preserves_source_relationship_parts_when_replacement_omits_references()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-orphaned-rels-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-orphaned-rels-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");

    const std::string replacement_sheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>123</v></c></row></sheetData></worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* drawing_plan = editor.edit_plan().find_part(drawing_part);
    check(drawing_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan drawing copy-original");
    check(drawing_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve source drawing part");
    const auto* table_plan = editor.edit_plan().find_part(table_part);
    check(table_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan table copy-original");
    check(table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve source table part");
    const fastxlsx::detail::PartName worksheet_relationships_part(
        "/xl/worksheets/_rels/sheet1.xml.rels");
    const fastxlsx::detail::PartName drawing_relationships_part(
        "/xl/drawings/_rels/drawing1.xml.rels");
    const fastxlsx::detail::PartName shared_strings_relationships_part(
        "/xl/_rels/sharedStrings.xml.rels");
    const auto* worksheet_relationships_plan = editor.edit_plan().find_package_entry(
        "xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan worksheet relationship audit");
    check(worksheet_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve worksheet relationships part");
    check(worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify worksheet relationships as source relationships");
    check(worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "omitted-reference worksheet rewrite should keep worksheet relationships owner");
    const auto* drawing_relationships_plan = editor.edit_plan().find_package_entry(
        "xl/drawings/_rels/drawing1.xml.rels");
    check(drawing_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan drawing relationship audit");
    check(drawing_relationships_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve drawing relationships part");
    check(drawing_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify drawing relationships as source relationships");
    check(drawing_relationships_plan->owner_part == drawing_part.value(),
        "omitted-reference worksheet rewrite should keep drawing relationships owner");
    const auto* shared_strings_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/sharedStrings.xml.rels");
    check(shared_strings_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan sharedStrings relationship audit");
    check(shared_strings_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve sharedStrings relationships part");
    check(shared_strings_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify sharedStrings relationships as source relationships");
    check(shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "omitted-reference worksheet rewrite should keep sharedStrings relationships owner");
    const auto* opaque_extension_relationships_plan =
        editor.edit_plan().find_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(opaque_extension_relationships_plan != nullptr,
        "omitted-reference worksheet rewrite should still plan unknown extension relationship audit");
    check(opaque_extension_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "omitted-reference worksheet rewrite should preserve unknown extension relationships part");
    check(opaque_extension_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "omitted-reference worksheet rewrite should classify unknown extension relationships as source relationships");
    check(opaque_extension_relationships_plan->owner_part == opaque_extension_part.value(),
        "omitted-reference worksheet rewrite should keep unknown extension relationships owner");
    check(!editor.edit_plan().notes().empty(),
        "omitted-reference worksheet rewrite should record relationship preservation notes");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "omitted-reference worksheet rewrite should omit stale calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "omitted-reference worksheet rewrite should write the replacement sheet XML");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "<drawing",
        "omitted-reference replacement sheet should not contain drawing markup");
    check_not_contains(output_reader.read_entry("xl/worksheets/sheet1.xml"), "<tableParts",
        "omitted-reference replacement sheet should not contain tableParts markup");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "omitted-reference worksheet rewrite should byte-preserve source worksheet relationships");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "omitted-reference worksheet rewrite should preserve drawing XML bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "omitted-reference worksheet rewrite should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "omitted-reference worksheet rewrite should preserve table XML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "omitted-reference worksheet rewrite should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "omitted-reference worksheet rewrite should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "omitted-reference worksheet rewrite should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "omitted-reference worksheet rewrite should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "omitted-reference worksheet rewrite should byte-preserve unknown extension relationships");

    const auto* sheet_relationships = output_reader.relationships_for(worksheet_part);
    check(sheet_relationships != nullptr,
        "omitted-reference worksheet rewrite should keep source worksheet relationships readable");
    check(sheet_relationships->find_by_id("rId1") != nullptr,
        "omitted-reference worksheet rewrite should keep source drawing relationship");
    check(sheet_relationships->find_by_id("rId3") != nullptr,
        "omitted-reference worksheet rewrite should keep source table relationship");
    const auto* opaque_extension_relationships = output_reader.relationships_for(
        opaque_extension_part);
    check(opaque_extension_relationships != nullptr,
        "omitted-reference worksheet rewrite should keep unknown extension relationships readable");
    check(opaque_extension_relationships->find_by_id("rIdOpaqueExternal") != nullptr,
        "omitted-reference worksheet rewrite should keep unknown extension external relationship");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    const auto* output_worksheet_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve worksheet relationships part");
    check(output_worksheet_relationships_plan != nullptr
            && output_worksheet_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_worksheet_relationships_plan->owner_part
                == worksheet_part.value(),
        "omitted-reference worksheet rewrite output should keep worksheet relationships audit");
    const auto* output_drawing_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/drawings/_rels/drawing1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve drawing relationships part");
    check(output_drawing_relationships_plan != nullptr
            && output_drawing_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_drawing_relationships_plan->owner_part
                == drawing_part.value(),
        "omitted-reference worksheet rewrite output should keep drawing relationships audit");
    const auto* output_shared_strings_relationships_plan = find_output_entry_plan(
        output_plan.entries, "xl/_rels/sharedStrings.xml.rels");
    check_output_entry_plan(output_plan.entries, "xl/_rels/sharedStrings.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve sharedStrings relationships part");
    check(output_shared_strings_relationships_plan != nullptr
            && output_shared_strings_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_shared_strings_relationships_plan->owner_part
            == shared_strings_part.value(),
        "omitted-reference worksheet rewrite output should keep sharedStrings relationships audit");
    const auto* output_opaque_extension_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "omitted-reference worksheet rewrite output should preserve unknown extension relationships part");
    check(output_opaque_extension_relationships_plan != nullptr
            && output_opaque_extension_relationships_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships
            && output_opaque_extension_relationships_plan->owner_part
                == opaque_extension_part.value(),
        "omitted-reference worksheet rewrite output should keep unknown extension relationships audit");
    check(output_reader.content_types().override_for(calc_chain_part) == nullptr,
        "omitted-reference worksheet rewrite should still remove calcChain override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "omitted-reference worksheet rewrite should preserve table override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "omitted-reference worksheet rewrite should preserve VBA override");
}

void test_package_editor_reference_policy_fail_preserves_state()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t initial_package_entry_count = editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count = editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "initial worksheet plan should be copy-original before policy failure");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "initial manifest should include calcChain before policy failure");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetPr filterMode="1"/>)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<sheetFormatPr defaultRowHeight="15"/>)"
        R"(<cols><col min="1" max="2" width="16" customWidth="1"/></cols>)"
        R"(<sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" s="1"><f>SUM(A1:A1)</f><v>7</v></c></row></sheetData>)"
        R"(<sheetProtection sheet="1"/>)"
        R"(<protectedRanges><protectedRange name="Locked" sqref="A1:B2"/></protectedRanges>)"
        R"(<autoFilter ref="A1:B2"/>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(<dataValidations count="1"><dataValidation type="whole" sqref="A1:B2"><formula1>1</formula1></dataValidation></dataValidations>)"
        R"(<conditionalFormatting sqref="A1:B2"><cfRule type="expression" priority="1"><formula>$A$1&gt;0</formula></cfRule></conditionalFormatting>)"
        R"(<hyperlinks><hyperlink ref="A1" r:id="rId2"/></hyperlinks>)"
        R"(<printOptions horizontalCentered="1"/>)"
        R"(<pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/>)"
        R"(<pageSetup orientation="landscape"/>)"
        R"(<ignoredErrors><ignoredError sqref="A1:B2" numberStoredAsText="1"/></ignoredErrors>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(<extLst><ext uri="{fastxlsx-test}"/></extLst>)"
        R"(</worksheet>)";

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_worksheet, fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should fail worksheet replacement when policy blocks linked parts");

    check(editor.edit_plan().size() == initial_plan_size,
        "policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "policy failure should not change edit plan notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "shared string indexes"}),
        "policy failure should not leak worksheet sharedStrings payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "style id references"}),
        "policy failure should not leak worksheet styles payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "contains formulas"}),
        "policy failure should not leak worksheet formula payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "worksheet relationships"}),
        "policy failure should not leak worksheet relationship payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "autoFilter metadata"}),
        "policy failure should not leak worksheet autoFilter payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "sheet property metadata"}),
        "policy failure should not leak worksheet sheet property payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "dimension metadata"}),
        "policy failure should not leak worksheet dimension payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "view metadata"}),
        "policy failure should not leak worksheet view payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "default format metadata"}),
        "policy failure should not leak worksheet default format payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "column metadata"}),
        "policy failure should not leak worksheet column payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "data validation metadata"}),
        "policy failure should not leak worksheet data validation payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "print options metadata"}),
        "policy failure should not leak worksheet print options payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page margins metadata"}),
        "policy failure should not leak worksheet page margins payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "page setup metadata"}),
        "policy failure should not leak worksheet page setup payload audit notes");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet replacement", "extension metadata"}),
        "policy failure should not leak worksheet extension payload audit notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "policy failure should not change relationship target audit records");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "policy failure should not change worksheet relationship reference audit records");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "policy failure should not change worksheet payload audit records");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "policy failure should not change workbook payload audit records");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "policy failure should not change package-entry audit records");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "policy failure should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "policy failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave linked drawing copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave linked table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave VBA copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave drawing manifest copy-original");
    check_manifest_write_mode(editor, table_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave table manifest copy-original");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave sharedStrings manifest copy-original");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave styles manifest copy-original");
    check_manifest_write_mode(editor, vba_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave VBA manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == initial_note_count,
        "policy failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "policy failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "policy failure output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "policy failure output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "policy failure output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "policy failure output plan should not record omitted metadata entries");
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "policy failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "policy failure output plan should preserve calcChain policy");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "policy failure output plan should preserve every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "policy failure output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "policy failure output plan should classify content types as metadata entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "policy failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "policy failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "policy failure output should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "policy failure output should preserve drawing XML bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "policy failure output should preserve table XML bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "policy failure output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "policy failure output should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "policy failure output should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown extension bytes");
}

void test_package_editor_reference_policy_fail_preserves_prior_replacement()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-prior-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-prior-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Queued" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook metadata replacement before policy failure");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const auto* queued_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(queued_workbook_plan != nullptr,
        "queued policy-failure fixture should record prior workbook replacement");
    check(queued_workbook_plan->write_mode
            == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement should be local-DOM-rewrite before policy failure");
    check(queued_workbook_plan->reason.find("queued workbook metadata replacement")
            != std::string::npos,
        "queued workbook replacement should keep its reason before policy failure");
    const auto* queued_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(queued_workbook_relationships_plan != nullptr,
        "queued workbook replacement should audit preserved workbook relationships");
    check(queued_workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued workbook relationships audit should be copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement should update manifest write mode before policy failure");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should still fail linked worksheet replacement after a queued edit");

    check(editor.edit_plan().size() == queued_plan_size,
        "policy failure should preserve prior edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "policy failure should not append notes to a queued edit plan");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "policy failure should not append relationship target audits to a queued edit plan");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "policy failure should not append worksheet relationship reference audits to a queued edit plan");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "policy failure should not append worksheet payload audits to a queued edit plan");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "policy failure should not append workbook payload audits to a queued edit plan");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "policy failure should preserve queued package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "policy failure should not add removed parts after a queued edit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "policy failure should not add removed package entries after a queued edit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "policy failure should not request recalculation after a queued edit");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "policy failure should preserve calcChain action after a queued edit");
    const auto* final_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(final_workbook_plan != nullptr
            && final_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "policy failure should keep prior workbook replacement in the edit plan");
    check(final_workbook_plan->reason.find("queued workbook metadata replacement")
            != std::string::npos,
        "policy failure should keep prior workbook replacement reason");
    const auto* final_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(final_workbook_relationships_plan != nullptr
            && final_workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should keep prior workbook relationships audit");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original after a queued edit");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in manifest after a queued edit");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "policy failure should keep prior workbook manifest write mode");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original after a queued edit");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original after a queued edit");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued workbook policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == queued_note_count,
        "queued workbook policy failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued workbook policy failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued workbook policy failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued workbook policy failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued workbook policy failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.empty(),
        "queued workbook policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "queued workbook policy failure output plan should not record omitted metadata entries");
    check(!output_plan.full_calculation_on_load,
        "queued workbook policy failure output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "queued workbook policy failure output plan should preserve calcChain policy");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued workbook policy failure output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve worksheet copy-original");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve calcChain copy-original");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve content types copy-original");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued workbook policy failure output plan should preserve unknown extension copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "policy failure output should keep prior workbook replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "policy failure output should preserve workbook relationships for prior replacement");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes after a queued edit");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "policy failure output should preserve worksheet relationships after a queued edit");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes after a queued edit");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "policy failure output should preserve content types after a queued edit");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown bytes after a queued edit");
}

void test_package_editor_reference_policy_fail_preserves_prior_document_properties()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-fail-docprops-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-docprops-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::DocumentProperties properties;
    properties.creator = "Queued Patch Author";
    properties.last_modified_by = "Queued Patch Reviewer";
    properties.title = "Queued metadata";
    properties.description = "Preserved after linked worksheet policy failure";
    properties.application = "FastXLSX Patch";
    properties.app_version = "4.1";
    editor.set_document_properties(properties);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const auto* queued_core_plan = editor.edit_plan().find_part(core_part);
    check(queued_core_plan != nullptr
            && queued_core_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should record generated core properties");
    const auto* queued_app_plan = editor.edit_plan().find_part(app_part);
    check(queued_app_plan != nullptr
            && queued_app_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should record generated app properties");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") != nullptr,
        "queued docProps fixture should audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") != nullptr,
        "queued docProps fixture should audit package relationships rewrite");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should mark core manifest generated");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "queued docProps fixture should mark app manifest generated");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", fail_policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should fail linked worksheet replacement after queued docProps");

    check(editor.edit_plan().size() == queued_plan_size,
        "policy failure should preserve queued docProps edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "policy failure should not append notes after queued docProps");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "policy failure should not append relationship target audits after queued docProps");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "policy failure should not append worksheet relationship reference audits after queued docProps");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "policy failure should not append worksheet payload audits after queued docProps");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "policy failure should not append workbook payload audits after queued docProps");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "policy failure should preserve queued docProps package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "policy failure should not add removed parts after queued docProps");
    check(editor.edit_plan().removed_package_entries().empty(),
        "policy failure should not add removed package entries after queued docProps");
    check(!editor.edit_plan().full_calculation_on_load(),
        "policy failure should not request recalculation after queued docProps");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "policy failure should preserve calcChain policy after queued docProps");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated core properties in edit plan");
    check(editor.edit_plan().find_part(app_part)->write_mode
            == fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated app properties in edit plan");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet copy-original after queued docProps");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave workbook copy-original after queued docProps");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "policy failure should keep calcChain in manifest after queued docProps");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated core manifest state");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::GenerateSmallXml,
        "policy failure should keep generated app manifest state");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave worksheet manifest copy-original after queued docProps");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "policy failure should leave calcChain manifest copy-original after queued docProps");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued docProps policy failure should keep planned output snapshot consistent");
    check(output_plan.notes.size() == queued_note_count,
        "queued docProps policy failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued docProps policy failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued docProps policy failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued docProps policy failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued docProps policy failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.empty(),
        "queued docProps policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "queued docProps policy failure output plan should not record omitted metadata entries");
    check(!output_plan.full_calculation_on_load,
        "queued docProps policy failure output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "queued docProps policy failure output plan should preserve calcChain policy");
    const auto* output_core_plan =
        find_output_entry_plan(output_plan.entries, "docProps/core.xml");
    check(output_core_plan != nullptr
            && output_core_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && output_core_plan->package_part
            && output_core_plan->part_name == core_part.value()
            && !output_core_plan->omitted,
        "queued docProps policy failure output plan should keep generated core properties");
    const auto* output_app_plan =
        find_output_entry_plan(output_plan.entries, "docProps/app.xml");
    check(output_app_plan != nullptr
            && output_app_plan->write_mode
                == fastxlsx::detail::PartWriteMode::GenerateSmallXml
            && output_app_plan->package_part
            && output_app_plan->part_name == app_part.value()
            && !output_app_plan->omitted,
        "queued docProps policy failure output plan should keep generated app properties");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued docProps policy failure output plan should rewrite content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued docProps policy failure output plan should rewrite package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve workbook copy-original");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve worksheet copy-original");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve calcChain copy-original");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued docProps policy failure output plan should preserve unknown extension copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    const std::string core_xml = output_reader.read_entry("docProps/core.xml");
    check_contains(core_xml, "<dc:creator>Queued Patch Author</dc:creator>",
        "policy failure output should keep queued core properties creator");
    check_contains(core_xml, "<cp:lastModifiedBy>Queued Patch Reviewer</cp:lastModifiedBy>",
        "policy failure output should keep queued core properties modifier");
    check_contains(core_xml, "<dc:title>Queued metadata</dc:title>",
        "policy failure output should keep queued core properties title");
    const std::string app_xml = output_reader.read_entry("docProps/app.xml");
    check_contains(app_xml, "<Application>FastXLSX Patch</Application>",
        "policy failure output should keep queued app properties application");
    check_contains(app_xml, "<AppVersion>4.1</AppVersion>",
        "policy failure output should keep queued app properties version");
    check_contains(output_reader.read_entry("[Content_Types].xml"), "/docProps/app.xml",
        "policy failure output should keep queued docProps content type");
    check_contains(output_reader.read_entry("_rels/.rels"), "Target=\"docProps/app.xml\"",
        "policy failure output should keep queued docProps package relationship");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "policy failure output should preserve workbook bytes after queued docProps");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "policy failure output should preserve worksheet bytes after queued docProps");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "policy failure output should preserve calcChain bytes after queued docProps");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "policy failure output should preserve unknown extension bytes after queued docProps");
}

void test_package_editor_reference_policy_request_recalculation_updates_workbook_metadata()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-policy-recalc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-recalc-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    fastxlsx::detail::ReferencePolicy policy;
    policy.request_full_calculation_on_sheet_rewrite = false;
    policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::RequestRecalculation;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>101</v></c></row></sheetData>)"
        R"(<drawing r:id="rId1"/>)"
        R"(<tableParts count="1"><tablePart r:id="rId3"/></tableParts>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet, policy);

    check(editor.edit_plan().full_calculation_on_load(),
        "request-recalculation policy should request full calculation for linked worksheet rewrite");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "request-recalculation policy should preserve its calcChain action");
    check(editor.edit_plan().removed_parts().empty(),
        "request-recalculation preserve policy should not remove calcChain");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr,
        "request-recalculation policy should record workbook metadata rewrite");
    check(workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "request-recalculation policy should local-DOM-rewrite workbook metadata");
    check(workbook_plan->reason.find("definedNames") != std::string::npos,
        "request-recalculation workbook rewrite should preserve definedNames review context");
    const auto* workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(workbook_relationships_plan != nullptr,
        "request-recalculation policy should audit preserved workbook relationships");
    check(workbook_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "preserved workbook relationships should be copy-original in package-entry audit");
    check(workbook_relationships_plan->reason.find("/xl/workbook.xml") != std::string::npos,
        "preserved workbook relationships audit should name the owner part");
    const auto* workbook_manifest_part = editor.manifest().find_part(workbook_part);
    check(workbook_manifest_part != nullptr,
        "request-recalculation policy should keep workbook in the manifest");
    check(workbook_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "request-recalculation policy manifest should mirror workbook metadata rewrite mode");
    check(workbook_manifest_part->dirty && !workbook_manifest_part->preserve_original
            && !workbook_manifest_part->generated,
        "request-recalculation policy manifest should mark workbook metadata dirty");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "request-recalculation preserve policy should keep calcChain in the manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "request-recalculation output plan should expose full calculation request");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "request-recalculation output plan should expose calcChain preserve policy");
    check(output_plan.relationship_target_audits.size()
            == editor.edit_plan().relationship_target_audits().size(),
        "request-recalculation output plan should snapshot relationship target audits");
    check(has_note_containing(output_plan.notes,
              {"worksheet relationships are preserved", "policy review"}),
        "request-recalculation output plan should snapshot dependency audit notes");
    check_output_entry_plan(output_plan.entries, "xl/calcChain.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "request-recalculation output plan should keep calcChain copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") != entries.end(),
        "request-recalculation preserve output should keep calcChain.xml");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "request-recalculation output should replace worksheet XML");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "request-recalculation preserve output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "request-recalculation preserve output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "request-recalculation preserve output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "request-recalculation preserve output should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "request-recalculation preserve output should preserve styles bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "request-recalculation preserve output should preserve unknown extension bytes");

    const std::string workbook_xml = output_reader.read_entry("xl/workbook.xml");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "request-recalculation output should request full calculation on load");
    check_contains(workbook_xml, R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)",
        "request-recalculation output should preserve workbook defined names");
}

void test_package_editor_rejects_calc_chain_rebuild_without_state_changes()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-rebuild-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-rebuild-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    fastxlsx::detail::ReferencePolicy policy;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Rebuild;

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>", policy);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject calcChain rebuild because it is not implemented");

    check(editor.edit_plan().size() == initial_plan_size,
        "calcChain rebuild failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "calcChain rebuild failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "calcChain rebuild failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "calcChain rebuild failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "calcChain rebuild failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "calcChain rebuild failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "calcChain rebuild failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "calcChain rebuild failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "calcChain rebuild failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave worksheet copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "calcChain rebuild failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "calcChain rebuild failure should leave calcChain manifest copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "calcChain rebuild failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "calcChain rebuild failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "calcChain rebuild failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "calcChain rebuild failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "calcChain rebuild failure output should preserve workbook XML bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "calcChain rebuild failure output should preserve unknown bytes");
}

void test_package_editor_rejects_malformed_workbook_metadata_without_state_changes()
{
    CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-malformed-workbook-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"xl/calcChain.xml", source.calc_chain},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-malformed-workbook-output.xlsx");

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
    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>");
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject workbook metadata rewrite when workbook XML is malformed");

    check(editor.edit_plan().size() == initial_plan_size,
        "malformed workbook failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "malformed workbook failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "malformed workbook failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "malformed workbook failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "malformed workbook failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "malformed workbook failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed workbook failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave workbook copy-original");
    check(editor.manifest().find_part(calc_chain_part) != nullptr,
        "malformed workbook failure should keep calcChain in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave worksheet manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave workbook manifest copy-original");
    check_manifest_write_mode(editor, calc_chain_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "malformed workbook failure should leave calcChain manifest copy-original");
    check(editor.manifest().content_types().override_for(calc_chain_part) != nullptr,
        "malformed workbook failure should keep calcChain content type override");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "malformed workbook failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed workbook failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "malformed workbook failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "malformed workbook failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "malformed workbook failure output plan should not add worksheet reference audits");
    check(output_plan.removed_parts.empty(),
        "malformed workbook failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.empty(),
        "malformed workbook failure output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "malformed workbook failure output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "malformed workbook failure output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "malformed workbook failure output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "malformed workbook failure output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "malformed workbook failure output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "malformed workbook failure output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/calcChain.xml", true,
        calc_chain_part.value(),
        "malformed workbook failure output plan should classify calcChain as a package part");

    {
        fastxlsx::detail::PackageEditor cell_editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();
        const std::array replacements {
            worksheet_cell_replacement("A1", R"(<c r="A1"><v>7</v></c>)"),
        };

        bool cell_replacement_failed = false;
        try {
            cell_editor.replace_worksheet_cells(worksheet_part, replacements);
        } catch (const std::exception& error) {
            cell_replacement_failed = true;
            check_contains(error.what(), "workbook XML closing tag",
                "malformed workbook cell replacement failure should report workbook XML preflight");
        }
        check(cell_replacement_failed,
            "cell replacement should reject malformed workbook metadata after output staging");
        check(cell_editor.edit_plan().size() == initial_plan_size,
            "malformed workbook cell replacement failure should not change edit plan size");
        check(!cell_editor.edit_plan().full_calculation_on_load(),
            "malformed workbook cell replacement failure should not request full calculation");
        check_manifest_write_mode(cell_editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "malformed workbook cell replacement failure should leave worksheet manifest copy-original");
        check_no_new_package_editor_temp_files(temp_files_before,
            "malformed workbook cell replacement failure should clean staged output temp file immediately");
    }

    {
        fastxlsx::detail::PackageEditor sheet_data_editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();

        bool sheet_data_failed = false;
        try {
            replace_worksheet_sheet_data_from_single_chunk_source(sheet_data_editor, worksheet_part,
                R"(<sheetData><row r="1"><c r="A1"><v>9</v></c></row></sheetData>)");
        } catch (const std::exception& error) {
            sheet_data_failed = true;
            check_contains(error.what(), "workbook XML closing tag",
                "malformed workbook sheetData failure should report workbook XML preflight");
        }
        check(sheet_data_failed,
            "sheetData replacement should reject malformed workbook metadata after output staging");
        check(sheet_data_editor.edit_plan().size() == initial_plan_size,
            "malformed workbook sheetData failure should not change edit plan size");
        check(sheet_data_editor.edit_plan().notes().size() == initial_note_count,
            "malformed workbook sheetData failure should not change edit plan notes");
        check(!sheet_data_editor.edit_plan().full_calculation_on_load(),
            "malformed workbook sheetData failure should not request full calculation");
        check_manifest_write_mode(sheet_data_editor, worksheet_part,
            fastxlsx::detail::PartWriteMode::CopyOriginal,
            "malformed workbook sheetData failure should leave worksheet manifest copy-original");
        check_no_new_package_editor_temp_files(temp_files_before,
            "malformed workbook sheetData failure should clean staged temp files immediately");
    }

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "malformed workbook failure output should preserve workbook XML bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "malformed workbook failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "malformed workbook failure output should preserve calcChain bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "malformed workbook failure output should preserve content types bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "malformed workbook failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "malformed workbook failure output should preserve unknown bytes");
}

void test_package_editor_rejects_worksheet_rewrite_without_workbook_metadata()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-editor-missing-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-missing-workbook-output.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"/>)";
    const std::string worksheet =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    const std::string unknown = std::string("missing-workbook\0unknown", 24);

    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/worksheets/sheet1.xml", worksheet},
            {"custom/opaque.bin", unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();

    bool failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, "<worksheet/>");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "officeDocument relationship",
            "missing workbook failure should report workbook metadata requirement");
    }
    check(failed,
        "PackageEditor should reject worksheet replacement without workbook metadata");

    check(editor.edit_plan().size() == initial_plan_size,
        "missing workbook failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "missing workbook failure should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "missing workbook failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "missing workbook failure should not change worksheet relationship reference audits");
    check(editor.edit_plan().removed_parts().empty(),
        "missing workbook failure should not record removed parts");
    check(editor.edit_plan().package_entries().empty(),
        "missing workbook failure should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().empty(),
        "missing workbook failure should not record removed package-entry audit");
    check(!editor.edit_plan().full_calculation_on_load(),
        "missing workbook failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "missing workbook failure should not change calcChain action");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing workbook failure should leave worksheet copy-original");
    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "missing workbook failure should not invent workbook edit-plan entries");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "missing workbook failure should not invent workbook manifest parts");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing workbook failure should leave worksheet manifest copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("xl/workbook.xml") == nullptr,
        "missing workbook failure output should not create workbook XML");
    check(output_reader.find_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "missing workbook failure output should not create workbook relationships");
    check(output_reader.read_entry("[Content_Types].xml") == content_types,
        "missing workbook failure output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == package_relationships,
        "missing workbook failure output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == worksheet,
        "missing workbook failure output should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == unknown,
        "missing workbook failure output should preserve unknown bytes");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor policy shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "policy")) {
            test_package_editor_repeated_worksheet_rewrite_upserts_relationship_target_audits();
            test_package_editor_preserves_source_relationship_parts_when_replacement_omits_references();
            test_package_editor_reference_policy_fail_preserves_state();
            test_package_editor_reference_policy_fail_preserves_prior_replacement();
            test_package_editor_reference_policy_fail_preserves_prior_document_properties();
            test_package_editor_reference_policy_request_recalculation_updates_workbook_metadata();
            test_package_editor_rejects_calc_chain_rebuild_without_state_changes();
            test_package_editor_rejects_malformed_workbook_metadata_without_state_changes();
            test_package_editor_rejects_worksheet_rewrite_without_workbook_metadata();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
