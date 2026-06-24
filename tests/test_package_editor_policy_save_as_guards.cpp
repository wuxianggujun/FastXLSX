#include "test_package_editor_policy_common.hpp"

void test_package_editor_rejects_saving_over_source_package()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-overwrite-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-overwrite-safe-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Unsafe</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);
    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    bool failed = false;
    try {
        editor.save_as(source.path);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageEditor should reject saving over the source package");

    const std::filesystem::path equivalent_source_path =
        source.path.parent_path() / "." / source.path.filename();
    bool equivalent_failed = false;
    try {
        editor.save_as(equivalent_source_path);
    } catch (const std::exception&) {
        equivalent_failed = true;
    }
    check(equivalent_failed,
        "PackageEditor should reject saving over a path-equivalent source package");

    bool empty_path_failed = false;
    try {
        editor.save_as(std::filesystem::path());
    } catch (const std::exception&) {
        empty_path_failed = true;
    }
    check(empty_path_failed,
        "PackageEditor should reject saving to an empty output path");

    const std::filesystem::path missing_parent_output =
        output_path("fastxlsx-package-editor-missing-parent-output") / "out.xlsx";
    bool missing_parent_failed = false;
    try {
        editor.save_as(missing_parent_output);
    } catch (const std::exception&) {
        missing_parent_failed = true;
    }
    check(missing_parent_failed,
        "PackageEditor should reject saving under a missing output parent directory");

    const std::filesystem::path parent_file_output =
        output_path("fastxlsx-package-editor-parent-file-output");
    {
        std::ofstream parent_file(parent_file_output, std::ios::binary);
        parent_file << "not a directory";
        check(parent_file.good(),
            "test setup should create a non-directory output parent");
    }
    bool parent_file_failed = false;
    try {
        editor.save_as(parent_file_output / "out.xlsx");
    } catch (const std::exception&) {
        parent_file_failed = true;
    }
    check(parent_file_failed,
        "PackageEditor should reject saving under a non-directory output parent");

    const std::filesystem::path directory_output =
        output_path("fastxlsx-package-editor-directory-output");
    std::filesystem::create_directory(directory_output);
    bool directory_failed = false;
    try {
        editor.save_as(directory_output);
    } catch (const std::exception&) {
        directory_failed = true;
    }
    check(directory_failed,
        "PackageEditor should reject saving to an existing directory");

    check(editor.edit_plan().size() == queued_plan_size,
        "save_as guard rejection should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "save_as guard rejection should not add edit-plan notes");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "save_as guard rejection should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "save_as guard rejection should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "save_as guard rejection should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "save_as guard rejection should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "save_as guard rejection should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "save_as guard rejection should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "save_as guard rejection should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "save_as guard rejection should not change calcChain policy");
    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr
            && core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "save_as guard rejection should keep queued core replacement active");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "save_as guard rejection should keep manifest replacement active");
    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "save_as guard rejection should keep planned output snapshot consistent");
    check(output_plan.removed_parts.size() == queued_removed_part_count,
        "save_as guard rejection output plan should keep removed-part audits stable");
    check(output_plan.removed_package_entries.size()
            == queued_removed_package_entry_count,
        "save_as guard rejection output plan should keep package-entry audits stable");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "save_as guard rejection output plan should keep relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "save_as guard rejection output plan should keep worksheet reference audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "save_as guard rejection output plan should keep worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "save_as guard rejection output plan should keep workbook payload audits stable");
    check(output_plan.full_calculation_on_load == queued_full_calculation_on_load,
        "save_as guard rejection output plan should keep fullCalcOnLoad intent stable");
    check(output_plan.calc_chain_action == queued_calc_chain_action,
        "save_as guard rejection output plan should keep calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "save_as guard rejection should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "save_as guard rejection should keep planned unknown copy-original");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    check(source_reader.read_entry("docProps/core.xml") == source.core_properties,
        "save_as guard rejection should preserve source core properties");
    check(source_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "save_as guard rejection should preserve source worksheet bytes");
    check(source_reader.read_entry("custom/opaque.bin") == source.unknown,
        "save_as guard rejection should preserve source unknown bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "save_as guard rejection should allow later safe output of queued replacement");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "later safe output should preserve worksheet bytes after rejected save_as guard");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve unknown bytes after rejected save_as guard");
}

void test_package_editor_save_as_copy_original_read_failure_preserves_state_and_output()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-copy-failure-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-copy-failure-output.xlsx");
    const std::string output_sentinel = "do not overwrite this failed output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    const std::string original_source_bytes = fastxlsx::test::read_file(source.path);
    std::string corrupted_source_bytes = original_source_bytes;
    corrupt_first_occurrence(corrupted_source_bytes,
        std::string_view(source.unknown.data(), source.unknown.size()));
    write_binary_file(source.path, corrupted_source_bytes);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Queued</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    try {
        editor.save_as(output);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "failed to materialize planned output source-copy entry 'custom/opaque.bin'",
            "copy-original failure should include planned output entry materialization context");
        check_contains(error.what(), "failed to copy source package entry",
            "copy-original failure should include copy context");
        check_contains(error.what(), "custom/opaque.bin",
            "copy-original failure should include the source entry name");
        check_contains(error.what(), "CRC",
            "copy-original failure should preserve the reader failure reason");
    }
    check(failed,
        "PackageEditor should reject save_as when a copy-original source entry cannot be read");

    check(editor.edit_plan().size() == queued_plan_size,
        "copy-original read failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "copy-original read failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "copy-original read failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "copy-original read failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "copy-original read failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "copy-original read failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "copy-original read failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "copy-original read failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "copy-original read failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "copy-original read failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "copy-original read failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "copy-original read failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "copy-original read failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "copy-original read failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "copy-original read failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "copy-original read failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "copy-original read failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "copy-original read failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "copy-original read failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "copy-original read failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "copy-original read failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "copy-original read failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "copy-original read failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "copy-original read failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "copy-original read failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "copy-original read failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "copy-original read failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "copy-original read failure should clean committed-output sibling temp files");

    write_binary_file(source.path, original_source_bytes);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after copy-original failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after copy-original failure");
}

void test_package_editor_save_as_rejects_mutated_source_copy_temp_size_without_state_changes()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-size-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-size-output.xlsx");
    const std::string output_sentinel = "do not overwrite this source-copy temp failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>TempContract</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            truncate_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-copy temp size failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "source-copy temp size failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "source-copy temp size failure should include chunk index context");
            check_contains(error.what(), "ZIP entry chunk size changed after staging",
                "source-copy temp size failure should enforce expected-size metadata");
            check_contains(error.what(), "actual 0 bytes",
                "source-copy temp size failure should report the truncated temp size");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "source-copy temp size failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject source-copy temp files whose size changes before write");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-copy temp size failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-copy temp size failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-copy temp size failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-copy temp size failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-copy temp size failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "source-copy temp size failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "source-copy temp size failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "source-copy temp size failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "source-copy temp size failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "source-copy temp size failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-copy temp size failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-copy temp size failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-copy temp size failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "source-copy temp size failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "source-copy temp size failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "source-copy temp size failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "source-copy temp size failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "source-copy temp size failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "source-copy temp size failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "source-copy temp size failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "source-copy temp size failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "source-copy temp size failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "source-copy temp size failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "source-copy temp size failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-copy temp size failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "source-copy temp size failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "source-copy temp size failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "source-copy temp size failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after temp size failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after temp size failure");
}

void test_package_editor_save_as_rejects_missing_source_copy_temp_with_expected_size()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-missing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-missing-output.xlsx");
    const std::string output_sentinel =
        "do not overwrite this missing source-copy temp failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>MissingTemp</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            delete_package_editor_source_copy_temp_files);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "missing source-copy temp failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "missing source-copy temp failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "missing source-copy temp failure should include chunk index context");
            check_contains(error.what(), "failed to stat file-backed ZIP entry chunk",
                "missing source-copy temp failure should report preflight stat failure");
            check_contains(error.what(), "expected ",
                "missing source-copy temp failure should report expected-size metadata");
            check_contains(error.what(), " bytes",
                "missing source-copy temp failure should keep expected byte units");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "missing source-copy temp failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject deleted source-copy temp files before output commit");

    check(editor.edit_plan().size() == queued_plan_size,
        "missing source-copy temp failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "missing source-copy temp failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "missing source-copy temp failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "missing source-copy temp failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "missing source-copy temp failure should not change removed package-entry audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "missing source-copy temp failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "missing source-copy temp failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "missing source-copy temp failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing source-copy temp failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "missing source-copy temp failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "missing source-copy temp failure should keep output-plan notes stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "missing source-copy temp failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "missing source-copy temp failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "missing source-copy temp failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "missing source-copy temp failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "missing source-copy temp failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing source-copy temp failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "missing source-copy temp failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after missing temp failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after missing temp failure");
}

void test_package_editor_save_as_rejects_mutated_source_copy_temp_crc_without_state_changes()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source-copy-temp-crc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-source-copy-temp-crc-output.xlsx");
    const std::string output_sentinel = "do not overwrite this source-copy crc failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>TempCrcContract</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    bool failed = false;
    {
        ScopedPackageEditorSourceCopyTempFilesHook hook(
            rewrite_package_editor_source_copy_temp_files_same_size);
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "failed to write PackageEditor output package",
                "source-copy temp CRC failure should include PackageEditor write context");
            check_contains(error.what(), "ZIP entry '",
                "source-copy temp CRC failure should include ZIP entry context");
            check_contains(error.what(), "chunk 0",
                "source-copy temp CRC failure should include chunk index context");
            check_contains(error.what(), "ZIP entry chunk CRC32 changed after staging",
                "source-copy temp CRC failure should enforce expected-CRC metadata");
            check_contains(error.what(), "actual ",
                "source-copy temp CRC failure should report actual CRC");
            check_contains(error.what(), "fastxlsx-package-editor-",
                "source-copy temp CRC failure should include the file-backed temp path");
        }
    }
    check(failed,
        "PackageEditor should reject source-copy temp files whose CRC changes before write");

    check(editor.edit_plan().size() == queued_plan_size,
        "source-copy temp CRC failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "source-copy temp CRC failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "source-copy temp CRC failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "source-copy temp CRC failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "source-copy temp CRC failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "source-copy temp CRC failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "source-copy temp CRC failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "source-copy temp CRC failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "source-copy temp CRC failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "source-copy temp CRC failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "source-copy temp CRC failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "source-copy temp CRC failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "source-copy temp CRC failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "source-copy temp CRC failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "source-copy temp CRC failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "source-copy temp CRC failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "source-copy temp CRC failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "source-copy temp CRC failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "source-copy temp CRC failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "source-copy temp CRC failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "source-copy temp CRC failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "source-copy temp CRC failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "source-copy temp CRC failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "source-copy temp CRC failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "source-copy temp CRC failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "source-copy temp CRC failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "source-copy temp CRC failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "source-copy temp CRC failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after temp CRC failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve copied unknown bytes after temp CRC failure");
}

void test_package_editor_save_as_writer_failure_preserves_state_and_output()
{
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-writer-failure-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-writer-failure-output.xlsx");
    const std::string output_sentinel = "do not overwrite this writer failure output";
    write_binary_file(output, output_sentinel);
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Writer</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite);

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t queued_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t queued_worksheet_relationship_reference_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t queued_worksheet_payload_dependency_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t queued_workbook_payload_dependency_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();
    const fastxlsx::detail::PackageEditorOutputPlan queued_output_plan =
        editor.planned_output();

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = static_cast<fastxlsx::detail::PackageWriterBackend>(999);

    bool failed = false;
    try {
        editor.save_as(output, options);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "failed to write PackageEditor output package",
            "writer failure should include PackageEditor write context");
        check_contains(error.what(), "fastxlsx-package-editor-writer-failure-output.xlsx",
            "writer failure should include the output package path");
        check_contains(error.what(), "unsupported package writer backend",
            "writer failure should preserve the backend failure reason");
    }
    check(failed,
        "PackageEditor should reject save_as when the selected writer backend fails");

    check(editor.edit_plan().size() == queued_plan_size,
        "writer failure should not change queued edit-plan entries");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "writer failure should not add edit-plan notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "writer failure should not change package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "writer failure should not change removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "writer failure should not change removed package-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "writer failure should not change relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "writer failure should not change worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "writer failure should not change worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "writer failure should not change workbook payload audits");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "writer failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "writer failure should not change calcChain policy");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "writer failure should keep queued core replacement active");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "writer failure should keep opaque part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.entries.size() == queued_output_plan.entries.size(),
        "writer failure should keep output-plan entry count stable");
    check(output_plan.notes.size() == queued_output_plan.notes.size(),
        "writer failure should keep output-plan notes stable");
    check(output_plan.removed_parts.size() == queued_output_plan.removed_parts.size(),
        "writer failure should keep output-plan removed parts stable");
    check(output_plan.removed_package_entries.size()
            == queued_output_plan.removed_package_entries.size(),
        "writer failure should keep output-plan package-entry omissions stable");
    check(output_plan.relationship_target_audits.size()
            == queued_output_plan.relationship_target_audits.size(),
        "writer failure should keep output-plan relationship audits stable");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_output_plan.worksheet_relationship_reference_audits.size(),
        "writer failure should keep output-plan worksheet audits stable");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_output_plan.worksheet_payload_dependency_audits.size(),
        "writer failure should keep output-plan worksheet payload audits stable");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_output_plan.workbook_payload_dependency_audits.size(),
        "writer failure should keep output-plan workbook payload audits stable");
    check(output_plan.full_calculation_on_load
            == queued_output_plan.full_calculation_on_load,
        "writer failure should keep output-plan fullCalcOnLoad stable");
    check(output_plan.calc_chain_action == queued_output_plan.calc_chain_action,
        "writer failure should keep output-plan calcChain policy stable");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "writer failure should keep planned core rewrite");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "writer failure should keep planned opaque copy-original");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "writer failure should not overwrite an existing output file");
    check_no_new_package_editor_temp_files(temp_files_before,
        "writer failure should clean staged source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "writer failure should clean committed-output sibling temp files");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "later safe output should write queued core replacement after writer failure");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "later safe output should preserve unknown bytes after writer failure");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor policy shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "policy-save-as-guards")) {
            test_package_editor_rejects_saving_over_source_package();
            test_package_editor_save_as_copy_original_read_failure_preserves_state_and_output();
            test_package_editor_save_as_rejects_mutated_source_copy_temp_size_without_state_changes();
            test_package_editor_save_as_rejects_missing_source_copy_temp_with_expected_size();
            test_package_editor_save_as_rejects_mutated_source_copy_temp_crc_without_state_changes();
            test_package_editor_save_as_writer_failure_preserves_state_and_output();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
