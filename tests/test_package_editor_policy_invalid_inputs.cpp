#include "test_package_editor_policy_common.hpp"

void test_package_editor_rejects_invalid_replacements()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    expect_replace_failure(editor,
        fastxlsx::detail::PartName("/xl/missing.xml"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor should reject replacement for missing parts");
    expect_replace_failure(editor,
        core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor should reject copy-original replacement mode");
    bool worksheet_failed = false;
    try {
        replace_worksheet_part_from_single_chunk_source(editor, core_part, "<worksheet/>");
    } catch (const std::exception&) {
        worksheet_failed = true;
    }
    check(worksheet_failed,
        "PackageEditor should reject worksheet replacement for non-worksheet parts");
    check(editor.edit_plan().size() == initial_plan_size,
        "failed replacements should not alter the edit plan");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "failed replacements should not alter edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "failed replacements should not alter relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "failed replacements should not alter worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "failed replacements should not alter worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "failed replacements should not alter workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "failed replacements should not alter package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "failed replacements should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "failed replacements should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "failed replacements should not alter full calculation policy");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "failed replacements should not alter calcChain action");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave core properties copy-original in the edit plan");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave worksheet copy-original in the edit plan");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave core properties copy-original in the manifest");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "failed replacements should leave worksheet copy-original in the manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "failed replacements output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "failed replacements output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "failed replacements output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "failed replacements output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "failed replacements output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "failed replacements output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "failed replacements output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "failed replacements output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "failed replacements output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "failed replacements output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "failed replacements output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "failed replacements output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "failed replacements output plan should classify core properties as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "failed replacements output plan should classify worksheet as a package part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin", true,
        "/custom/opaque.bin",
        "failed replacements output plan should classify unknown bytes as a package part");

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-invalid-output.xlsx");
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "failed replacements should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "failed replacements should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "failed replacements should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "failed replacements should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "failed replacements should preserve worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "failed replacements should preserve unknown bytes");
}

void test_package_editor_rejects_metadata_entry_replacements_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-metadata-targets-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
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
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool initial_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction initial_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("[Content_Types].xml"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject content-types replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("_rels/.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject package relationships replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("xl/_rels/workbook.xml.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject source-owned relationships replacement as an ordinary part");
    expect_replace_failure_containing(editor,
        fastxlsx::detail::PartName("_rels/root.xml.rels"),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "metadata package entries",
        "PackageEditor should reject root source-owned relationships replacement as an ordinary part");
    check(editor.edit_plan().size() == initial_plan_size,
        "metadata replacement failures should not alter the edit plan");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "metadata replacement failures should not alter edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "metadata replacement failures should not alter relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "metadata replacement failures should not alter worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "metadata replacement failures should not alter worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "metadata replacement failures should not alter workbook payload audits");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "metadata replacement failures should not record removed parts");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "metadata replacement failures should not record package-entry audit");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "metadata replacement failures should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "metadata replacement failures should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "metadata replacement failures should not change calcChain action");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "metadata replacement failures should not audit content types rewrite");
    check(editor.edit_plan().find_package_entry("_rels/.rels") == nullptr,
        "metadata replacement failures should not audit package relationships rewrite");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "metadata replacement failures should not audit workbook relationships rewrite");
    check(editor.edit_plan().find_part(core_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave core properties copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave worksheet copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave core manifest copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave workbook manifest copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "metadata replacement failures should leave worksheet manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "metadata replacement failures output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "metadata replacement failures output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "metadata replacement failures output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "metadata replacement failures output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "metadata replacement failures output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "metadata replacement failures output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "metadata replacement failures output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "metadata replacement failures output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "metadata replacement failures output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "metadata replacement failures output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "metadata replacement failures output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "metadata replacement failures output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "metadata replacement failures output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "docProps/core.xml", true,
        core_part.value(),
        "metadata replacement failures output plan should classify core properties as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "metadata replacement failures output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        worksheet_part.value(),
        "metadata replacement failures output plan should classify worksheet as a package part");

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-metadata-targets-output.xlsx");
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "metadata replacement failure output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "metadata replacement failure output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "metadata replacement failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "metadata replacement failure output should preserve ordinary part bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "metadata replacement failure output should preserve unknown bytes");
}

void test_package_editor_rejects_invalid_removals_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-invalid-removal-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-invalid-removal-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
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

    bool missing_failed = false;
    try {
        editor.remove_part(
            fastxlsx::detail::PartName("/xl/missing.xml"), "missing part removal");
    } catch (const std::exception& error) {
        missing_failed = true;
        check_contains(error.what(), "not present",
            "missing removal failure should report missing source part");
    }
    check(missing_failed,
        "PackageEditor should reject removal for missing parts");

    bool content_types_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/[Content_Types].xml"),
            "content types removal");
    } catch (const std::exception& error) {
        content_types_failed = true;
        check_contains(error.what(), "metadata package entries",
            "content-types removal failure should report metadata entry restriction");
    }
    check(content_types_failed,
        "PackageEditor should reject content-types removal as an ordinary part");

    bool package_relationships_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/_rels/.rels"),
            "package relationships removal");
    } catch (const std::exception& error) {
        package_relationships_failed = true;
        check_contains(error.what(), "metadata package entries",
            "package relationships removal failure should report metadata entry restriction");
    }
    check(package_relationships_failed,
        "PackageEditor should reject package relationships removal as an ordinary part");

    bool source_relationships_failed = false;
    try {
        editor.remove_part(fastxlsx::detail::PartName("/xl/_rels/workbook.xml.rels"),
            "source relationships removal");
    } catch (const std::exception& error) {
        source_relationships_failed = true;
        check_contains(error.what(), "metadata package entries",
            "source relationships removal failure should report metadata entry restriction");
    }
    check(source_relationships_failed,
        "PackageEditor should reject source-owned relationships removal as an ordinary part");

    check(editor.edit_plan().size() == initial_plan_size,
        "invalid removal failures should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "invalid removal failures should not change edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "invalid removal failures should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "invalid removal failures should not change worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "invalid removal failures should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "invalid removal failures should not change workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "invalid removal failures should not change package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "invalid removal failures should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "invalid removal failures should not record removed package-entry audit");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "invalid removal failures should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "invalid removal failures should not change calcChain action");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid removal failures should leave workbook copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid removal failures should leave workbook manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "invalid removal failures output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "invalid removal failures output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "invalid removal failures output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "invalid removal failures output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "invalid removal failures output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "invalid removal failures output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "invalid removal failures output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "invalid removal failures output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "invalid removal failures output plan should not record omitted metadata entries");
    check(output_plan.entries.size() == editor.reader().entries().size(),
        "invalid removal failures output plan should keep one decision per source entry");
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "invalid removal failures legacy output-entry preview should match aggregate plan");
    for (const fastxlsx::detail::PackageReaderEntry& entry : editor.reader().entries()) {
        check_output_entry_plan(output_plan.entries, entry.name,
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "invalid removal failures output plan should keep every source entry copy-original");
    }
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "invalid removal failures output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "_rels/.rels", false, "",
        "invalid removal failures output plan should classify package relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        workbook_part.value(),
        "invalid removal failures output plan should classify workbook as a package part");
    check_output_entry_part_context(output_plan.entries, "xl/_rels/workbook.xml.rels",
        false, "",
        "invalid removal failures output plan should classify workbook relationships as metadata entry");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin",
        true, "/custom/opaque-extension.bin",
        "invalid removal failures output plan should classify unknown extension as a package part");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "invalid removal failures output should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "invalid removal failures output should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "invalid removal failures output should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "invalid removal failures output should preserve workbook relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "invalid removal failures output should preserve unknown extension bytes");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor policy shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "policy-invalid-inputs")) {
            test_package_editor_rejects_invalid_replacements();
            test_package_editor_rejects_metadata_entry_replacements_without_state_changes();
            test_package_editor_rejects_invalid_removals_without_state_changes();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
