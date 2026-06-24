#include "test_package_editor_preservation_removal_common.hpp"

void test_package_editor_removes_part_with_malformed_unrelated_relationship_target()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-remove-malformed-rel-source.xlsx");
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rIdMalformedPercent" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="drawings/bad%ZZ.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-malformed-rel-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");
    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    editor.remove_part(
        unknown_part, "explicit unknown removal with malformed relationship target", fail_policy);

    const auto* removed_unknown = editor.edit_plan().find_removed_part(unknown_part);
    check(removed_unknown != nullptr,
        "malformed unrelated targets should not block explicit part removal");
    check(removed_unknown->inbound_relationships.empty(),
        "malformed unrelated targets should not be recorded as inbound removal links");
    check(has_note_containing(editor.edit_plan().notes(),
              {"invalid relationship target skipped during removed-part inbound audit",
                  "/xl/workbook.xml", "rIdMalformedPercent", "drawings/bad%ZZ.xml",
                  "percent escape is invalid"}),
        "PackageEditor should surface malformed relationship target audit notes");
    check(editor.edit_plan().relationship_target_audits().empty(),
        "malformed unrelated targets should not create structured target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "malformed unrelated targets should not create worksheet reference audits");
    check(editor.edit_plan().package_entries().empty(),
        "malformed unrelated targets should not rewrite metadata package entries");
    check(editor.edit_plan().removed_package_entries().empty(),
        "malformed unrelated targets should not omit metadata package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "malformed unrelated targets should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed unrelated targets should preserve calcChain policy");
    check(editor.manifest().find_part(unknown_part) == nullptr,
        "malformed unrelated targets should still allow removing the target from manifest");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "malformed target removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "malformed target removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "malformed target removal output plan should not add structured target audits");
    check(output_plan.worksheet_relationship_reference_audits.empty(),
        "malformed target removal output plan should not add worksheet reference audits");
    check(output_plan.removed_parts.size() == 1,
        "malformed target removal output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == unknown_part,
        "malformed target removal output plan should expose the removed unknown part");
    check(output_plan.removed_parts.front().inbound_relationships.empty(),
        "malformed target removal output plan should not invent inbound links");
    check(has_note_containing(output_plan.notes,
              {"invalid relationship target skipped during removed-part inbound audit",
                  "/xl/workbook.xml", "rIdMalformedPercent", "drawings/bad%ZZ.xml",
                  "percent escape is invalid"}),
        "malformed target removal output plan should snapshot audit notes");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "malformed target removal output plan should omit removed unknown part");
    check_output_entry_part_context(output_plan.entries, "custom/opaque.bin",
        true, unknown_part.value(),
        "malformed target removal output plan should classify removed unknown part");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "malformed target removal output plan should preserve package relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("custom/opaque.bin") == entries.end(),
        "malformed unrelated target removal output should omit removed unknown part");
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.find_entry("custom/opaque.bin") == nullptr,
        "malformed unrelated target removal reader should omit removed unknown part");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "malformed relationship target removal should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "malformed relationship target removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "malformed relationship target removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "malformed relationship target should be byte-preserved in owner relationships");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "malformed relationship target removal should preserve core properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "malformed relationship target removal should preserve worksheet bytes");
}

void test_package_editor_reference_policy_fail_blocks_inbound_part_removal_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-policy-fail-remove-drawing-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-remove-drawing-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");
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

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        editor.remove_part(
            drawing_part, "strict drawing removal should fail", fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "inbound relationships blocked by reference policy",
            "linked removal policy failure should report inbound relationship policy");
    }
    check(failed,
        "ReferencePolicyAction::Fail should reject removal of an inbound-linked drawing part");

    check(editor.edit_plan().size() == initial_plan_size,
        "linked removal policy failure should not change edit plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "linked removal policy failure should not add edit plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "linked removal policy failure should not add relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "linked removal policy failure should not add worksheet relationship reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "linked removal policy failure should not add worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "linked removal policy failure should not add workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "linked removal policy failure should not add package-entry audit");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "linked removal policy failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "linked removal policy failure should not record removed package entries");
    check(editor.edit_plan().full_calculation_on_load()
            == initial_full_calculation_on_load,
        "linked removal policy failure should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == initial_calc_chain_action,
        "linked removal policy failure should not change calcChain action");
    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "linked removal policy failure should not leave stale removed-part audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "linked removal policy failure should not audit content types rewrite");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "linked removal policy failure should not omit drawing owner relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave drawing copy-original");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked removal policy failure should leave drawing manifest copy-original");
    check(editor.manifest().content_types().override_for(drawing_part) != nullptr,
        "linked removal policy failure should preserve drawing content type override");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(output_plan.full_calculation_on_load == initial_full_calculation_on_load,
        "linked removal policy failure output plan should not request full calculation");
    check(output_plan.calc_chain_action == initial_calc_chain_action,
        "linked removal policy failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == initial_note_count,
        "linked removal policy failure output plan should not add audit notes");
    check(output_plan.relationship_target_audits.size()
            == initial_relationship_target_audit_count,
        "linked removal policy failure output plan should not add relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == initial_worksheet_relationship_reference_audit_count,
        "linked removal policy failure output plan should not add worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == initial_worksheet_payload_dependency_audit_count,
        "linked removal policy failure output plan should not add worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == initial_workbook_payload_dependency_audit_count,
        "linked removal policy failure output plan should not add workbook payload audits");
    check(output_plan.removed_parts.size() == initial_removed_part_count,
        "linked removal policy failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == initial_removed_package_entry_count,
        "linked removal policy failure output plan should not record omitted metadata entries");
    check_output_plan_preserves_source_copy_original(editor, output_plan,
        "linked removal policy failure output plan should keep every source entry copy-original");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "linked removal policy failure output plan should classify content types as metadata entry");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "linked removal policy failure output plan should classify drawing as a package part");
    check_output_entry_part_context(output_plan.entries,
        "xl/drawings/_rels/drawing1.xml.rels", false, "",
        "linked removal policy failure output plan should classify drawing relationships as metadata entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader);
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "linked removal policy failure output should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "linked removal policy failure output should preserve drawing relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "linked removal policy failure output should preserve unknown extension bytes");
}

void test_package_editor_reference_policy_fail_preserves_prior_replacement_for_inbound_removal()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-policy-fail-remove-drawing-prior-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-policy-fail-remove-drawing-prior-output.xlsx");

    const fastxlsx::detail::PackageReader source_reader =
        fastxlsx::detail::PackageReader::open(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName drawing_part("/xl/drawings/drawing1.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Queued" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued workbook replacement before strict drawing removal");

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
    const std::size_t queued_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t queued_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const bool queued_full_calculation_on_load =
        editor.edit_plan().full_calculation_on_load();
    const fastxlsx::detail::CalcChainAction queued_calc_chain_action =
        editor.edit_plan().calc_chain_action();

    const auto* queued_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(queued_workbook_plan != nullptr
            && queued_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound-removal fixture should record prior workbook replacement");
    check(queued_workbook_plan->reason.find("queued workbook replacement")
            != std::string::npos,
        "queued inbound-removal fixture should keep prior workbook replacement reason");
    const auto* queued_workbook_relationships_plan =
        editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels");
    check(queued_workbook_relationships_plan != nullptr
            && queued_workbook_relationships_plan->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound-removal fixture should audit preserved workbook relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound-removal fixture should leave drawing copy-original before failure");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound-removal fixture should update workbook manifest write mode");

    fastxlsx::detail::ReferencePolicy fail_policy;
    fail_policy.unsupported_linked_part_action =
        fastxlsx::detail::ReferencePolicyAction::Fail;

    bool failed = false;
    try {
        editor.remove_part(
            drawing_part, "strict drawing removal after queued workbook replacement", fail_policy);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "inbound relationships blocked by reference policy",
            "queued inbound removal policy failure should report inbound relationship policy");
    }
    check(failed,
        "PackageEditor should fail inbound drawing removal after a queued workbook replacement");

    check(editor.edit_plan().size() == queued_plan_size,
        "queued inbound removal failure should preserve prior edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "queued inbound removal failure should not append edit-plan notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == queued_relationship_target_audit_count,
        "queued inbound removal failure should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued inbound removal failure should not append worksheet reference audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued inbound removal failure should not append worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == queued_workbook_payload_dependency_audit_count,
        "queued inbound removal failure should not append workbook payload audits");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "queued inbound removal failure should preserve package-entry audits");
    check(editor.edit_plan().removed_parts().size() == queued_removed_part_count,
        "queued inbound removal failure should not record removed parts");
    check(editor.edit_plan().removed_package_entries().size()
            == queued_removed_package_entry_count,
        "queued inbound removal failure should not record removed package entries");
    check(editor.edit_plan().full_calculation_on_load()
            == queued_full_calculation_on_load,
        "queued inbound removal failure should not change fullCalcOnLoad intent");
    check(editor.edit_plan().calc_chain_action() == queued_calc_chain_action,
        "queued inbound removal failure should preserve calcChain policy");
    const auto* final_workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(final_workbook_plan != nullptr
            && final_workbook_plan->write_mode
                == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound removal failure should keep prior workbook replacement active");
    check(final_workbook_plan->reason.find("queued workbook replacement")
            != std::string::npos,
        "queued inbound removal failure should keep prior workbook replacement reason");
    check(editor.edit_plan().find_removed_part(drawing_part) == nullptr,
        "queued inbound removal failure should not leave stale removed drawing audit");
    check(editor.edit_plan().find_removed_package_entry("xl/drawings/_rels/drawing1.xml.rels")
            == nullptr,
        "queued inbound removal failure should not omit drawing owner relationships");
    check(editor.edit_plan().find_part(drawing_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave drawing copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "queued inbound removal failure should keep prior workbook manifest write mode");
    check_manifest_write_mode(editor, drawing_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "queued inbound removal failure should leave drawing manifest copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "queued inbound removal failure should keep planned output snapshot consistent");
    check(output_plan.full_calculation_on_load == queued_full_calculation_on_load,
        "queued inbound removal failure output plan should preserve fullCalcOnLoad intent");
    check(output_plan.calc_chain_action == queued_calc_chain_action,
        "queued inbound removal failure output plan should preserve calcChain policy");
    check(output_plan.notes.size() == queued_note_count,
        "queued inbound removal failure output plan should not append notes");
    check(output_plan.relationship_target_audits.size()
            == queued_relationship_target_audit_count,
        "queued inbound removal failure output plan should not append relationship target audits");
    check(output_plan.worksheet_relationship_reference_audits.size()
            == queued_worksheet_relationship_reference_audit_count,
        "queued inbound removal failure output plan should not append worksheet reference audits");
    check(output_plan.worksheet_payload_dependency_audits.size()
            == queued_worksheet_payload_dependency_audit_count,
        "queued inbound removal failure output plan should not append worksheet payload audits");
    check(output_plan.workbook_payload_dependency_audits.size()
            == queued_workbook_payload_dependency_audit_count,
        "queued inbound removal failure output plan should not append workbook payload audits");
    check(output_plan.removed_parts.size() == queued_removed_part_count,
        "queued inbound removal failure output plan should not record removed parts");
    check(output_plan.removed_package_entries.size()
            == queued_removed_package_entry_count,
        "queued inbound removal failure output plan should not record omitted metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "queued inbound removal failure output plan should keep workbook rewrite");
    check_output_entry_plan(output_plan.entries, "xl/drawings/drawing1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued inbound removal failure output plan should preserve drawing copy-original");
    check_output_entry_part_context(output_plan.entries, "xl/drawings/drawing1.xml",
        true, drawing_part.value(),
        "queued inbound removal failure output plan should classify drawing as a package part");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "queued inbound removal failure output plan should preserve workbook relationships");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(source_reader, output_reader, workbook_part.zip_path());
    check(output_reader.read_entry("xl/workbook.xml") == replacement_workbook,
        "queued inbound removal failure output should keep prior workbook replacement bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "queued inbound removal failure output should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "queued inbound removal failure output should preserve worksheet bytes");
    check(output_reader.read_entry("xl/drawings/drawing1.xml") == source.drawing,
        "queued inbound removal failure output should preserve drawing bytes");
    check(output_reader.read_entry("xl/drawings/_rels/drawing1.xml.rels")
            == source.drawing_relationships,
        "queued inbound removal failure output should preserve drawing relationships bytes");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "queued inbound removal failure output should preserve unknown extension bytes");
}


} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation removal shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-removal-policy")) {
            test_package_editor_removes_part_with_malformed_unrelated_relationship_target();
            test_package_editor_reference_policy_fail_blocks_inbound_part_removal_without_state_changes();
            test_package_editor_reference_policy_fail_preserves_prior_replacement_for_inbound_removal();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
