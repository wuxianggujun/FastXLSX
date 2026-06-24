#include "test_package_editor_preservation_linked_pivot_common.hpp"

void test_package_editor_worksheet_rewrite_preserves_pivot_table_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_sheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheetData><row r="1"><c r="A1"><v>128</v></c></row></sheetData>)"
        R"(</worksheet>)";
    replace_worksheet_part_from_single_chunk_source(editor, worksheet_part, replacement_sheet);

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table part should remain visible in worksheet rewrite edit plan");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table part should remain copy-original during worksheet rewrite");
    check(pivot_table_plan->reason.find("worksheet relationship rIdPivotTable")
            != std::string::npos,
        "pivot table copy reason should come from worksheet relationship traversal");
    check(pivot_table_plan->reason.find("relationships/pivotTable") != std::string::npos,
        "pivot table copy reason should include relationship type");
    check(pivot_table_plan->relationship_owner_part == worksheet_part.value(),
        "pivot table copy audit should keep structured relationship owner");
    check(pivot_table_plan->relationship_id == "rIdPivotTable",
        "pivot table copy audit should keep structured relationship id");
    check(pivot_table_plan->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table copy audit should keep structured relationship type");
    check(pivot_table_plan->relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table copy audit should keep structured relationship target");

    const auto* pivot_cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(pivot_cache_definition_plan != nullptr,
        "pivot cache definition should remain visible in worksheet rewrite edit plan");
    check(pivot_cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition should remain copy-original during worksheet rewrite");
    check(pivot_cache_definition_plan->reason.find("/xl/pivotTables/pivotTable1.xml")
            != std::string::npos
            && pivot_cache_definition_plan->reason.find("rIdPivotCacheDef")
                != std::string::npos,
        "pivot cache definition copy reason should come from pivot table relationship traversal");
    check(pivot_cache_definition_plan->relationship_owner_part == pivot_table_part.value(),
        "pivot cache definition copy audit should keep pivot-table-owned relationship owner");
    check(pivot_cache_definition_plan->relationship_id == "rIdPivotCacheDef",
        "pivot cache definition copy audit should keep relationship id");
    check(pivot_cache_definition_plan->relationship_target
            == "../pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition copy audit should keep relationship target");

    const auto* pivot_cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(pivot_cache_records_plan != nullptr,
        "pivot cache records should remain visible in worksheet rewrite edit plan");
    check(pivot_cache_records_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records should remain copy-original during worksheet rewrite");
    check(pivot_cache_records_plan->reason.find("/xl/pivotCache/pivotCacheDefinition1.xml")
            != std::string::npos
            && pivot_cache_records_plan->reason.find("rIdPivotRecords")
                != std::string::npos,
        "pivot cache records copy reason should come from cache-definition relationship traversal");
    check(pivot_cache_records_plan->relationship_owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache records copy audit should keep cache-definition-owned relationship owner");
    check(pivot_cache_records_plan->relationship_id == "rIdPivotRecords",
        "pivot cache records copy audit should keep relationship id");
    check(pivot_cache_records_plan->relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records copy audit should keep relationship target");

    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot worksheet rewrite should keep unrelated unknown part copy-original");
    const auto* worksheet_relationships_plan =
        editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(worksheet_relationships_plan != nullptr,
        "pivot worksheet rewrite should audit preserved worksheet relationships");
    check(worksheet_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot worksheet relationships should be copy-original in package-entry audit");
    check(worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet relationships audit should keep structured role");
    check(worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "pivot worksheet relationships audit should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            != nullptr,
        "pivot worksheet rewrite should audit preserved pivot table relationships");
    check(editor.edit_plan().find_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != nullptr,
        "pivot worksheet rewrite should audit preserved pivot cache definition relationships");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "pivot worksheet rewrite should not rewrite content types without calcChain");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load,
        "pivot worksheet rewrite output plan should request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Remove,
        "pivot worksheet rewrite output plan should expose calcChain removal policy");
    check(output_plan.relationship_target_audits.empty(),
        "pivot worksheet rewrite output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot worksheet rewrite output plan should stream-rewrite worksheet");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml",
        true, worksheet_part.value(),
        "pivot worksheet rewrite output plan should classify worksheet as a part");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot worksheet rewrite output plan should update workbook metadata");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "pivot worksheet rewrite output plan should classify workbook as a part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "pivot worksheet rewrite output plan should classify package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve workbook relationships");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "pivot worksheet rewrite output plan should classify workbook relationships");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep workbook relationship role");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "pivot worksheet rewrite output plan should keep workbook owner context");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve worksheet relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/worksheets/_rels/sheet1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify worksheet relationships");
    const auto* output_worksheet_relationships_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels");
    check(output_worksheet_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep worksheet relationship role");
    check(output_worksheet_relationships_plan->owner_part == worksheet_part.value(),
        "pivot worksheet rewrite output plan should keep worksheet owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot table");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot worksheet rewrite output plan should classify pivot table as a part");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotTables/pivotTable1.xml", worksheet_part.value(), "rIdPivotTable",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "../pivotTables/pivotTable1.xml",
        "pivot worksheet rewrite output plan should keep pivot table relationship context");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot table relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify pivot table relationships");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotTables/_rels/pivotTable1.xml.rels");
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep pivot table relationship role");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot worksheet rewrite output plan should keep pivot table owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve pivot cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot worksheet rewrite output plan should classify pivot cache definition");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", pivot_table_part.value(),
        "rIdPivotCacheDef",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "../pivotCache/pivotCacheDefinition1.xml",
        "pivot worksheet rewrite output plan should keep cache definition relationship context");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve cache definition relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", false, "",
        "pivot worksheet rewrite output plan should classify cache definition relationships");
    const auto* output_cache_definition_relationships_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels");
    check(output_cache_definition_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot worksheet rewrite output plan should keep cache definition relationship role");
    check(output_cache_definition_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot worksheet rewrite output plan should keep cache definition owner context");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true, pivot_cache_records_part.value(),
        "pivot worksheet rewrite output plan should classify cache records");
    check_output_entry_relationship_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", pivot_cache_definition_part.value(),
        "rIdPivotRecords",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords",
        "pivotCacheRecords1.xml",
        "pivot worksheet rewrite output plan should keep cache records relationship context");
    check(find_output_entry_plan(output_plan.entries,
              "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels")
            == nullptr,
        "pivot worksheet rewrite output plan should not invent cache records relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot worksheet rewrite output plan should classify content types");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot worksheet rewrite output plan should preserve unknown entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        unknown_part.value(),
        "pivot worksheet rewrite output plan should classify unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot table part");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") != entries.end(),
        "pivot worksheet rewrite output should keep pivot table relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot worksheet rewrite output should keep pivot cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == replacement_sheet,
        "pivot worksheet rewrite should write replacement worksheet XML");
    const std::string output_workbook = output_reader.read_entry("xl/workbook.xml");
    check_contains(output_workbook, "<pivotCaches>",
        "pivot worksheet rewrite should preserve workbook pivot cache metadata");
    check_contains(output_workbook, "r:id=\"rIdPivotCache\"",
        "pivot worksheet rewrite should preserve workbook pivot cache relationship id");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot worksheet rewrite should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot worksheet rewrite should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot worksheet rewrite should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot worksheet rewrite should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot worksheet rewrite should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot worksheet rewrite should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot worksheet rewrite should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot worksheet rewrite should preserve content types bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot worksheet rewrite should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot worksheet rewrite should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot worksheet rewrite should keep pivot table worksheet relationship id");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot worksheet rewrite should not rewrite pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot worksheet rewrite should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot worksheet rewrite should keep workbook pivot cache relationship id");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot worksheet rewrite should keep pivot table owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot worksheet rewrite should keep pivot table to cache definition relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot worksheet rewrite should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot worksheet rewrite should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship");
    check(output_reader.relationships_for(pivot_cache_records_part) == nullptr,
        "pivot worksheet rewrite should not invent pivot cache records owner relationships");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot worksheet rewrite should preserve pivot cache records content type override");
}

void test_package_editor_replaces_pivot_table_and_preserves_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-replace-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Patched" cacheId="1" dataCaption="Patched Values">)"
        R"(<location ref="G3:H8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, replacement_pivot_table,
        "pivot table local-DOM rewrite");

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table replacement should be present in the edit plan");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement should mirror write mode into manifest");
    const auto* pivot_table_relationships_plan =
        editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels");
    check(pivot_table_relationships_plan != nullptr,
        "pivot table replacement should audit preserved owner relationships");
    check(pivot_table_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table owner relationships should remain copy-original");
    check(pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table owner relationships audit should keep structured role");
    check(pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table owner relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml")
            == replacement_pivot_table,
        "pivot table replacement should write replacement pivot table XML");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot table replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot table replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdPivotTable") != nullptr,
        "pivot table replacement should keep worksheet pivot table relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot table replacement should keep pivot table owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot table replacement should keep pivot table cache definition relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot table replacement should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table replacement should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement should keep pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table replacement should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table replacement should keep pivot cache records content type override");
}

void test_package_editor_repeated_pivot_table_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-repeat-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    const std::string stale_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Stale" cacheId="1" dataCaption="Stale Values">)"
        R"(<location ref="F3:G8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisRow"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    const std::string final_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Final" cacheId="1" dataCaption="Final Values">)"
        R"(<location ref="H4:I9" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";

    replace_part_with_memory_chunks(editor, pivot_table_part, stale_pivot_table,
        "stale repeated pivot table local-DOM rewrite");
    replace_part_with_memory_chunks(editor, pivot_table_part, final_pivot_table,
        "final repeated pivot table local-DOM rewrite");

    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "repeated pivot table replacement should keep an active edit-plan part");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot table replacement should keep final local-DOM-rewrite mode");
    check(pivot_table_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot table replacement should keep final reason");
    check(pivot_table_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot table replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot table replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_table_part) != nullptr,
        "repeated pivot table replacement should keep pivot table content type override");
    check(editor.edit_plan().find_removed_part(pivot_table_part) == nullptr,
        "repeated pivot table replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            == nullptr,
        "repeated pivot table replacement should not leave owner relationships omission");
    const auto* pivot_table_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_table_relationships_entry);
    check(pivot_table_relationships_audit != nullptr,
        "repeated pivot table replacement should preserve owner relationships audit");
    check(pivot_table_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should preserve owner relationships bytes");
    check(pivot_table_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot table replacement should keep owner relationship audit role");
    check(pivot_table_relationships_audit->owner_part == pivot_table_part.value(),
        "repeated pivot table replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot table replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot table replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot table replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot table replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot table replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot table replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot table replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot table replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot table replacement output plan should rewrite pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot table replacement output plan should keep final reason");
    check(output_pivot_table_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot table replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, pivot_table_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve owner relationships");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot table replacement output plan should keep owner relationship audit role");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "repeated pivot table replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve pivot cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot table replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == final_pivot_table,
        "repeated pivot table replacement should write final pivot table payload");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "repeated pivot table replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "repeated pivot table replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot table replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "repeated pivot table replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot table replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot table replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot table replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot table replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot table replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot table replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot table replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after repeated replacement");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "repeated pivot table replacement should keep pivot table content type override");
}

void test_package_editor_removes_pivot_table_and_preserves_cache_chain()
{
    const PivotSourcePackage source =
        write_pivot_source_package("fastxlsx-package-editor-remove-pivot-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(pivot_table_part, "explicit pivot table part removal");

    check(editor.edit_plan().find_part(pivot_table_part) == nullptr,
        "pivot table removal should clear the active edit-plan part");
    const auto* removed_pivot_table = editor.edit_plan().find_removed_part(pivot_table_part);
    check(removed_pivot_table != nullptr,
        "pivot table removal should record removed-part audit");
    check(removed_pivot_table->reason.find("pivot table part") != std::string::npos,
        "pivot table removal should retain the removal reason");
    check(removed_pivot_table->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot table removal should audit preserved inbound relationships");
    check(removed_pivot_table->inbound_relationships.size() == 1,
        "pivot table removal should keep structured inbound audit");
    const auto& pivot_table_inbound =
        removed_pivot_table->inbound_relationships.front();
    check(pivot_table_inbound.owner_part == worksheet_part.value(),
        "pivot table removal should keep inbound owner part");
    check(pivot_table_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "pivot table removal should keep inbound owner relationship entry");
    check(pivot_table_inbound.relationship_id == "rIdPivotTable",
        "pivot table removal should keep inbound relationship id");
    check(pivot_table_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table removal should keep inbound relationship type");
    check(pivot_table_inbound.relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table removal should keep inbound raw target");
    check(pivot_table_inbound.target_part == pivot_table_part,
        "pivot table removal should keep normalized target part");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "pivot table removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot table removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/pivotTables/_rels/pivotTable1.xml.rels")
            != nullptr,
        "pivot table removal should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == nullptr,
        "pivot table removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") == entries.end(),
        "pivot table removal output should omit pivot table part");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") == entries.end(),
        "pivot table removal output should omit pivot table owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot table removal output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot table removal output should keep pivot cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot table removal output should keep pivot cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal output should remove pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table removal output should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table removal output should keep pivot cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotTables/pivotTable1.xml",
        "pivot table removal content types XML should omit pivot table override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table removal should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table removal should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table removal should preserve pivot cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table removal should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot table removal should keep inbound worksheet pivot table relationship id");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot table removal should not rewrite inbound pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table removal should keep workbook pivot cache relationship");
    const auto* pivot_cache_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(pivot_cache_relationships != nullptr,
        "pivot table removal should keep pivot cache definition relationships readable");
    check(pivot_cache_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table removal should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep inbound pivot table relationship after removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after removal");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after removal");
    check(output_reader.relationships_for(pivot_table_part) == nullptr,
        "pivot table removal should not keep owner relationships for absent part");
}

void test_package_editor_pivot_table_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    editor.remove_part(pivot_table_part, "temporary pivot table removal");
    check(editor.edit_plan().find_removed_part(pivot_table_part) != nullptr,
        "setup should record removed pivot table before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "setup should omit pivot table owner relationships before restore");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "setup should remove pivot table from manifest before restore");

    const std::string restored_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Restored" cacheId="1" dataCaption="Restored Values">)"
        R"(<location ref="F3:G8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisRow"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, restored_pivot_table,
        "restored pivot table after removal");

    check(editor.edit_plan().find_removed_part(pivot_table_part) == nullptr,
        "pivot table replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            == nullptr,
        "pivot table replacement after removal should clear stale owner relationships omission");
    const auto* pivot_table_plan = editor.edit_plan().find_part(pivot_table_part);
    check(pivot_table_plan != nullptr,
        "pivot table replacement after removal should restore active edit-plan part");
    check(pivot_table_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement after removal should keep final write mode");
    check(pivot_table_plan->reason.find("after removal") != std::string::npos,
        "pivot table replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_table_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot table replacement after removal should restore manifest write mode");

    const auto* pivot_table_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_table_relationships_entry);
    check(pivot_table_relationships_audit != nullptr,
        "pivot table replacement after removal should restore owner relationships audit");
    check(pivot_table_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal owner relationships should be copy-original");
    check(pivot_table_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table replacement after removal owner relationships should keep source role");
    check(pivot_table_relationships_audit->owner_part == pivot_table_part.value(),
        "pivot table replacement after removal owner relationships should keep owner part");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should restore source content types audit");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table replacement after removal content types audit should keep structured role");
    check(editor.manifest().content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement after removal should restore manifest content type override");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot table replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot table replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot table replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot table replacement after removal output plan should rewrite pivot table part");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot table replacement after removal output plan should classify rewritten pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("after removal") != std::string::npos,
        "pivot table replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, pivot_table_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_table_relationships_entry,
        false, "",
        "pivot table replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table replacement after removal output plan should classify owner relationships metadata");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot table replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot table replacement after removal output should restore pivot table entry");
    check(entries.find(pivot_table_relationships_entry) != entries.end(),
        "pivot table replacement after removal output should restore owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_table_part.zip_path());
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml")
            == restored_pivot_table,
        "pivot table replacement after removal should write restored payload");
    check(output_reader.read_entry(pivot_table_relationships_entry)
            == source.pivot_table_relationships,
        "pivot table replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot table replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table replacement after removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table replacement after removal should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table replacement after removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table replacement after removal should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table replacement after removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table replacement after removal should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdPivotTable") != nullptr,
        "pivot table replacement after removal should keep inbound worksheet relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot table replacement after removal should keep owner relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot table replacement after removal should keep cache definition relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot table replacement after removal should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table replacement after removal should keep cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep worksheet pivot table relationship after restore");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after restore");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after pivot table restore");
    check(output_reader.content_types().override_for(pivot_table_part) != nullptr,
        "pivot table replacement after removal should keep pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table replacement after removal should keep cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table replacement after removal should keep cache records content type override");
}

void test_package_editor_pivot_table_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-table-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-table-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName pivot_table_part("/xl/pivotTables/pivotTable1.xml");
    const fastxlsx::detail::PartName pivot_cache_definition_part(
        "/xl/pivotCache/pivotCacheDefinition1.xml");
    const fastxlsx::detail::PartName pivot_cache_records_part(
        "/xl/pivotCache/pivotCacheRecords1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    const std::string pivot_table_relationships_entry =
        "xl/pivotTables/_rels/pivotTable1.xml.rels";

    const std::string stale_pivot_table =
        R"(<pivotTableDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" name="PivotTable1Stale" cacheId="1" dataCaption="Stale Values">)"
        R"(<location ref="H3:I8" firstHeaderRow="1" firstDataRow="2" firstDataCol="1"/>)"
        R"(<pivotFields count="1"><pivotField axis="axisCol"/></pivotFields>)"
        R"(</pivotTableDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_table_part, stale_pivot_table,
        "stale pivot table replacement before removal");
    check(editor.edit_plan().find_part(pivot_table_part) != nullptr,
        "setup should record active pivot table replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "setup should preserve owner relationships before final pivot table removal");

    editor.remove_part(pivot_table_part, "final pivot table removal after replacement");

    check(editor.edit_plan().find_part(pivot_table_part) == nullptr,
        "pivot table removal after replacement should clear active replacement");
    const auto* removed_pivot_table = editor.edit_plan().find_removed_part(pivot_table_part);
    check(removed_pivot_table != nullptr,
        "pivot table removal after replacement should record removed-part audit");
    check(removed_pivot_table->reason.find("after replacement") != std::string::npos,
        "pivot table removal after replacement should keep final removal reason");
    check(removed_pivot_table->inbound_relationships.size() == 1,
        "pivot table removal after replacement should keep worksheet inbound audit");
    const auto& inbound = removed_pivot_table->inbound_relationships.front();
    check(inbound.owner_part == worksheet_part.value(),
        "pivot table removal after replacement should audit worksheet owner part");
    check(inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "pivot table removal after replacement should audit worksheet relationships entry");
    check(inbound.relationship_id == "rIdPivotTable",
        "pivot table removal after replacement should audit pivot table relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "pivot table removal after replacement should audit pivot table relationship type");
    check(inbound.relationship_target == "../pivotTables/pivotTable1.xml",
        "pivot table removal after replacement should audit raw pivot table target");
    check(inbound.target_part == pivot_table_part,
        "pivot table removal after replacement should audit normalized pivot table target");
    check(editor.edit_plan().find_removed_package_entry(pivot_table_relationships_entry)
            != nullptr,
        "pivot table removal after replacement should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(pivot_table_relationships_entry) == nullptr,
        "pivot table removal after replacement should clear active owner relationships audit");
    check(editor.manifest().find_part(pivot_table_part) == nullptr,
        "pivot table removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot table removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot table removal after replacement content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot table removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot table removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot table removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot table removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot table removal after replacement output plan should omit pivot table part");
    check_output_entry_part_context(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        true, pivot_table_part.value(),
        "pivot table removal after replacement output plan should classify omitted pivot table");
    const auto* output_pivot_table_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml");
    check(output_pivot_table_plan->reason.find("after replacement") != std::string::npos,
        "pivot table removal after replacement output plan should keep final removal reason");
    check(output_pivot_table_plan->inbound_relationships.size() == 1,
        "pivot table removal after replacement output plan should expose worksheet inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotTables/pivotTable1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rIdPivotTable",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
        "../pivotTables/pivotTable1.xml", pivot_table_part,
        "pivot table removal after replacement output plan should keep worksheet inbound audit");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot table removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels", false, "",
        "pivot table removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_pivot_table_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_table_relationships_entry);
    check(output_pivot_table_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot table removal after replacement output plan should classify owner relationships metadata");
    check(output_pivot_table_relationships_plan->owner_part == pivot_table_part.value(),
        "pivot table removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot table removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot table removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot table removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot table removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotTables/pivotTable1.xml") == entries.end(),
        "pivot table removal after replacement output should omit pivot table part");
    check(entries.find(pivot_table_relationships_entry) == entries.end(),
        "pivot table removal after replacement output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot table removal after replacement output should keep cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") != entries.end(),
        "pivot table removal after replacement output should keep cache definition relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot table removal after replacement output should keep cache records");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement output should remove pivot table content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot table removal after replacement output should keep cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot table removal after replacement output should keep cache records content type override");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "pivot table removal after replacement output should keep unknown extension default content type");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotTables/pivotTable1.xml",
        "pivot table removal after replacement content types XML should omit pivot table override");
    check_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot table removal after replacement content types XML should keep cache definition override");
    check_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot table removal after replacement content types XML should keep cache records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot table removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot table removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot table removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot table removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot table removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot table removal after replacement should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot table removal after replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot table removal after replacement should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot table removal after replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "pivot table removal after replacement should keep worksheet relationships readable");
    const auto* pivot_table_relationship = worksheet_relationships->find_by_id("rIdPivotTable");
    check(pivot_table_relationship != nullptr,
        "pivot table removal after replacement should keep inbound worksheet pivot table relationship");
    check(pivot_table_relationship->target == "../pivotTables/pivotTable1.xml",
        "pivot table removal after replacement should not rewrite inbound pivot table target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot table removal after replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot table removal after replacement should keep workbook pivot cache relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot table removal after replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot table removal after replacement should keep cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdPivotTable")
            != nullptr,
        "relationship graph should keep inbound pivot table relationship after final removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after final removal");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after final removal");
    check(output_reader.relationships_for(pivot_table_part) == nullptr,
        "pivot table removal after replacement should not keep owner relationships for absent part");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "preservation-linked-pivot")) {
            throw TestFailure("wrong shard for preservation-linked-pivot executable");
        }

        test_package_editor_worksheet_rewrite_preserves_pivot_table_cache_chain();
        test_package_editor_replaces_pivot_table_and_preserves_cache_chain();
        test_package_editor_repeated_pivot_table_replacement_updates_final_state();
        test_package_editor_removes_pivot_table_and_preserves_cache_chain();
        test_package_editor_pivot_table_replacement_restores_prior_removal();
        test_package_editor_pivot_table_removal_overrides_prior_replacement();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
