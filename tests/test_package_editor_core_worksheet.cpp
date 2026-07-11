#include "test_package_editor_core_common.hpp"

class ScopedPackageEditorWorksheetPartReplacementStagedHook {
public:
    explicit ScopedPackageEditorWorksheetPartReplacementStagedHook(
        fastxlsx::detail::PackageEditorWorksheetPartReplacementStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            hook);
    }

    ~ScopedPackageEditorWorksheetPartReplacementStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            nullptr);
    }

    ScopedPackageEditorWorksheetPartReplacementStagedHook(
        const ScopedPackageEditorWorksheetPartReplacementStagedHook&) = delete;
    ScopedPackageEditorWorksheetPartReplacementStagedHook& operator=(
        const ScopedPackageEditorWorksheetPartReplacementStagedHook&) = delete;
};

void fail_package_editor_worksheet_part_replacement_after_staging()
{
    throw std::runtime_error("injected worksheet part replacement commit failure");
}

void test_package_editor_worksheet_staged_failure_preserves_state_and_retries()
{
    const SourcePackage source = write_source_package(
        "fastxlsx-package-editor-worksheet-staging-failure-source.xlsx");
    const std::filesystem::path failed_output = output_path(
        "fastxlsx-package-editor-worksheet-staging-failure-output.xlsx");
    const std::filesystem::path retry_output = output_path(
        "fastxlsx-package-editor-worksheet-staging-retry-output.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string prior_opaque = "prior opaque replacement";
    const std::string replacement_worksheet =
        R"(<worksheet><sheetData><row r="2"><c r="A2"><f>A1+1</f></c></row></sheetData></worksheet>)";
    const auto replacement_chunks = [&] {
        return std::vector<fastxlsx::detail::PackageEntryChunk> {
            fastxlsx::detail::PackageEntryChunk::memory(replacement_worksheet),
        };
    };

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_part_chunks(opaque_part,
        {fastxlsx::detail::PackageEntryChunk::memory(prior_opaque)},
        "prior opaque replacement before injected worksheet replacement failure");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const std::size_t initial_package_entry_count =
        editor.edit_plan().package_entries().size();
    const std::size_t initial_removed_part_count =
        editor.edit_plan().removed_parts().size();
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();
    const std::size_t initial_relationship_target_audit_count =
        editor.edit_plan().relationship_target_audits().size();
    const std::size_t initial_worksheet_relationship_audit_count =
        editor.edit_plan().worksheet_relationship_reference_audits().size();
    const std::size_t initial_worksheet_payload_audit_count =
        editor.edit_plan().worksheet_payload_dependency_audits().size();
    const std::size_t initial_workbook_payload_audit_count =
        editor.edit_plan().workbook_payload_dependency_audits().size();

    bool failed = false;
    {
        ScopedPackageEditorWorksheetPartReplacementStagedHook hook(
            fail_package_editor_worksheet_part_replacement_after_staging);
        try {
            editor.replace_part_chunks(worksheet_part, replacement_chunks(),
                "worksheet replacement with injected commit failure");
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(),
                "injected worksheet part replacement commit failure",
                "worksheet replacement staged failure should preserve injected context");
        }
    }

    check(failed,
        "PackageEditor should surface injected worksheet replacement commit failure");
    check(editor.edit_plan().size() == initial_plan_size,
        "worksheet replacement staged failure should preserve edit-plan parts");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "worksheet replacement staged failure should preserve notes");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "worksheet replacement staged failure should preserve package-entry audits");
    check(editor.edit_plan().removed_parts().size() == initial_removed_part_count,
        "worksheet replacement staged failure should preserve removed-part audits");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "worksheet replacement staged failure should preserve removed-entry audits");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "worksheet replacement staged failure should preserve relationship audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_audit_count,
        "worksheet replacement staged failure should preserve worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_audit_count,
        "worksheet replacement staged failure should preserve worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_audit_count,
        "worksheet replacement staged failure should preserve workbook payload audits");
    check(!editor.edit_plan().full_calculation_on_load(),
        "worksheet replacement staged failure should not request recalculation");
    check(editor.edit_plan().calc_chain_action()
            == fastxlsx::detail::CalcChainAction::Preserve,
        "worksheet replacement staged failure should preserve calcChain policy");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"generic staged package part chunk replacement targeting a worksheet part"}),
        "worksheet replacement staged failure should not publish generic routing note");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events"}),
        "worksheet replacement staged failure should not publish worksheet audit note");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement staged failure should keep worksheet copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "worksheet replacement staged failure should keep workbook copy-original");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement staged failure should preserve prior opaque replacement");

    editor.save_as(failed_output);
    const fastxlsx::detail::PackageReader failed_reader =
        fastxlsx::detail::PackageReader::open(failed_output);
    check_entry_bytes(failed_reader, worksheet_part.zip_path(), source.worksheet);
    check_entry_bytes(failed_reader, workbook_part.zip_path(), source.workbook);
    check_entry_bytes(failed_reader, opaque_part.zip_path(), prior_opaque);

    editor.replace_part_chunks(worksheet_part, replacement_chunks(),
        "worksheet replacement retry after injected commit failure");
    check(editor.edit_plan().full_calculation_on_load(),
        "worksheet replacement retry should request recalculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"generic staged package part chunk replacement targeting a worksheet part",
                  "worksheet-aware validation"}),
        "worksheet replacement retry should publish generic routing note");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "worksheet replacement retry should publish worksheet audit note");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "worksheet replacement retry should publish worksheet stream rewrite");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "worksheet replacement retry should publish workbook calc rewrite");

    editor.save_as(retry_output);
    const fastxlsx::detail::PackageReader retry_reader =
        fastxlsx::detail::PackageReader::open(retry_output);
    check_entry_bytes(retry_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(retry_reader.read_entry(workbook_part.zip_path()),
        "fullCalcOnLoad=\"1\"",
        "worksheet replacement retry should write workbook calc metadata");
    check_entry_bytes(retry_reader, opaque_part.zip_path(), prior_opaque);
}

void test_package_editor_generic_staged_chunks_route_worksheet_targets()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-generic-worksheet-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-generic-worksheet-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-generic-worksheet-staged-chunks-body.xml");
    const std::string worksheet_prefix =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData>)";
    const std::string worksheet_body =
        R"(<row r="2"><c r="A2"><f>A1+1</f></c></row>)";
    const std::string worksheet_suffix =
        R"(</sheetData><drawing r:id="rIdMissing"/></worksheet>)";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        },
        "generic caller supplied worksheet chunks");

    check(editor.edit_plan().full_calculation_on_load(),
        "generic staged chunks targeting a worksheet should use worksheet calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"generic staged package part chunk replacement targeting a worksheet part",
                  "worksheet-aware validation"}),
        "generic worksheet chunks should report worksheet-aware routing");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "generic worksheet chunks should use combined chunk-source worksheet audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "generic worksheet chunks should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "generic worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "generic worksheet chunks should expose worksheet staged rewrite reason");

    const auto output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "generic worksheet chunks should appear as worksheet stream rewrite");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "generic worksheet chunks should still rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "generic worksheet chunks should audit formulas from staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "generic worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);

    fastxlsx::detail::PackageEditor invalid_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_chunks_failed = false;
    try {
        invalid_chunks_editor.replace_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory("<!DOCTYPE worksheet><worksheet/>"),
            },
            "invalid generic worksheet chunks");
    } catch (const std::exception&) {
        invalid_chunks_failed = true;
    }
    check(invalid_chunks_failed, "invalid generic staged worksheet chunks should fail");
    check(!invalid_chunks_editor.edit_plan().full_calculation_on_load(),
        "invalid generic staged worksheet chunks should not request recalculation");
    check(invalid_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid generic staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid generic staged worksheet chunks should not change manifest state");
}

void test_package_editor_replaces_worksheet_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-staged-chunks-body.xml");
    const std::string worksheet_prefix =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData>)";
    const std::string worksheet_body =
        R"(<row r="2"><c r="A2"><f>A1+1</f></c></row>)";
    const std::string worksheet_suffix =
        R"(</sheetData><drawing r:id="rIdMissing"/></worksheet>)";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks(worksheet_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    check(editor.edit_plan().full_calculation_on_load(),
        "staged worksheet chunks should keep worksheet replacement calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"staged chunk replacement", "one chunk-source audit reader"}),
        "staged worksheet chunks should report combined chunk-source validation/audit");
    check(!has_note_containing(editor.edit_plan().notes(),
              {"materialized worksheet XML"}),
        "staged worksheet chunks should not retain the legacy materialized-audit note");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "staged worksheet chunks should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "staged worksheet chunks should expose staged rewrite reason");
    const auto output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "staged worksheet chunks should appear as worksheet stream rewrite");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "staged worksheet chunks should still rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "staged worksheet chunks should expose combined validation/audit chunk-source handoff");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "staged worksheet chunks should audit formulas from the staged chunks");
    check(has_note_containing(output_plan.notes,
              {"worksheet drawing relationship metadata", "linked parts require caller review"}),
        "staged worksheet chunks should audit relationship-bearing metadata from the combined audit reader");
    check(has_note_containing(output_plan.notes,
              {"relationship id rIdMissing", "<drawing>", "repair worksheet .rels"}),
        "staged worksheet chunks should audit relationship ids from the combined audit reader");
    check(!output_plan.worksheet_payload_dependency_audits.empty(),
        "staged worksheet chunks should keep structured payload dependency audits");
    check(!output_plan.worksheet_relationship_reference_audits.empty(),
        "staged worksheet chunks should keep structured relationship-id audits");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "staged worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);
    check_entry_bytes(output_reader, "[Content_Types].xml", source.content_types);
    check_entry_bytes(output_reader, "_rels/.rels", source.package_relationships);
    check_entry_bytes(output_reader, "xl/_rels/workbook.xml.rels", source.workbook_relationships);

    fastxlsx::detail::PackageEditor empty_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool empty_chunks_failed = false;
    try {
        empty_chunks_editor.replace_worksheet_part_chunks(worksheet_part, {});
    } catch (const std::exception&) {
        empty_chunks_failed = true;
    }
    check(empty_chunks_failed, "empty staged worksheet chunks should fail");
    check(!empty_chunks_editor.edit_plan().full_calculation_on_load(),
        "empty staged worksheet chunks should not request recalculation");
    check(empty_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(empty_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "empty staged worksheet chunks should not change manifest state");

    fastxlsx::detail::PackageEditor invalid_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_chunks_failed = false;
    try {
        invalid_chunks_editor.replace_worksheet_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory("<!DOCTYPE worksheet><worksheet/>"),
            });
    } catch (const std::exception&) {
        invalid_chunks_failed = true;
    }
    check(invalid_chunks_failed, "invalid staged worksheet chunks should fail");
    check(!invalid_chunks_editor.edit_plan().full_calculation_on_load(),
        "invalid staged worksheet chunks should not request recalculation");
    check(invalid_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid staged worksheet chunks should not change manifest state");

    fastxlsx::detail::PackageEditor invalid_event_chunks_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t invalid_event_initial_plan_size =
        invalid_event_chunks_editor.edit_plan().size();
    const std::size_t invalid_event_initial_note_count =
        invalid_event_chunks_editor.edit_plan().notes().size();
    bool invalid_event_chunks_failed = false;
    try {
        invalid_event_chunks_editor.replace_worksheet_part_chunks(
            worksheet_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(
                    R"(<worksheet><row r="1"/></worksheet>)"),
            });
    } catch (const std::exception& error) {
        invalid_event_chunks_failed = true;
        check_contains(error.what(), "row outside sheetData",
            "event-validated staged worksheet chunks should reject row outside sheetData");
    }
    check(invalid_event_chunks_failed,
        "staged worksheet chunks should fail when event validation rejects the worksheet");
    check(invalid_event_chunks_editor.edit_plan().size() == invalid_event_initial_plan_size,
        "event-invalid staged worksheet chunks should not change edit-plan parts");
    check(invalid_event_chunks_editor.edit_plan().notes().size() == invalid_event_initial_note_count,
        "event-invalid staged worksheet chunks should not add notes");
    check(!invalid_event_chunks_editor.edit_plan().full_calculation_on_load(),
        "event-invalid staged worksheet chunks should not request recalculation");
    check(invalid_event_chunks_editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "event-invalid staged worksheet chunks should not change worksheet edit-plan state");
    check_manifest_write_mode(invalid_event_chunks_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "event-invalid staged worksheet chunks should not change manifest state");
}

void test_package_editor_replaces_worksheet_by_name_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-by-name-staged-chunks-source.xlsx");
    SourcePackage source_with_namespaced_catalog = source;
    source_with_namespaced_catalog.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    fastxlsx::detail::write_package(source_with_namespaced_catalog.path,
        {
            {"[Content_Types].xml", source_with_namespaced_catalog.content_types},
            {"_rels/.rels", source_with_namespaced_catalog.package_relationships},
            {"xl/workbook.xml", source_with_namespaced_catalog.workbook},
            {"xl/_rels/workbook.xml.rels", source_with_namespaced_catalog.workbook_relationships},
            {"docProps/core.xml", source_with_namespaced_catalog.core_properties},
            {"xl/worksheets/sheet1.xml", source_with_namespaced_catalog.worksheet},
            {"custom/opaque.bin", source_with_namespaced_catalog.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-by-name-staged-chunks-body.xml");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="4"><c r="B4"><f>A1+3</f></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source_with_namespaced_catalog.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_chunks_by_name("Sheet1",
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    check(editor.edit_plan().full_calculation_on_load(),
        "by-name staged worksheet chunks should use worksheet replacement calc policy");
    check(has_note_containing(editor.edit_plan().notes(),
              {"by-name worksheet staged chunk replacement", "provided chunks"}),
        "by-name staged worksheet chunks should report by-name staged handoff");
    check(has_note_containing(editor.edit_plan().notes(),
              {"worksheet staged chunk replacement validates worksheet root/events",
                  "one chunk-source audit reader"}),
        "by-name staged worksheet chunks should reuse combined chunk-source validation/audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "by-name staged worksheet chunks should resolve worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "by-name staged worksheet chunks should keep worksheet stream rewrite mode");
    check(worksheet_plan->reason.find("staged stream rewrite chunks") != std::string::npos,
        "by-name staged worksheet chunks should expose staged rewrite reason");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "by-name staged worksheet chunks output plan should stream-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "by-name staged worksheet chunks output plan should rewrite workbook calc metadata");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "by-name staged worksheet chunks should audit formulas from staged chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "by-name staged worksheet chunks output should keep sheet catalog readable");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "by-name staged worksheet chunks should request full workbook recalculation");
    check_entry_bytes(output_reader, "custom/opaque.bin", source_with_namespaced_catalog.unknown);

    fastxlsx::detail::PackageEditor missing_name_editor =
        fastxlsx::detail::PackageEditor::open(source_with_namespaced_catalog.path);
    bool missing_name_failed = false;
    try {
        missing_name_editor.replace_worksheet_part_chunks_by_name("Missing",
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });
    } catch (const std::exception&) {
        missing_name_failed = true;
    }
    check(missing_name_failed,
        "missing sheet name should fail before by-name staged worksheet chunk state changes");
    check(!missing_name_editor.edit_plan().full_calculation_on_load(),
        "missing by-name staged worksheet chunks should not request recalculation");
    check_manifest_write_mode(missing_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing by-name staged worksheet chunks should not change worksheet manifest state");
}

void test_package_editor_replaces_worksheet_by_planned_name_with_staged_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-worksheet-planned-name-staged-chunks-body.xml");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="6"><c r="C6"><v>66</v></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;
    write_binary_file(body_path, worksheet_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="ChunkRenamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name staged chunks");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    const std::size_t queued_package_entry_count =
        editor.edit_plan().package_entries().size();
    bool old_name_failed = false;
    try {
        editor.replace_worksheet_part_chunks_by_name("Sheet1",
            {
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
            });
    } catch (const std::exception& error) {
        old_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned by-name staged chunks should reject old source sheet name");
    }
    check(old_name_failed,
        "planned by-name staged chunks should use planned workbook sheet names");
    check(editor.edit_plan().size() == queued_plan_size,
        "planned old-name staged chunk failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "planned old-name staged chunk failure should not append notes");
    check(editor.edit_plan().package_entries().size() == queued_package_entry_count,
        "planned old-name staged chunk failure should preserve package-entry audit count");
    check(!editor.edit_plan().full_calculation_on_load(),
        "planned old-name staged chunk failure should not request recalculation");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned old-name staged chunk failure should keep workbook rewrite");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "planned old-name staged chunk failure should leave worksheet copy-original");

    editor.replace_worksheet_part_chunks_by_name("ChunkRenamed",
        {
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix),
        });

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "planned by-name staged chunks should resolve renamed worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "planned by-name staged chunks should stream-rewrite worksheet");
    const auto* workbook_plan = editor.edit_plan().find_part(workbook_part);
    check(workbook_plan != nullptr
            && workbook_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "planned by-name staged chunks should keep workbook local-DOM rewrite");
    check(editor.edit_plan().full_calculation_on_load(),
        "planned by-name staged chunks should request workbook recalculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "planned by-name staged chunks output plan should expose workbook rewrite");
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "planned by-name staged chunks output plan should expose worksheet stream rewrite");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("ChunkRenamed") == worksheet_part,
        "planned by-name staged chunks output should expose renamed sheet catalog");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    const std::string output_workbook = output_reader.read_entry(workbook_part.zip_path());
    check_contains(output_workbook, R"(name="ChunkRenamed")",
        "planned by-name staged chunks output should keep planned sheet name");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "planned by-name staged chunks output should request workbook recalculation");
}

void test_package_editor_replaces_worksheet_from_chunk_source()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-chunk-source-output.xlsx");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="8"><c r="D8"><f>A1+7</f></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    editor.replace_worksheet_part_from_chunk_source(worksheet_part,
        make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));

    check(editor.edit_plan().full_calculation_on_load(),
        "chunk-source worksheet replacement should request full calculation");
    check(has_note_containing(editor.edit_plan().notes(),
              {"pull-based chunk source", "file-backed staged chunk"}),
        "chunk-source worksheet replacement should expose file-backed staged handoff");
    check(has_note_containing(editor.edit_plan().notes(),
              {"target/workbook/calc policy preflight", "pull-based chunk source"}),
        "chunk-source worksheet replacement should document preflight before consuming chunks");
    check(has_note_containing(editor.edit_plan().notes(),
              {"writes the staged worksheet chunk in one caller chunk-source pass",
                  "without reopening that staged chunk"}),
        "chunk-source worksheet replacement should fuse staging with validation/audit");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "chunk-source worksheet replacement should record worksheet edit-plan entry");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "chunk-source worksheet replacement should stream-rewrite worksheet");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "chunk-source worksheet replacement output plan should stream-rewrite worksheet");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        worksheet_part.zip_path(), true,
        "chunk-source worksheet replacement output plan should expose staged chunks instead of string data");
    check_output_entry_plan(output_plan.entries, workbook_part.zip_path(),
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "chunk-source worksheet replacement output plan should rewrite workbook calc metadata");
    check_output_entry_staged_replacement_chunks(output_plan.entries,
        workbook_part.zip_path(), false,
        "workbook calc metadata rewrite remains small XML and should not be marked as staged chunks");
    check(has_note_containing(output_plan.notes,
              {"contains formulas", "calcChain policy"}),
        "chunk-source worksheet replacement should audit formulas from streamed chunks");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_contains(output_reader.read_entry(workbook_part.zip_path()), "fullCalcOnLoad=\"1\"",
        "chunk-source worksheet replacement should request workbook recalculation in output");
    check_entry_bytes(output_reader, "custom/opaque.bin", source.unknown);

    fastxlsx::detail::PackageEditor invalid_source_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    bool invalid_source_failed = false;
    try {
        invalid_source_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            make_test_chunk_source({"<!DOCTYPE worksheet><worksheet/>"}));
    } catch (const std::exception&) {
        invalid_source_failed = true;
    }
    check(invalid_source_failed, "invalid worksheet chunk source should fail");
    check(!invalid_source_editor.edit_plan().full_calculation_on_load(),
        "invalid worksheet chunk source should not request recalculation");
    check_manifest_write_mode(invalid_source_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "invalid worksheet chunk source should not change worksheet manifest state");

    fastxlsx::detail::PackageEditor throwing_source_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t throwing_source_initial_plan_size =
        throwing_source_editor.edit_plan().size();
    const std::size_t throwing_source_initial_note_count =
        throwing_source_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> throwing_source_temp_files_before =
        package_editor_temp_files();
    int throwing_source_reads = 0;
    bool throwing_source_failed = false;
    try {
        throwing_source_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++throwing_source_reads;
                if (throwing_source_reads == 1) {
                    chunk = "<worksheet><sheetData>";
                    return true;
                }
                throw std::runtime_error("caller worksheet stream stopped");
            });
    } catch (const std::exception& error) {
        throwing_source_failed = true;
        check_contains(error.what(),
            "failed while reading planned worksheet replacement chunk source",
            "throwing worksheet chunk source should name the staging boundary");
        check_contains(error.what(), "caller worksheet stream stopped",
            "throwing worksheet chunk source should preserve the caller failure");
    }
    check(throwing_source_failed,
        "throwing chunk-source worksheet replacement should fail");
    check(throwing_source_reads == 2,
        "throwing chunk-source worksheet replacement should stop at the throwing read");
    check(throwing_source_editor.edit_plan().size() == throwing_source_initial_plan_size,
        "throwing chunk-source worksheet replacement should not change edit-plan size");
    check(throwing_source_editor.edit_plan().notes().size()
            == throwing_source_initial_note_count,
        "throwing chunk-source worksheet replacement should not add notes");
    check(!throwing_source_editor.edit_plan().full_calculation_on_load(),
        "throwing chunk-source worksheet replacement should not request recalculation");
    check_manifest_write_mode(throwing_source_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "throwing chunk-source worksheet replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(throwing_source_temp_files_before,
        "throwing chunk-source worksheet replacement should not leak staged temp files");

    fastxlsx::detail::PackageEditor missing_target_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    int missing_target_reads = 0;
    bool missing_target_failed = false;
    try {
        missing_target_editor.replace_worksheet_part_from_chunk_source(
            fastxlsx::detail::PartName("/xl/worksheets/missing.xml"),
            [&](std::string& chunk) {
                ++missing_target_reads;
                chunk = "<worksheet/>";
                return true;
            });
    } catch (const std::exception& error) {
        missing_target_failed = true;
        check_contains(error.what(), "worksheet replacement target is not present",
            "missing target chunk-source failure should name the worksheet target");
        check_contains(error.what(), "worksheet part '/xl/worksheets/missing.xml'",
            "missing target chunk-source failure should include the requested worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/missing.xml'",
            "missing target chunk-source failure should include the requested worksheet entry");
    }
    check(missing_target_failed,
        "missing target chunk-source worksheet replacement should fail");
    check(missing_target_reads == 0,
        "missing target chunk-source worksheet replacement should fail before consuming input");
    check(!missing_target_editor.edit_plan().full_calculation_on_load(),
        "missing target chunk-source failure should not request recalculation");

    fastxlsx::detail::PackageEditor rebuild_policy_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::ReferencePolicy rebuild_policy;
    rebuild_policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Rebuild;
    int rebuild_policy_reads = 0;
    bool rebuild_policy_failed = false;
    try {
        rebuild_policy_editor.replace_worksheet_part_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++rebuild_policy_reads;
                chunk = "<worksheet/>";
                return true;
            },
            rebuild_policy);
    } catch (const std::exception& error) {
        rebuild_policy_failed = true;
        check_contains(error.what(), "calcChain rebuild is not implemented",
            "rebuild policy chunk-source failure should name calcChain rebuild");
    }
    check(rebuild_policy_failed,
        "rebuild policy chunk-source worksheet replacement should fail");
    check(rebuild_policy_reads == 0,
        "rebuild policy chunk-source worksheet replacement should fail before consuming input");
    check(!rebuild_policy_editor.edit_plan().full_calculation_on_load(),
        "rebuild policy chunk-source failure should not request recalculation");
}

void test_package_editor_replaces_worksheet_by_name_from_chunk_source()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-worksheet-by-name-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-worksheet-by-name-chunk-source-output.xlsx");
    const std::string worksheet_prefix = "<worksheet><sheetData>";
    const std::string worksheet_body =
        R"(<row r="11"><c r="E11"><v>111</v></c></row>)";
    const std::string worksheet_suffix = "</sheetData></worksheet>";
    const std::string replacement_worksheet =
        worksheet_prefix + worksheet_body + worksheet_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string replacement_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="ChunkSourceRenamed" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    editor.replace_part(workbook_part, replacement_workbook,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "ordinary workbook replacement before by-name chunk-source worksheet replacement");

    const std::size_t queued_plan_size = editor.edit_plan().size();
    const std::size_t queued_note_count = editor.edit_plan().notes().size();
    bool old_name_failed = false;
    try {
        editor.replace_worksheet_part_from_chunk_source_by_name("Sheet1",
            make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));
    } catch (const std::exception& error) {
        old_name_failed = true;
        check_contains(error.what(), "workbook sheet name is not present",
            "planned by-name chunk-source replacement should reject old source sheet name");
    }
    check(old_name_failed,
        "planned by-name chunk-source replacement should use planned workbook sheet names");
    check(editor.edit_plan().size() == queued_plan_size,
        "old-name chunk-source failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == queued_note_count,
        "old-name chunk-source failure should not append notes");
    check(!editor.edit_plan().full_calculation_on_load(),
        "old-name chunk-source failure should not request recalculation");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "old-name chunk-source failure should leave worksheet copy-original");

    editor.replace_worksheet_part_from_chunk_source_by_name("ChunkSourceRenamed",
        make_test_chunk_source({worksheet_prefix, worksheet_body, worksheet_suffix}));

    check(has_note_containing(editor.edit_plan().notes(),
              {"by-name worksheet chunk-source replacement", "planned/source workbook catalog"}),
        "by-name chunk-source replacement should expose catalog-based handoff");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr
            && worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "by-name chunk-source replacement should stream-rewrite worksheet");
    check(editor.edit_plan().full_calculation_on_load(),
        "by-name chunk-source replacement should request full calculation");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("ChunkSourceRenamed") == worksheet_part,
        "by-name chunk-source output should expose planned sheet catalog");
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    const std::string output_workbook = output_reader.read_entry(workbook_part.zip_path());
    check_contains(output_workbook, R"(name="ChunkSourceRenamed")",
        "by-name chunk-source output should keep planned sheet name");
    check_contains(output_workbook, R"(fullCalcOnLoad="1")",
        "by-name chunk-source output should request workbook recalculation");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "core-worksheet")) {
            test_package_editor_worksheet_staged_failure_preserves_state_and_retries();
            test_package_editor_generic_staged_chunks_route_worksheet_targets();
            test_package_editor_replaces_worksheet_with_staged_chunks();
            test_package_editor_replaces_worksheet_by_name_with_staged_chunks();
            test_package_editor_replaces_worksheet_by_planned_name_with_staged_chunks();
            test_package_editor_replaces_worksheet_from_chunk_source();
            test_package_editor_replaces_worksheet_by_name_from_chunk_source();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
