#include "test_package_editor_preservation_removal_common.hpp"

class ScopedPackageEditorPartRemovalStagedHook {
public:
    explicit ScopedPackageEditorPartRemovalStagedHook(
        fastxlsx::detail::PackageEditorPartRemovalStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_part_removal_staged_hook(hook);
    }

    ~ScopedPackageEditorPartRemovalStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_part_removal_staged_hook(nullptr);
    }

    ScopedPackageEditorPartRemovalStagedHook(
        const ScopedPackageEditorPartRemovalStagedHook&) = delete;
    ScopedPackageEditorPartRemovalStagedHook& operator=(
        const ScopedPackageEditorPartRemovalStagedHook&) = delete;
};

void fail_package_editor_part_removal_after_staging()
{
    throw std::runtime_error("injected part removal commit failure");
}

void test_package_editor_part_removal_staging_failure_preserves_prior_plan_and_retries()
{
    const LinkedObjectSourcePackage source = write_linked_object_source_package(
        "fastxlsx-package-editor-remove-staging-failure-source.xlsx");
    const std::filesystem::path failed_output = output_path(
        "fastxlsx-package-editor-remove-staging-failure-output.xlsx");
    const std::filesystem::path retry_output = output_path(
        "fastxlsx-package-editor-remove-staging-retry-output.xlsx");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const std::string prior_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Queued" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string prior_table =
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(id="7" name="QueuedTable" displayName="QueuedTable" ref="A1:B2"/>)";

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_part(workbook_part, prior_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "prior workbook replacement before injected part removal failure");
    replace_part_with_memory_chunks(editor, table_part, prior_table,
        "prior table replacement before injected part removal failure");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    bool failed = false;
    {
        ScopedPackageEditorPartRemovalStagedHook hook(
            fail_package_editor_part_removal_after_staging);
        try {
            editor.remove_part(table_part,
                "table removal with injected commit failure");
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "injected part removal commit failure",
                "part removal staged failure should preserve injected context");
        }
    }
    check(failed, "PackageEditor should surface injected part removal staging failure");
    check(editor.edit_plan().size() == initial_plan_size,
        "part removal staging failure should preserve prior edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "part removal staging failure should preserve prior notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "part removal staging failure should preserve package-entry audits");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "part removal staging failure should preserve removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "part removal staging failure should preserve omitted-entry audits");
    check(editor.edit_plan().find_part(table_part) != nullptr,
        "part removal staging failure should preserve the prior table replacement");
    check(editor.edit_plan().find_removed_part(table_part) == nullptr,
        "part removal staging failure should not publish table removal audit");
    check(editor.manifest().find_part(table_part) != nullptr,
        "part removal staging failure should preserve table manifest state");
    check(editor.manifest().content_types().override_for(table_part) != nullptr,
        "part removal staging failure should preserve table content type");

    editor.save_as(failed_output);
    const fastxlsx::detail::PackageReader failed_reader =
        fastxlsx::detail::PackageReader::open(failed_output);
    check(failed_reader.read_entry("xl/workbook.xml") == prior_workbook,
        "part removal staging failure should preserve prior workbook replacement");
    check(failed_reader.read_entry("xl/tables/table1.xml") == prior_table,
        "part removal staging failure should preserve prior table replacement");
    check(failed_reader.read_entry("[Content_Types].xml") == source.content_types,
        "part removal staging failure should preserve content types bytes");
    check(failed_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "part removal staging failure should preserve inbound worksheet relationships");

    editor.remove_part(table_part, "table removal retry after staged failure");
    editor.save_as(retry_output);
    const fastxlsx::detail::PackageReader retry_reader =
        fastxlsx::detail::PackageReader::open(retry_output);
    check(retry_reader.read_entry("xl/workbook.xml") == prior_workbook,
        "part removal retry should preserve prior workbook replacement");
    check(retry_reader.find_entry("xl/tables/table1.xml") == nullptr,
        "part removal retry should omit the target table part");
    check(retry_reader.content_types().override_for(table_part) == nullptr,
        "part removal retry should remove the table content type override");
    check(retry_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "part removal retry should preserve audited inbound worksheet relationships");
}

void test_package_editor_removes_unknown_extension_and_omits_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-opaque-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-opaque-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.remove_part(opaque_extension_part, "explicit opaque extension removal");

    check(editor.edit_plan().find_part(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should clear the active edit-plan part");
    const auto* removed_opaque = editor.edit_plan().find_removed_part(opaque_extension_part);
    check(removed_opaque != nullptr,
        "explicit unknown extension removal should record removed-part audit");
    check(removed_opaque->reason.find("opaque extension") != std::string::npos,
        "explicit unknown extension removal should retain the removal reason");
    check(removed_opaque->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit unknown extension removal should audit preserved inbound relationships");
    check(removed_opaque->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit unknown extension removal inbound audit should include owner part");
    check(removed_opaque->reason.find("rId9") != std::string::npos,
        "explicit unknown extension removal inbound audit should include relationship id");
    check(removed_opaque->reason.find("../../custom/opaque-extension.bin")
            != std::string::npos,
        "explicit unknown extension removal inbound audit should include original target");
    check(removed_opaque->inbound_relationships.size() == 1,
        "explicit unknown extension removal should keep structured inbound audit");
    const auto& opaque_inbound = removed_opaque->inbound_relationships.front();
    check(opaque_inbound.owner_part == worksheet_part.value(),
        "explicit unknown extension removal should keep inbound owner part");
    check(opaque_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit unknown extension removal should keep inbound owner relationship entry");
    check(opaque_inbound.relationship_id == "rId9",
        "explicit unknown extension removal should keep inbound relationship id");
    check(opaque_inbound.relationship_type
            == "https://fastxlsx.invalid/relationships/opaque-extension",
        "explicit unknown extension removal should keep inbound relationship type");
    check(opaque_inbound.relationship_target == "../../custom/opaque-extension.bin",
        "explicit unknown extension removal should keep inbound raw target");
    check(opaque_inbound.target_part == opaque_extension_part,
        "explicit unknown extension removal should keep normalized target part");
    check(editor.manifest().find_part(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should remove the part from the manifest");
    const auto* removed_opaque_relationships =
        editor.edit_plan().find_removed_package_entry("custom/_rels/opaque-extension.bin.rels");
    check(removed_opaque_relationships != nullptr,
        "explicit unknown extension removal should omit source-owned relationships");
    check(removed_opaque_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit unknown extension removal should keep source relationship audit role");
    check(removed_opaque_relationships->owner_part == opaque_extension_part.value(),
        "explicit unknown extension removal should keep owner part in removed relationship audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "explicit unknown extension removal should not rewrite content types when only default applies");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "explicit unknown extension removal should not rewrite package relationships");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "explicit unknown extension removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "explicit unknown extension removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "explicit unknown extension removal output plan should not invent dependency audits");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit unknown extension removal output plan should omit target part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, opaque_extension_part.value(),
        "explicit unknown extension removal output plan should classify omitted target");
    const auto* output_opaque_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(output_opaque_plan->reason.find("opaque extension") != std::string::npos,
        "explicit unknown extension removal output plan should keep removal reason");
    check(output_opaque_plan->inbound_relationships.size() == 1,
        "explicit unknown extension removal output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "custom/opaque-extension.bin", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId9",
        "https://fastxlsx.invalid/relationships/opaque-extension",
        "../../custom/opaque-extension.bin", opaque_extension_part,
        "explicit unknown extension removal output plan should keep inbound context");
    check_output_entry_plan(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit unknown extension removal output plan should omit owner relationships");
    check_output_entry_part_context(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", false, "",
        "explicit unknown extension removal output plan should classify owner relationships as metadata");
    const auto* output_opaque_relationships_plan = find_output_entry_plan(
        output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(output_opaque_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit unknown extension removal output plan should classify owner relationships metadata");
    check(output_opaque_relationships_plan->owner_part == opaque_extension_part.value(),
        "explicit unknown extension removal output plan should keep owner relationship context");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "explicit unknown extension removal output plan should not classify content types as package part");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve package relationships");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "explicit unknown extension removal output plan should not classify package relationships as package part");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit unknown extension removal output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("custom/opaque-extension.bin") == entries.end(),
        "explicit unknown extension removal output should omit target part");
    check(entries.find("custom/_rels/opaque-extension.bin.rels") == entries.end(),
        "explicit unknown extension removal output should omit target owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "explicit unknown extension removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit unknown extension removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit unknown extension removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit unknown extension removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit unknown extension removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(opaque_extension_part) == nullptr,
        "explicit unknown extension removal should not keep owner relationships for absent part");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit unknown extension removal should keep worksheet relationships readable");
    const auto* opaque_link = worksheet_relationships->find_by_id("rId9");
    check(opaque_link != nullptr,
        "explicit unknown extension removal should keep inbound unknown relationship id");
    check(opaque_link->target == "../../custom/opaque-extension.bin",
        "explicit unknown extension removal should not rewrite inbound relationship target");
    check(output_reader.content_types().default_for("bin") != nullptr,
        "explicit unknown extension removal should keep BIN default content type");
}

void test_package_editor_removes_workbook_and_preserves_package_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-workbook-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-workbook-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(workbook_part, "explicit workbook part removal");

    check(editor.edit_plan().find_part(workbook_part) == nullptr,
        "explicit workbook removal should clear the active edit-plan part");
    const auto* removed_workbook = editor.edit_plan().find_removed_part(workbook_part);
    check(removed_workbook != nullptr,
        "explicit workbook removal should record removed-part audit");
    check(removed_workbook->reason.find("workbook part") != std::string::npos,
        "explicit workbook removal should retain the removal reason");
    check(removed_workbook->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit workbook removal should audit preserved inbound relationships");
    check(removed_workbook->reason.find("package _rels/.rels") != std::string::npos
            || removed_workbook->reason.find("_rels/.rels") != std::string::npos,
        "explicit workbook removal inbound audit should include package relationships entry");
    check(removed_workbook->reason.find("rId1") != std::string::npos,
        "explicit workbook removal inbound audit should include package relationship id");
    check(removed_workbook->reason.find("xl/workbook.xml") != std::string::npos,
        "explicit workbook removal inbound audit should include package target");
    check(removed_workbook->inbound_relationships.size() == 1,
        "explicit workbook removal should keep one structured inbound audit");
    const auto& inbound = removed_workbook->inbound_relationships.front();
    check(inbound.owner_part.empty(),
        "explicit workbook removal should keep package inbound owner part empty");
    check(inbound.owner_entry == "_rels/.rels",
        "explicit workbook removal should keep package relationships entry");
    check(inbound.relationship_id == "rId1",
        "explicit workbook removal should keep package relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
        "explicit workbook removal should keep package relationship type");
    check(inbound.relationship_target == "xl/workbook.xml",
        "explicit workbook removal should keep package raw target");
    check(inbound.target_part == workbook_part,
        "explicit workbook removal should keep normalized workbook target part");
    check(editor.manifest().find_part(workbook_part) == nullptr,
        "explicit workbook removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(workbook_part) == nullptr,
        "explicit workbook removal should remove the workbook content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit workbook removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit workbook removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit workbook removal content types audit should keep structured role");
    const auto* removed_workbook_relationships =
        editor.edit_plan().find_removed_package_entry("xl/_rels/workbook.xml.rels");
    check(removed_workbook_relationships != nullptr,
        "explicit workbook removal should omit source-owned workbook relationships");
    check(removed_workbook_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit workbook owner relationships omission should keep source relationship role");
    check(removed_workbook_relationships->owner_part == workbook_part.value(),
        "explicit workbook owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "explicit workbook removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "explicit workbook removal should not rewrite package relationships");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit workbook removal should keep unknown extension copy-original");

    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit workbook removal output plan should omit workbook part");
    check_output_entry_part_context(output_plan, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "explicit workbook removal output plan should classify omitted workbook as package part");
    check_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit workbook removal output plan should omit workbook owner relationships");
    check_output_entry_part_context(output_plan, "xl/_rels/workbook.xml.rels", false, "",
        "explicit workbook removal output plan should classify owner relationships as metadata entry");
    const auto* output_workbook_relationships_plan =
        find_output_entry_plan(output_plan, "xl/_rels/workbook.xml.rels");
    check(output_workbook_relationships_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit workbook removal output plan should classify owner relationships omission");
    check(output_workbook_relationships_plan->owner_part == workbook_part.value(),
        "explicit workbook removal output plan should keep owner relationships context");
    check_output_entry_plan(output_plan, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "explicit workbook removal output plan should rewrite content types");
    check_output_entry_part_context(output_plan, "[Content_Types].xml", false, "",
        "explicit workbook removal output plan should classify content types as metadata entry");
    check_output_entry_plan(output_plan, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit workbook removal output plan should preserve package relationships");
    check_output_entry_part_context(output_plan, "_rels/.rels", false, "",
        "explicit workbook removal output plan should classify package relationships as metadata entry");
    check_output_entry_plan(output_plan, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "explicit workbook removal output plan should preserve unknown extension");
    check_output_entry_part_context(output_plan, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "explicit workbook removal output plan should classify unknown extension as package part");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/workbook.xml") == entries.end(),
        "explicit workbook removal output should omit workbook part");
    check(entries.find("xl/_rels/workbook.xml.rels") == entries.end(),
        "explicit workbook removal output should omit workbook owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(workbook_part) == nullptr,
        "explicit workbook removal output should remove workbook content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/workbook.xml",
        "explicit workbook removal content types XML should omit workbook override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit workbook removal should preserve package relationships bytes");
    check(output_reader.relationships_for(workbook_part) == nullptr,
        "explicit workbook removal should not keep owner relationships for absent workbook");
    const auto& package_relationships = output_reader.package_relationships();
    const auto* workbook_link = package_relationships.find_by_id("rId1");
    check(workbook_link != nullptr,
        "explicit workbook removal should keep inbound workbook relationship id");
    check(workbook_link->target == "xl/workbook.xml",
        "explicit workbook removal should not rewrite inbound workbook target");
    check(workbook_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit workbook removal should keep inbound workbook target mode");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit workbook removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit workbook removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit workbook removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit workbook removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit workbook removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit workbook removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit workbook removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit workbook removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit workbook removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit workbook removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit workbook removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit workbook removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit workbook removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit workbook removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(worksheet_part) != nullptr,
        "explicit workbook removal should keep worksheet content type override");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "explicit workbook removal should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit workbook removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit workbook removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit workbook removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit workbook removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit workbook removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit workbook removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit workbook removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit workbook removal should not promote PNG media default to override");
}

void test_package_editor_removes_worksheet_and_preserves_workbook_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-worksheet-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-worksheet-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName table_part("/xl/tables/table1.xml");
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName vba_part("/xl/vbaProject.bin");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(worksheet_part, "explicit worksheet part removal");

    check(editor.edit_plan().find_part(worksheet_part) == nullptr,
        "explicit worksheet removal should clear the active edit-plan part");
    const auto* removed_worksheet = editor.edit_plan().find_removed_part(worksheet_part);
    check(removed_worksheet != nullptr,
        "explicit worksheet removal should record removed-part audit");
    check(removed_worksheet->reason.find("worksheet part") != std::string::npos,
        "explicit worksheet removal should retain the removal reason");
    check(removed_worksheet->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit worksheet removal should audit preserved inbound relationships");
    check(removed_worksheet->reason.find("/xl/workbook.xml") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook owner part");
    check(removed_worksheet->reason.find("rId1") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook relationship id");
    check(removed_worksheet->reason.find("worksheets/sheet1.xml") != std::string::npos,
        "explicit worksheet removal inbound audit should include workbook target");
    check(removed_worksheet->inbound_relationships.size() == 1,
        "explicit worksheet removal should keep one structured inbound audit");
    const auto& inbound = removed_worksheet->inbound_relationships.front();
    check(inbound.owner_part == workbook_part.value(),
        "explicit worksheet removal should keep workbook owner part");
    check(inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "explicit worksheet removal should keep workbook relationships entry");
    check(inbound.relationship_id == "rId1",
        "explicit worksheet removal should keep workbook relationship id");
    check(inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",
        "explicit worksheet removal should keep workbook relationship type");
    check(inbound.relationship_target == "worksheets/sheet1.xml",
        "explicit worksheet removal should keep workbook raw target");
    check(inbound.target_part == worksheet_part,
        "explicit worksheet removal should keep normalized worksheet target part");
    check(editor.manifest().find_part(worksheet_part) == nullptr,
        "explicit worksheet removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(worksheet_part) == nullptr,
        "explicit worksheet removal should remove the worksheet content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit worksheet removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit worksheet removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit worksheet removal content types audit should keep structured role");
    const auto* removed_worksheet_relationships =
        editor.edit_plan().find_removed_package_entry("xl/worksheets/_rels/sheet1.xml.rels");
    check(removed_worksheet_relationships != nullptr,
        "explicit worksheet removal should omit source-owned worksheet relationships");
    check(removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit worksheet owner relationships omission should keep source relationship role");
    check(removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "explicit worksheet owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "explicit worksheet removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep drawing copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep table copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit worksheet removal should keep unknown extension copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.removed_parts.size() == editor.edit_plan().removed_parts().size(),
        "explicit worksheet removal output plan should mirror removed-part audits");
    const auto output_removed_worksheet =
        std::find_if(output_plan.removed_parts.begin(), output_plan.removed_parts.end(),
            [&](const fastxlsx::detail::EditPlanRemovedPart& removed_part) {
                return removed_part.part_name == worksheet_part;
            });
    check(output_removed_worksheet != output_plan.removed_parts.end(),
        "explicit worksheet removal output plan should expose removed worksheet audit");
    check(output_removed_worksheet->reason.find("worksheet part") != std::string::npos,
        "explicit worksheet removal output plan should keep removed worksheet reason");
    check(output_removed_worksheet->inbound_relationships.size() == 1,
        "explicit worksheet removal output plan should keep removed worksheet inbound audit");
    check(output_removed_worksheet->inbound_relationships.front().owner_part
            == workbook_part.value(),
        "explicit worksheet removal output plan should keep removed worksheet inbound owner");
    check(output_removed_worksheet->inbound_relationships.front().relationship_id == "rId1",
        "explicit worksheet removal output plan should keep removed worksheet inbound id");
    check(output_plan.removed_package_entries.size()
            == editor.edit_plan().removed_package_entries().size(),
        "explicit worksheet removal output plan should mirror removed package-entry audits");
    const auto output_removed_worksheet_relationships =
        std::find_if(output_plan.removed_package_entries.begin(),
            output_plan.removed_package_entries.end(),
            [](const fastxlsx::detail::EditPlanRemovedPackageEntry& removed_entry) {
                return removed_entry.entry_name == "xl/worksheets/_rels/sheet1.xml.rels";
            });
    check(output_removed_worksheet_relationships
            != output_plan.removed_package_entries.end(),
        "explicit worksheet removal output plan should expose omitted worksheet relationships audit");
    check(output_removed_worksheet_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit worksheet removal output plan should keep omitted worksheet relationships role");
    check(output_removed_worksheet_relationships->owner_part == worksheet_part.value(),
        "explicit worksheet removal output plan should keep omitted worksheet relationships owner");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/worksheets/sheet1.xml") == entries.end(),
        "explicit worksheet removal output should omit worksheet part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") == entries.end(),
        "explicit worksheet removal output should omit worksheet owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(worksheet_part) == nullptr,
        "explicit worksheet removal output should remove worksheet content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/worksheets/sheet1.xml",
        "explicit worksheet removal content types XML should omit worksheet override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit worksheet removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit worksheet removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit worksheet removal should preserve workbook relationships bytes");
    check(output_reader.relationships_for(worksheet_part) == nullptr,
        "explicit worksheet removal should not keep owner relationships for absent worksheet");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "explicit worksheet removal should keep workbook relationships readable");
    const auto* worksheet_link = workbook_relationships->find_by_id("rId1");
    check(worksheet_link != nullptr,
        "explicit worksheet removal should keep inbound worksheet relationship id");
    check(worksheet_link->target == "worksheets/sheet1.xml",
        "explicit worksheet removal should not rewrite inbound worksheet target");
    check(worksheet_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit worksheet removal should keep inbound worksheet target mode");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit worksheet removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit worksheet removal should preserve drawing relationships bytes");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit worksheet removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit worksheet removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit worksheet removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit worksheet removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit worksheet removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit worksheet removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit worksheet removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit worksheet removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit worksheet removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit worksheet removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(drawing_part) != nullptr,
        "explicit worksheet removal should keep drawing content type override");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit worksheet removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit worksheet removal should keep table content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit worksheet removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit worksheet removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit worksheet removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit worksheet removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit worksheet removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit worksheet removal should not promote PNG media default to override");
}

void test_package_editor_removes_drawing_and_omits_owner_relationships()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-drawing-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    editor.remove_part(drawing_part, "explicit drawing part removal");

    check(editor.edit_plan().find_part(drawing_part) == nullptr,
        "explicit drawing removal should clear the active edit-plan part");
    const auto* removed_drawing = editor.edit_plan().find_removed_part(drawing_part);
    check(removed_drawing != nullptr,
        "explicit drawing removal should record removed-part audit");
    check(removed_drawing->reason.find("drawing part") != std::string::npos,
        "explicit drawing removal should retain the removal reason");
    check(removed_drawing->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit drawing removal should audit preserved inbound relationships");
    check(removed_drawing->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit drawing removal inbound audit should include owner part");
    check(removed_drawing->reason.find("rId1") != std::string::npos,
        "explicit drawing removal inbound audit should include direct relationship id");
    check(removed_drawing->reason.find("rId5") != std::string::npos,
        "explicit drawing removal inbound audit should include URI-qualified relationship id");
    check(removed_drawing->reason.find("../drawings/drawing1.xml")
            != std::string::npos,
        "explicit drawing removal inbound audit should include direct target");
    check(removed_drawing->reason.find("../drawings/drawing1.xml#shape1")
            != std::string::npos,
        "explicit drawing removal inbound audit should include URI-qualified target");
    check(removed_drawing->inbound_relationships.size() == 2,
        "explicit drawing removal should keep structured inbound audits");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* direct_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* fragment_inbound = nullptr;
    for (const auto& audit : removed_drawing->inbound_relationships) {
        if (audit.relationship_id == "rId1") {
            direct_inbound = &audit;
        } else if (audit.relationship_id == "rId5") {
            fragment_inbound = &audit;
        }
    }
    check(direct_inbound != nullptr,
        "explicit drawing removal should keep direct drawing inbound audit");
    check(direct_inbound->owner_part == worksheet_part.value(),
        "explicit drawing removal should keep direct inbound owner part");
    check(direct_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit drawing removal should keep direct inbound owner relationship entry");
    check(direct_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit drawing removal should keep direct inbound relationship type");
    check(direct_inbound->relationship_target == "../drawings/drawing1.xml",
        "explicit drawing removal should keep direct inbound raw target");
    check(direct_inbound->target_part == drawing_part,
        "explicit drawing removal should keep direct normalized target part");
    check(fragment_inbound != nullptr,
        "explicit drawing removal should keep URI-qualified drawing inbound audit");
    check(fragment_inbound->owner_part == worksheet_part.value(),
        "explicit drawing removal should keep URI-qualified inbound owner part");
    check(fragment_inbound->owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit drawing removal should keep URI-qualified inbound owner relationship entry");
    check(fragment_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
        "explicit drawing removal should keep URI-qualified inbound relationship type");
    check(fragment_inbound->relationship_target == "../drawings/drawing1.xml#shape1",
        "explicit drawing removal should keep URI-qualified raw target");
    check(fragment_inbound->target_part == drawing_part,
        "explicit drawing removal should keep URI-qualified normalized target part");
    check(editor.manifest().find_part(drawing_part) == nullptr,
        "explicit drawing removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(drawing_part) == nullptr,
        "explicit drawing removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit drawing removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit drawing removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit drawing removal content types audit should keep structured role");
    const auto* removed_drawing_relationships =
        editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels");
    check(removed_drawing_relationships != nullptr,
        "explicit drawing removal should omit source-owned drawing relationships");
    check(removed_drawing_relationships->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::SourceRelationships,
        "explicit drawing owner relationships omission should keep source relationship role");
    check(removed_drawing_relationships->owner_part == drawing_part.value(),
        "explicit drawing owner relationships omission should keep owner part");
    check(editor.edit_plan().find_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "explicit drawing removal should not keep active owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(chart_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep chart copy-original");
    check(editor.edit_plan().find_part(image_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep media copy-original");
    check(editor.edit_plan().find_part(table_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep table copy-original");
    check(editor.edit_plan().find_part(vml_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep VML drawing copy-original");
    check(editor.edit_plan().find_part(percent_encoded_drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep percent-decoded drawing copy-original");
    check(editor.edit_plan().find_part(shared_strings_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep sharedStrings copy-original");
    check(editor.edit_plan().find_part(styles_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep styles copy-original");
    check(editor.edit_plan().find_part(vba_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep VBA copy-original");
    check(editor.edit_plan().find_part(calc_chain_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep calcChain copy-original");
    check(editor.edit_plan().find_part(opaque_extension_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit drawing removal should keep unknown extension copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/drawings/drawing1.xml") == entries.end(),
        "explicit drawing removal output should omit drawing part");
    check(entries.find("xl/drawings/_rels/drawing1.xml.rels") == entries.end(),
        "explicit drawing removal output should omit drawing owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(drawing_part) == nullptr,
        "explicit drawing removal output should remove drawing content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/drawings/drawing1.xml",
        "explicit drawing removal content types XML should omit drawing override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit drawing removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit drawing removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit drawing removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit drawing removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit drawing removal should not prune inbound worksheet relationships");
    check(output_reader.relationships_for(drawing_part) == nullptr,
        "explicit drawing removal should not keep owner relationships for absent drawing");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit drawing removal should keep worksheet relationships readable");
    const auto* drawing_link = worksheet_relationships->find_by_id("rId1");
    check(drawing_link != nullptr,
        "explicit drawing removal should keep inbound drawing relationship id");
    check(drawing_link->target == "../drawings/drawing1.xml",
        "explicit drawing removal should not rewrite inbound drawing target");
    check(drawing_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit drawing removal should keep inbound drawing target mode");
    const auto* fragment_link = worksheet_relationships->find_by_id("rId5");
    check(fragment_link != nullptr,
        "explicit drawing removal should keep URI-qualified inbound drawing relationship id");
    check(fragment_link->target == "../drawings/drawing1.xml#shape1",
        "explicit drawing removal should not rewrite URI-qualified inbound drawing target");
    check(fragment_link->target_mode == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit drawing removal should keep URI-qualified inbound drawing target mode");
    check(worksheet_relationships->find_by_id("rId3") != nullptr,
        "explicit drawing removal should keep worksheet table relationship");
    check(worksheet_relationships->find_by_id("rId7") != nullptr,
        "explicit drawing removal should keep worksheet VML relationship");
    check(worksheet_relationships->find_by_id("rId8") != nullptr,
        "explicit drawing removal should keep percent-encoded drawing relationship");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit drawing removal should preserve chart bytes");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit drawing removal should preserve media bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit drawing removal should preserve table bytes");
    check(output_reader.read_entry("xl/drawings/vmlDrawing1.vml") == source.vml_drawing,
        "explicit drawing removal should preserve VML bytes");
    check(output_reader.read_entry("xl/drawings/drawing space.xml")
            == source.percent_encoded_drawing,
        "explicit drawing removal should preserve percent-decoded drawing bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit drawing removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit drawing removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit drawing removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit drawing removal should preserve VBA bytes");
    check(output_reader.read_entry("xl/calcChain.xml") == source.calc_chain,
        "explicit drawing removal should preserve calcChain bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit drawing removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit drawing removal should preserve unknown extension relationships bytes");
    check(output_reader.content_types().override_for(chart_part) != nullptr,
        "explicit drawing removal should keep chart content type override");
    check(output_reader.content_types().override_for(table_part) != nullptr,
        "explicit drawing removal should keep table content type override");
    check(output_reader.content_types().override_for(vml_drawing_part) != nullptr,
        "explicit drawing removal should keep VML content type override");
    check(output_reader.content_types().override_for(percent_encoded_drawing_part) != nullptr,
        "explicit drawing removal should keep percent-decoded drawing content type override");
    check(output_reader.content_types().override_for(shared_strings_part) != nullptr,
        "explicit drawing removal should keep sharedStrings content type override");
    check(output_reader.content_types().override_for(styles_part) != nullptr,
        "explicit drawing removal should keep styles content type override");
    check(output_reader.content_types().override_for(vba_part) != nullptr,
        "explicit drawing removal should keep VBA content type override");
    check(output_reader.content_types().override_for(calc_chain_part) != nullptr,
        "explicit drawing removal should keep calcChain content type override");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit drawing removal should keep PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit drawing removal should not promote PNG media to an override");
}

void test_package_editor_removes_chart_and_rewrites_content_types_without_pruning_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-chart-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-chart-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName chart_part("/xl/charts/chart1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    editor.remove_part(chart_part, "explicit chart part removal");

    check(editor.edit_plan().find_part(chart_part) == nullptr,
        "explicit chart removal should clear the active edit-plan part");
    const auto* removed_chart = editor.edit_plan().find_removed_part(chart_part);
    check(removed_chart != nullptr,
        "explicit chart removal should record removed-part audit");
    check(removed_chart->reason.find("chart part") != std::string::npos,
        "explicit chart removal should retain the removal reason");
    check(removed_chart->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit chart removal should audit preserved inbound relationships");
    check(removed_chart->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "explicit chart removal inbound audit should include owner part");
    check(removed_chart->reason.find("rId2") != std::string::npos,
        "explicit chart removal inbound audit should include relationship id");
    check(removed_chart->reason.find("../charts/chart1.xml") != std::string::npos,
        "explicit chart removal inbound audit should include original target");
    check(removed_chart->inbound_relationships.size() == 2,
        "explicit chart removal should keep structured inbound relationship audit");
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_inbound = nullptr;
    const fastxlsx::detail::RemovedPartInboundRelationshipAudit* chart_fragment_inbound = nullptr;
    for (const auto& audit : removed_chart->inbound_relationships) {
        if (audit.relationship_id == "rId2") {
            chart_inbound = &audit;
        } else if (audit.relationship_id == "rId4") {
            chart_fragment_inbound = &audit;
        }
    }
    check(chart_inbound != nullptr,
        "explicit chart removal should keep direct chart inbound audit");
    check(chart_inbound->owner_part == drawing_part.value(),
        "explicit chart removal should keep inbound owner part");
    check(chart_inbound->owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "explicit chart removal should keep inbound owner relationship entry");
    check(chart_inbound->relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "explicit chart removal should keep inbound relationship type");
    check(chart_inbound->relationship_target == "../charts/chart1.xml",
        "explicit chart removal should keep inbound raw target");
    check(chart_inbound->target_part == chart_part,
        "explicit chart removal should keep normalized target part");
    check(chart_fragment_inbound != nullptr,
        "explicit chart removal should keep URI-qualified chart inbound audit");
    check(chart_fragment_inbound->relationship_target == "../charts/chart1.xml#plotArea",
        "explicit chart removal should keep URI-qualified raw target");
    check(chart_fragment_inbound->target_part == chart_part,
        "explicit chart removal should keep URI-qualified normalized target part");
    const std::vector<fastxlsx::detail::PackageEditorOutputEntryPlan> output_plan =
        editor.planned_output_entries();
    check_output_entry_plan(output_plan, "xl/charts/chart1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "explicit chart removal output plan should omit chart source entry");
    check_output_entry_part_context(output_plan, "xl/charts/chart1.xml", true,
        chart_part.value(), "explicit chart removal output plan should keep part context");
    const auto* chart_output_plan =
        find_output_entry_plan(output_plan, "xl/charts/chart1.xml");
    check(chart_output_plan != nullptr
            && chart_output_plan->inbound_relationships.size() == 2,
        "explicit chart removal output plan should expose inbound relationship audit");
    check_output_entry_has_inbound_relationship(output_plan, "xl/charts/chart1.xml",
        drawing_part.value(), "xl/drawings/_rels/drawing1.xml.rels", "rId2",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml", chart_part,
        "explicit chart removal output plan should keep direct inbound relationship");
    check_output_entry_has_inbound_relationship(output_plan, "xl/charts/chart1.xml",
        drawing_part.value(), "xl/drawings/_rels/drawing1.xml.rels", "rId4",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
        "../charts/chart1.xml#plotArea", chart_part,
        "explicit chart removal output plan should keep URI-qualified inbound relationship");
    check(editor.manifest().find_part(chart_part) == nullptr,
        "explicit chart removal should remove the part from the manifest");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit chart removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit chart removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit chart removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/charts/_rels/chart1.xml.rels")
            == nullptr,
        "explicit chart removal should not invent missing chart owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/charts/chart1.xml") == entries.end(),
        "explicit chart removal output should omit chart part");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(chart_part) == nullptr,
        "explicit chart removal output should remove chart content type override");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit chart removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit chart removal should not prune inbound drawing relationships");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "explicit chart removal should keep drawing relationships readable");
    const auto* chart_link = drawing_relationships->find_by_id("rId2");
    check(chart_link != nullptr,
        "explicit chart removal should keep inbound chart relationship id");
    check(chart_link->target == "../charts/chart1.xml",
        "explicit chart removal should not rewrite inbound chart relationship target");
    check(output_reader.read_entry("xl/media/image1.png") == source.media,
        "explicit chart removal should preserve media bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit chart removal should preserve unknown extension bytes");
}

void test_package_editor_removes_media_and_preserves_drawing_links()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-remove-media-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-media-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName image_part("/xl/media/image1.png");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    editor.remove_part(image_part, "explicit media part removal");

    check(editor.edit_plan().find_part(image_part) == nullptr,
        "explicit media removal should clear the active edit-plan part");
    const auto* removed_image = editor.edit_plan().find_removed_part(image_part);
    check(removed_image != nullptr,
        "explicit media removal should record removed-part audit");
    check(removed_image->reason.find("media part") != std::string::npos,
        "explicit media removal should retain the removal reason");
    check(removed_image->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit media removal should audit preserved inbound relationships");
    check(removed_image->reason.find("/xl/drawings/drawing1.xml")
            != std::string::npos,
        "explicit media removal inbound audit should include owner part");
    check(removed_image->reason.find("rId1") != std::string::npos,
        "explicit media removal inbound audit should include relationship id");
    check(removed_image->reason.find("../media/image1.png") != std::string::npos,
        "explicit media removal inbound audit should include original target");
    check(removed_image->inbound_relationships.size() == 1,
        "explicit media removal should keep structured inbound audit");
    const auto& image_inbound = removed_image->inbound_relationships.front();
    check(image_inbound.owner_part == drawing_part.value(),
        "explicit media removal should keep inbound owner part");
    check(image_inbound.owner_entry == "xl/drawings/_rels/drawing1.xml.rels",
        "explicit media removal should keep inbound owner relationship entry");
    check(image_inbound.relationship_id == "rId1",
        "explicit media removal should keep inbound relationship id");
    check(image_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
        "explicit media removal should keep inbound relationship type");
    check(image_inbound.relationship_target == "../media/image1.png",
        "explicit media removal should keep inbound raw target");
    check(image_inbound.target_part == image_part,
        "explicit media removal should keep normalized target part");
    check(editor.manifest().find_part(image_part) == nullptr,
        "explicit media removal should remove the part from the manifest");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "explicit media removal should not rewrite content types when only default applies");
    check(editor.edit_plan().find_removed_package_entry("xl/media/_rels/image1.png.rels")
            == nullptr,
        "explicit media removal should not invent missing media owner relationships omission");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/media/image1.png") == entries.end(),
        "explicit media removal output should omit media part");
    check(entries.find("xl/media/_rels/image1.png.rels") == entries.end(),
        "explicit media removal output should not invent media owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "explicit media removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit media removal should preserve package relationships bytes");
    check(output_reader.content_types().default_for("png") != nullptr,
        "explicit media removal should preserve PNG default content type");
    check(output_reader.content_types().override_for(image_part) == nullptr,
        "explicit media removal should not promote PNG media to an override");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit media removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit media removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit media removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "explicit media removal should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "explicit media removal should not prune inbound drawing relationships");
    check(output_reader.relationships_for(image_part) == nullptr,
        "explicit media removal should not keep owner relationships for absent media");
    const auto* drawing_relationships = output_reader.relationships_for(drawing_part);
    check(drawing_relationships != nullptr,
        "explicit media removal should keep drawing relationships readable");
    const auto* image_link = drawing_relationships->find_by_id("rId1");
    check(image_link != nullptr,
        "explicit media removal should keep inbound image relationship id");
    check(image_link->target == "../media/image1.png",
        "explicit media removal should not rewrite inbound image relationship target");
    check(output_reader.read_entry("xl/charts/chart1.xml") == source.chart,
        "explicit media removal should preserve chart bytes");
    check(output_reader.read_entry("xl/tables/table1.xml") == source.table,
        "explicit media removal should preserve table bytes");
    check(output_reader.read_entry("xl/sharedStrings.xml") == source.shared_strings,
        "explicit media removal should preserve sharedStrings bytes");
    check(output_reader.read_entry("xl/_rels/sharedStrings.xml.rels")
            == source.shared_strings_relationships,
        "explicit media removal should preserve sharedStrings relationships bytes");
    check(output_reader.read_entry("xl/styles.xml") == source.styles,
        "explicit media removal should preserve styles bytes");
    check(output_reader.read_entry("xl/vbaProject.bin") == source.vba_project,
        "explicit media removal should preserve VBA bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "explicit media removal should preserve unknown extension bytes");
    check(output_reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == source.opaque_extension_relationships,
        "explicit media removal should preserve unknown extension relationships bytes");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation removal shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-removal")) {
            test_package_editor_part_removal_staging_failure_preserves_prior_plan_and_retries();
            test_package_editor_removes_unknown_extension_and_omits_owner_relationships();
            test_package_editor_removes_workbook_and_preserves_package_links();
            test_package_editor_removes_worksheet_and_preserves_workbook_links();
            test_package_editor_removes_drawing_and_omits_owner_relationships();
            test_package_editor_removes_chart_and_rewrites_content_types_without_pruning_links();
            test_package_editor_removes_media_and_preserves_drawing_links();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
