#include "test_package_editor_preservation_linked_pivot_common.hpp"

void test_package_editor_replaces_pivot_cache_definition_and_preserves_cache_records()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-cache-definition-output.xlsx");

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

    const std::string replacement_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="9">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B4" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="PatchedValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, replacement_cache_definition,
        "pivot cache definition local-DOM rewrite");

    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "pivot cache definition replacement should be present in the edit plan");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement should be staged stream-rewrite");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement should mirror write mode into manifest");
    const auto* cache_definition_relationships_plan =
        editor.edit_plan().find_package_entry(
            "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels");
    check(cache_definition_relationships_plan != nullptr,
        "pivot cache definition replacement should audit preserved owner relationships");
    check(cache_definition_relationships_plan->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition owner relationships should remain copy-original");
    check(cache_definition_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition owner relationships audit should keep structured role");
    check(cache_definition_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache definition owner relationships audit should keep owner part");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == replacement_cache_definition,
        "pivot cache definition replacement should write replacement cache definition XML");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache definition replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache definition replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache definition replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache definition replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache definition replacement should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache definition replacement should keep pivot table cache definition relationship");
    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache definition replacement should keep owner relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache definition replacement should keep pivot cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after cache replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache definition replacement should keep pivot cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition replacement should keep pivot cache records content type override");
}

void test_package_editor_repeated_pivot_cache_definition_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-repeat-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-cache-definition-output.xlsx");

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
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    const std::string stale_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="11">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B6" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="StaleValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    const std::string final_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="12">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="B2:C7" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="FinalValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";

    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, stale_cache_definition,
        "stale repeated pivot cache definition local-DOM rewrite");
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, final_cache_definition,
        "final repeated pivot cache definition local-DOM rewrite");

    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "repeated pivot cache definition replacement should keep an active edit-plan part");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache definition replacement should keep final stream-rewrite mode");
    check(cache_definition_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache definition replacement should keep final reason");
    check(cache_definition_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache definition replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache definition replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part) != nullptr,
        "repeated pivot cache definition replacement should keep cache definition content type override");
    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) == nullptr,
        "repeated pivot cache definition replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            == nullptr,
        "repeated pivot cache definition replacement should not leave owner relationships omission");
    const auto* cache_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_cache_relationships_entry);
    check(cache_relationships_audit != nullptr,
        "repeated pivot cache definition replacement should preserve owner relationships audit");
    check(cache_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should preserve owner relationships bytes");
    check(cache_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot cache definition replacement should keep owner relationship audit role");
    check(cache_relationships_audit->owner_part == pivot_cache_definition_part.value(),
        "repeated pivot cache definition replacement should keep owner relationship context");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot cache definition replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot cache definition replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache definition replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot cache definition replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot cache definition replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot cache definition replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot cache definition replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot cache definition replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot cache definition replacement output plan should rewrite cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache definition replacement output plan should keep final reason");
    check(output_cache_definition_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache definition replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve owner relationships");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "repeated pivot cache definition replacement output plan should keep owner relationship audit role");
    check(output_cache_relationships_plan->owner_part == pivot_cache_definition_part.value(),
        "repeated pivot cache definition replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve pivot cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache definition replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader,
        pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == final_cache_definition,
        "repeated pivot cache definition replacement should write final cache definition payload");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot cache definition replacement should preserve owner relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "repeated pivot cache definition replacement should preserve pivot cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "repeated pivot cache definition replacement should preserve pivot table bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot cache definition replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot cache definition replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot cache definition replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot cache definition replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot cache definition replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot cache definition replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot cache definition replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after repeated replacement");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep pivot cache records relationship after repeated replacement");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "repeated pivot cache definition replacement should keep cache definition content type override");
}

void test_package_editor_removes_pivot_cache_definition_and_preserves_cache_records()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-cache-definition-output.xlsx");

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

    editor.remove_part(pivot_cache_definition_part,
        "explicit pivot cache definition part removal");

    check(editor.edit_plan().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should clear the active edit-plan part");
    const auto* removed_cache_definition =
        editor.edit_plan().find_removed_part(pivot_cache_definition_part);
    check(removed_cache_definition != nullptr,
        "pivot cache definition removal should record removed-part audit");
    check(removed_cache_definition->reason.find("pivot cache definition part")
            != std::string::npos,
        "pivot cache definition removal should retain the removal reason");
    check(removed_cache_definition->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot cache definition removal should audit preserved inbound relationships");
    check(removed_cache_definition->inbound_relationships.size() == 2,
        "pivot cache definition removal should keep workbook and pivot table inbound audits");
    bool found_workbook_inbound = false;
    bool found_pivot_table_inbound = false;
    for (const auto& inbound : removed_cache_definition->inbound_relationships) {
        if (inbound.owner_part == workbook_part.value()
            && inbound.owner_entry == "xl/_rels/workbook.xml.rels"
            && inbound.relationship_id == "rIdPivotCache"
            && inbound.relationship_target == "pivotCache/pivotCacheDefinition1.xml"
            && inbound.target_part == pivot_cache_definition_part) {
            found_workbook_inbound = true;
        }
        if (inbound.owner_part == pivot_table_part.value()
            && inbound.owner_entry == "xl/pivotTables/_rels/pivotTable1.xml.rels"
            && inbound.relationship_id == "rIdPivotCacheDef"
            && inbound.relationship_target == "../pivotCache/pivotCacheDefinition1.xml"
            && inbound.target_part == pivot_cache_definition_part) {
            found_pivot_table_inbound = true;
        }
    }
    check(found_workbook_inbound,
        "pivot cache definition removal should audit workbook inbound relationship");
    check(found_pivot_table_inbound,
        "pivot cache definition removal should audit pivot table inbound relationship");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part)
            == nullptr,
        "pivot cache definition removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache definition removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != nullptr,
        "pivot cache definition removal should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(
              "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == nullptr,
        "pivot cache definition removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") == entries.end(),
        "pivot cache definition removal output should omit pivot cache definition part");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels") == entries.end(),
        "pivot cache definition removal output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache definition removal output should keep pivot cache records");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache definition removal output should keep pivot table");
    check(entries.find("xl/pivotTables/_rels/pivotTable1.xml.rels") != entries.end(),
        "pivot cache definition removal output should keep pivot table relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal output should remove cache definition content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition removal output should keep cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition removal content types XML should omit cache definition override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition removal should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition removal should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition removal should preserve pivot cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition removal should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache definition removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache definition removal should keep inbound workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache definition removal should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache definition removal should keep inbound pivot table cache relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after removal");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship after removal");
    check(output_reader.relationships_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal should not keep owner relationships for absent part");
}

void test_package_editor_pivot_cache_definition_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-cache-definition-output.xlsx");

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
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    editor.remove_part(pivot_cache_definition_part,
        "temporary pivot cache definition removal");
    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) != nullptr,
        "setup should record removed pivot cache definition before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "setup should omit pivot cache definition owner relationships before restore");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "setup should remove pivot cache definition from manifest before restore");

    const std::string restored_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="10">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B5" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="RestoredValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, restored_cache_definition,
        "restored pivot cache definition after removal");

    check(editor.edit_plan().find_removed_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            == nullptr,
        "pivot cache definition replacement after removal should clear stale owner relationships omission");
    const auto* cache_definition_plan =
        editor.edit_plan().find_part(pivot_cache_definition_part);
    check(cache_definition_plan != nullptr,
        "pivot cache definition replacement after removal should restore active edit-plan part");
    check(cache_definition_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement after removal should keep final staged write mode");
    check(cache_definition_plan->reason.find("after removal") != std::string::npos,
        "pivot cache definition replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_cache_definition_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache definition replacement after removal should restore manifest write mode");

    const auto* cache_relationships_audit =
        editor.edit_plan().find_package_entry(pivot_cache_relationships_entry);
    check(cache_relationships_audit != nullptr,
        "pivot cache definition replacement after removal should restore owner relationships audit");
    check(cache_relationships_audit->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal owner relationships should be copy-original");
    check(cache_relationships_audit->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition replacement after removal owner relationships should keep source role");
    check(cache_relationships_audit->owner_part == pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal owner relationships should keep owner part");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should restore source content types audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep pivot cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache definition replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache definition replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache definition replacement after removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot cache definition replacement after removal output plan should rewrite cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal output plan should classify rewritten cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("after removal") != std::string::npos,
        "pivot cache definition replacement after removal output plan should keep replacement reason");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_cache_relationships_entry,
        false, "",
        "pivot cache definition replacement after removal output plan should classify owner relationships as metadata");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition replacement after removal output plan should classify owner relationships metadata");
    check(output_cache_relationships_plan->owner_part == pivot_cache_definition_part.value(),
        "pivot cache definition replacement after removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot cache definition replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache definition replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache definition replacement after removal output should restore cache definition entry");
    check(entries.find(pivot_cache_relationships_entry) != entries.end(),
        "pivot cache definition replacement after removal output should restore owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_definition_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == restored_cache_definition,
        "pivot cache definition replacement after removal should write restored payload");
    check(output_reader.read_entry(pivot_cache_relationships_entry)
            == source.pivot_cache_definition_relationships,
        "pivot cache definition replacement after removal should preserve owner relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache definition replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition replacement after removal should preserve cache records bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition replacement after removal should preserve pivot table bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition replacement after removal should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook pivot cache relationship after restore");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache relationship after restore");
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after restore");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache definition replacement after removal should keep content type override");
}

void test_package_editor_pivot_cache_definition_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-cache-definition-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-cache-definition-output.xlsx");

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
    const std::string pivot_cache_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels";

    const std::string stale_cache_definition =
        R"(<pivotCacheDefinition xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" refreshOnLoad="1" refreshedVersion="11">)"
        R"(<cacheSource type="worksheet"><worksheetSource ref="A1:B6" sheet="Sheet1"/></cacheSource>)"
        R"(<cacheFields count="1"><cacheField name="StaleValue" numFmtId="0"><sharedItems containsNumber="1"/></cacheField></cacheFields>)"
        R"(<cacheRecords r:id="rIdPivotRecords"/>)"
        R"(</pivotCacheDefinition>)";
    replace_part_with_memory_chunks(editor, pivot_cache_definition_part, stale_cache_definition,
        "stale pivot cache definition replacement before removal");
    check(editor.edit_plan().find_part(pivot_cache_definition_part) != nullptr,
        "setup should record active pivot cache definition replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "setup should preserve owner relationships before final cache definition removal");

    editor.remove_part(pivot_cache_definition_part,
        "final pivot cache definition removal after replacement");

    check(editor.edit_plan().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should clear active replacement");
    const auto* removed_cache_definition =
        editor.edit_plan().find_removed_part(pivot_cache_definition_part);
    check(removed_cache_definition != nullptr,
        "pivot cache definition removal after replacement should record removed-part audit");
    check(removed_cache_definition->reason.find("after replacement") != std::string::npos,
        "pivot cache definition removal after replacement should keep final removal reason");
    check(removed_cache_definition->inbound_relationships.size() == 2,
        "pivot cache definition removal after replacement should keep workbook and pivot table inbound audits");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_relationships_entry)
            != nullptr,
        "pivot cache definition removal after replacement should omit owner relationships entry");
    check(editor.edit_plan().find_package_entry(pivot_cache_relationships_entry) == nullptr,
        "pivot cache definition removal after replacement should clear active owner relationships audit");
    check(editor.manifest().find_part(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_cache_definition_part)
            == nullptr,
        "pivot cache definition removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache definition removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache definition removal after replacement content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_records_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep cache records copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache definition removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache definition removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache definition removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache definition removal after replacement output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache definition removal after replacement output plan should omit cache definition");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", true,
        pivot_cache_definition_part.value(),
        "pivot cache definition removal after replacement output plan should classify omitted cache definition");
    const auto* output_cache_definition_plan =
        find_output_entry_plan(output_plan.entries,
            "xl/pivotCache/pivotCacheDefinition1.xml");
    check(output_cache_definition_plan->reason.find("after replacement")
            != std::string::npos,
        "pivot cache definition removal after replacement output plan should keep final removal reason");
    check(output_cache_definition_plan->inbound_relationships.size() == 2,
        "pivot cache definition removal after replacement output plan should expose inbound audits");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", workbook_part.value(),
        "xl/_rels/workbook.xml.rels", "rIdPivotCache",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "pivotCache/pivotCacheDefinition1.xml", pivot_cache_definition_part,
        "pivot cache definition removal after replacement output plan should keep workbook inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheDefinition1.xml", pivot_table_part.value(),
        "xl/pivotTables/_rels/pivotTable1.xml.rels", "rIdPivotCacheDef",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition",
        "../pivotCache/pivotCacheDefinition1.xml", pivot_cache_definition_part,
        "pivot cache definition removal after replacement output plan should keep pivot table inbound audit");
    check_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry,
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache definition removal after replacement output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries, pivot_cache_relationships_entry,
        false, "",
        "pivot cache definition removal after replacement output plan should classify owner relationships as metadata");
    const auto* output_cache_relationships_plan =
        find_output_entry_plan(output_plan.entries, pivot_cache_relationships_entry);
    check(output_cache_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "pivot cache definition removal after replacement output plan should classify owner relationships metadata");
    check(output_cache_relationships_plan->owner_part
            == pivot_cache_definition_part.value(),
        "pivot cache definition removal after replacement output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot cache definition removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "pivot cache definition removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache definition removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve cache records");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache definition removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") == entries.end(),
        "pivot cache definition removal after replacement output should omit cache definition part");
    check(entries.find(pivot_cache_relationships_entry) == entries.end(),
        "pivot cache definition removal after replacement output should omit owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache definition removal after replacement output should keep cache records");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache definition removal after replacement output should keep pivot table");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement output should remove content type override");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache definition removal after replacement output should keep cache records content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheDefinition1.xml",
        "pivot cache definition removal after replacement content types should omit cache definition override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache definition removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache definition removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache definition removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache definition removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache definition removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache definition removal after replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache definition removal after replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == source.pivot_cache_records,
        "pivot cache definition removal after replacement should preserve cache records bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache definition removal after replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPivotCache")
            != nullptr,
        "relationship graph should keep workbook cache definition relationship after final removal");
    check(output_graph.relationships_for(pivot_table_part)->find_by_id("rIdPivotCacheDef")
            != nullptr,
        "relationship graph should keep pivot table cache definition relationship after final removal");
    check(output_reader.relationships_for(pivot_cache_definition_part) == nullptr,
        "pivot cache definition removal after replacement should not keep owner relationships");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "preservation-linked-pivot-cache-definition")) {
            throw TestFailure("wrong shard for preservation-linked-pivot-cache-definition executable");
        }

        test_package_editor_replaces_pivot_cache_definition_and_preserves_cache_records();
        test_package_editor_repeated_pivot_cache_definition_replacement_updates_final_state();
        test_package_editor_removes_pivot_cache_definition_and_preserves_cache_records();
        test_package_editor_pivot_cache_definition_replacement_restores_prior_removal();
        test_package_editor_pivot_cache_definition_removal_overrides_prior_replacement();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
