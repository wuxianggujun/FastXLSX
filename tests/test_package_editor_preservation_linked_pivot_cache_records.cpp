#include "test_package_editor_preservation_linked_pivot_common.hpp"

void test_package_editor_replaces_pivot_cache_records_and_preserves_cache_definition()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-pivot-cache-records-output.xlsx");

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

    const std::string replacement_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="64"/></r><r><n v="128"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        replacement_cache_records, "pivot cache records stream rewrite");

    const auto* cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "pivot cache records replacement should be present in the edit plan");
    check(cache_records_plan->write_mode
            == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement should be stream-rewrite");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == replacement_cache_records,
        "pivot cache records replacement should write replacement records XML");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache records replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records replacement should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache records replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache records replacement should keep inbound cache records relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "pivot cache records replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPivotCache") != nullptr,
        "pivot cache records replacement should keep workbook pivot cache relationship");
    const auto* pivot_table_relationships =
        output_reader.relationships_for(pivot_table_part);
    check(pivot_table_relationships != nullptr,
        "pivot cache records replacement should keep pivot table relationships readable");
    check(pivot_table_relationships->find_by_id("rIdPivotCacheDef") != nullptr,
        "pivot cache records replacement should keep pivot table cache definition relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after replacement");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache records replacement should keep pivot cache records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records replacement should keep pivot cache definition content type override");
}

void test_package_editor_repeated_pivot_cache_records_replacement_updates_final_state()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-repeat-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-pivot-cache-records-output.xlsx");

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
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    const std::string stale_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="128"/></r><r><n v="256"/></r>)"
        R"(</pivotCacheRecords>)";
    const std::string final_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4">)"
        R"(<r><n v="128"/></r><r><n v="256"/></r><r><n v="512"/></r><r><n v="1024"/></r>)"
        R"(</pivotCacheRecords>)";

    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        stale_cache_records, "stale repeated pivot cache records stream rewrite");
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        final_cache_records, "final repeated pivot cache records stream rewrite");

    const auto* cache_records_plan = editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "repeated pivot cache records replacement should keep an active edit-plan part");
    check(cache_records_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache records replacement should keep final stream-rewrite mode");
    check(cache_records_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache records replacement should keep final reason");
    check(cache_records_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache records replacement should drop stale reason");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated pivot cache records replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part) != nullptr,
        "repeated pivot cache records replacement should keep cache records content type override");
    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) == nullptr,
        "repeated pivot cache records replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "repeated pivot cache records replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "repeated pivot cache records replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated pivot cache records replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated pivot cache records replacement should not rewrite workbook relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated pivot cache records replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated pivot cache records replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated pivot cache records replacement output plan should preserve calcChain policy");
    check(output_plan.relationship_target_audits.empty(),
        "repeated pivot cache records replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated pivot cache records replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated pivot cache records replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated pivot cache records replacement output plan should rewrite cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("final repeated") != std::string::npos,
        "repeated pivot cache records replacement output plan should keep final reason");
    check(output_cache_records_plan->reason.find("stale repeated") == std::string::npos,
        "repeated pivot cache records replacement output plan should drop stale reason");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated pivot cache records replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml") == final_cache_records,
        "repeated pivot cache records replacement should write final cache records payload");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "repeated pivot cache records replacement should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "repeated pivot cache records replacement should preserve pivot cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "repeated pivot cache records replacement should preserve pivot table bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated pivot cache records replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated pivot cache records replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated pivot cache records replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated pivot cache records replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated pivot cache records replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated pivot cache records replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated pivot cache records replacement should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "repeated pivot cache records replacement should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "repeated pivot cache records replacement should keep inbound cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after repeated replacement");
}

void test_package_editor_removes_pivot_cache_records_and_preserves_cache_definition()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-pivot-cache-records-output.xlsx");

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

    editor.remove_part(pivot_cache_records_part,
        "explicit pivot cache records part removal");

    check(editor.edit_plan().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal should clear the active edit-plan part");
    const auto* removed_cache_records =
        editor.edit_plan().find_removed_part(pivot_cache_records_part);
    check(removed_cache_records != nullptr,
        "pivot cache records removal should record removed-part audit");
    check(removed_cache_records->reason.find("pivot cache records part")
            != std::string::npos,
        "pivot cache records removal should retain the removal reason");
    check(removed_cache_records->reason.find("inbound relationship preserved")
            != std::string::npos,
        "pivot cache records removal should audit preserved inbound relationships");
    check(removed_cache_records->inbound_relationships.size() == 1,
        "pivot cache records removal should keep cache definition inbound audit");
    const auto& inbound = removed_cache_records->inbound_relationships.front();
    check(inbound.owner_part == pivot_cache_definition_part.value(),
        "pivot cache records removal should audit cache definition owner part");
    check(inbound.owner_entry == "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        "pivot cache records removal should audit cache definition owner entry");
    check(inbound.relationship_id == "rIdPivotRecords",
        "pivot cache records removal should audit cache records relationship id");
    check(inbound.relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records removal should audit raw cache records target");
    check(inbound.target_part == pivot_cache_records_part,
        "pivot cache records removal should audit normalized cache records target");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part)
            == nullptr,
        "pivot cache records removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache records removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep pivot cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") == entries.end(),
        "pivot cache records removal output should omit pivot cache records part");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache records removal output should keep pivot cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != entries.end(),
        "pivot cache records removal output should keep cache definition relationships");
    check(entries.find("xl/pivotTables/pivotTable1.xml") != entries.end(),
        "pivot cache records removal output should keep pivot table");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_records_part) == nullptr,
        "pivot cache records removal output should remove records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records removal output should keep cache definition content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot cache records removal content types XML should omit records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records removal should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records removal should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records removal should preserve pivot cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records removal should preserve unknown bytes");

    const auto* cache_definition_relationships =
        output_reader.relationships_for(pivot_cache_definition_part);
    check(cache_definition_relationships != nullptr,
        "pivot cache records removal should keep cache definition relationships readable");
    check(cache_definition_relationships->find_by_id("rIdPivotRecords") != nullptr,
        "pivot cache records removal should keep inbound cache records relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after records removal");
}

void test_package_editor_pivot_cache_records_replacement_restores_prior_removal()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-replace-after-remove-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-pivot-cache-records-output.xlsx");

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
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    editor.remove_part(pivot_cache_records_part, "temporary pivot cache records removal");
    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) != nullptr,
        "setup should record removed pivot cache records before replacement restore");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "setup should not invent pivot cache records owner relationships omission");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "setup should remove pivot cache records from manifest before restore");

    const std::string restored_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="3">)"
        R"(<r><n v="256"/></r><r><n v="512"/></r><r><n v="1024"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        restored_cache_records, "restored pivot cache records after removal");

    check(editor.edit_plan().find_removed_part(pivot_cache_records_part) == nullptr,
        "pivot cache records replacement after removal should clear stale removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal should not keep owner relationships omission");
    const auto* cache_records_plan =
        editor.edit_plan().find_part(pivot_cache_records_part);
    check(cache_records_plan != nullptr,
        "pivot cache records replacement after removal should restore active edit-plan part");
    check(cache_records_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement after removal should keep final write mode");
    check(cache_records_plan->reason.find("after removal") != std::string::npos,
        "pivot cache records replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, pivot_cache_records_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "pivot cache records replacement after removal should restore manifest write mode");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal should not invent owner relationships audit");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records replacement after removal should keep content types audit");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should restore source content types audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records replacement after removal should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache records replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache records replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache records replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "pivot cache records replacement after removal output plan should clear stale removed parts");
    check(output_plan.removed_package_entries.empty(),
        "pivot cache records replacement after removal output plan should clear stale omitted entries");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "pivot cache records replacement after removal output plan should rewrite cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true, pivot_cache_records_part.value(),
        "pivot cache records replacement after removal output plan should classify rewritten cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("after removal") != std::string::npos,
        "pivot cache records replacement after removal output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "pivot cache records replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache records replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve cache definition relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") != entries.end(),
        "pivot cache records replacement after removal output should restore records entry");
    check(entries.find(pivot_cache_records_relationships_entry) == entries.end(),
        "pivot cache records replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, pivot_cache_records_part.zip_path());
    check(output_reader.read_entry("xl/pivotCache/pivotCacheRecords1.xml")
            == restored_cache_records,
        "pivot cache records replacement after removal should write restored payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "pivot cache records replacement after removal should preserve content types bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records replacement after removal should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records replacement after removal should preserve cache definition relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records replacement after removal should preserve pivot table bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records replacement after removal should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after records restore");
    check(output_reader.content_types().override_for(pivot_cache_records_part) != nullptr,
        "pivot cache records replacement after removal should keep records content type override");
}

void test_package_editor_pivot_cache_records_removal_overrides_prior_replacement()
{
    const PivotSourcePackage source =
        write_pivot_source_package(
            "fastxlsx-package-editor-remove-after-replace-pivot-cache-records-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-pivot-cache-records-output.xlsx");

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
    const std::string pivot_cache_records_relationships_entry =
        "xl/pivotCache/_rels/pivotCacheRecords1.xml.rels";

    const std::string stale_cache_records =
        R"(<pivotCacheRecords xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2">)"
        R"(<r><n v="2048"/></r><r><n v="4096"/></r>)"
        R"(</pivotCacheRecords>)";
    replace_part_with_memory_chunks(editor, pivot_cache_records_part,
        stale_cache_records, "stale pivot cache records replacement before removal");
    check(editor.edit_plan().find_part(pivot_cache_records_part) != nullptr,
        "setup should record active pivot cache records replacement before removal");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "setup should not invent owner relationships before records removal");

    editor.remove_part(pivot_cache_records_part,
        "final pivot cache records removal after replacement");

    check(editor.edit_plan().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement should clear active replacement");
    const auto* removed_cache_records =
        editor.edit_plan().find_removed_part(pivot_cache_records_part);
    check(removed_cache_records != nullptr,
        "pivot cache records removal after replacement should record removed-part audit");
    check(removed_cache_records->reason.find("after replacement") != std::string::npos,
        "pivot cache records removal after replacement should keep final removal reason");
    check(removed_cache_records->inbound_relationships.size() == 1,
        "pivot cache records removal after replacement should keep cache definition inbound audit");
    const auto& inbound = removed_cache_records->inbound_relationships.front();
    check(inbound.owner_part == pivot_cache_definition_part.value(),
        "pivot cache records removal after replacement should audit cache definition owner part");
    check(inbound.owner_entry == "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        "pivot cache records removal after replacement should audit cache definition relationships entry");
    check(inbound.relationship_id == "rIdPivotRecords",
        "pivot cache records removal after replacement should audit records relationship id");
    check(inbound.relationship_target == "pivotCacheRecords1.xml",
        "pivot cache records removal after replacement should audit raw records target");
    check(inbound.target_part == pivot_cache_records_part,
        "pivot cache records removal after replacement should audit normalized records target");
    check(editor.edit_plan().find_removed_package_entry(
              pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry(pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement should not keep active owner relationships audit");
    check(editor.manifest().find_part(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(pivot_cache_records_part)
            == nullptr,
        "pivot cache records removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "pivot cache records removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "pivot cache records removal after replacement content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(pivot_table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep pivot table copy-original");
    check(editor.edit_plan().find_part(pivot_cache_definition_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep cache definition copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "pivot cache records removal after replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "pivot cache records removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "pivot cache records removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "pivot cache records removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "pivot cache records removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == pivot_cache_records_part,
        "pivot cache records removal after replacement output plan should expose removed records");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "pivot cache records removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "pivot cache records removal after replacement output plan should expose removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "pivot cache records removal after replacement output plan should not invent owner relationships omission");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "pivot cache records removal after replacement output plan should omit cache records");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", true,
        pivot_cache_records_part.value(),
        "pivot cache records removal after replacement output plan should classify omitted cache records");
    const auto* output_cache_records_plan =
        find_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheRecords1.xml");
    check(output_cache_records_plan->reason.find("after replacement") != std::string::npos,
        "pivot cache records removal after replacement output plan should keep final removal reason");
    check(output_cache_records_plan->inbound_relationships.size() == 1,
        "pivot cache records removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/pivotCache/pivotCacheRecords1.xml", pivot_cache_definition_part.value(),
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", "rIdPivotRecords",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheRecords",
        "pivotCacheRecords1.xml", pivot_cache_records_part,
        "pivot cache records removal after replacement output plan should keep cache definition inbound audit");
    check(find_output_entry_plan(output_plan.entries, pivot_cache_records_relationships_entry)
            == nullptr,
        "pivot cache records removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "pivot cache records removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "pivot cache records removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "pivot cache records removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/pivotTable1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve pivot table");
    check_output_entry_plan(output_plan.entries, "xl/pivotTables/_rels/pivotTable1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve pivot table relationships");
    check_output_entry_plan(output_plan.entries, "xl/pivotCache/pivotCacheDefinition1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve cache definition");
    check_output_entry_plan(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve cache definition relationships");
    check_output_entry_part_context(output_plan.entries,
        "xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels", false,
        "",
        "pivot cache records removal after replacement output plan should keep cache definition relationships as metadata entry");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "pivot cache records removal after replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/pivotCache/pivotCacheRecords1.xml") == entries.end(),
        "pivot cache records removal after replacement output should omit records part");
    check(entries.find(pivot_cache_records_relationships_entry) == entries.end(),
        "pivot cache records removal after replacement output should not invent owner relationships");
    check(entries.find("xl/pivotCache/pivotCacheDefinition1.xml") != entries.end(),
        "pivot cache records removal after replacement output should keep cache definition");
    check(entries.find("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            != entries.end(),
        "pivot cache records removal after replacement output should keep cache definition relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(pivot_cache_records_part) == nullptr,
        "pivot cache records removal after replacement output should remove records content type override");
    check(output_reader.content_types().override_for(pivot_cache_definition_part) != nullptr,
        "pivot cache records removal after replacement output should keep cache definition content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/pivotCache/pivotCacheRecords1.xml",
        "pivot cache records removal after replacement content types should omit records override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "pivot cache records removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "pivot cache records removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "pivot cache records removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "pivot cache records removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "pivot cache records removal after replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/pivotTables/pivotTable1.xml") == source.pivot_table,
        "pivot cache records removal after replacement should preserve pivot table bytes");
    check(output_reader.read_entry("xl/pivotTables/_rels/pivotTable1.xml.rels")
            == source.pivot_table_relationships,
        "pivot cache records removal after replacement should preserve pivot table relationships bytes");
    check(output_reader.read_entry("xl/pivotCache/pivotCacheDefinition1.xml")
            == source.pivot_cache_definition,
        "pivot cache records removal after replacement should preserve cache definition bytes");
    check(output_reader.read_entry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels")
            == source.pivot_cache_definition_relationships,
        "pivot cache records removal after replacement should preserve cache definition relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "pivot cache records removal after replacement should preserve unknown bytes");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(pivot_cache_definition_part)
                ->find_by_id("rIdPivotRecords")
            != nullptr,
        "relationship graph should keep cache records relationship after final records removal");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "preservation-linked-pivot-cache-records")) {
            throw TestFailure("wrong shard for preservation-linked-pivot-cache-records executable");
        }

        test_package_editor_replaces_pivot_cache_records_and_preserves_cache_definition();
        test_package_editor_repeated_pivot_cache_records_replacement_updates_final_state();
        test_package_editor_removes_pivot_cache_records_and_preserves_cache_definition();
        test_package_editor_pivot_cache_records_replacement_restores_prior_removal();
        test_package_editor_pivot_cache_records_removal_overrides_prior_replacement();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
