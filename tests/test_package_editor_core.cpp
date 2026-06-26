#include "test_package_editor_core_common.hpp"

void test_package_editor_noop_save_preserves_all_source_entries()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-noop-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-noop-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    check(editor.edit_plan().size() == editor.manifest().size(),
        "no-op edit plan should include one part entry per manifest part");
    for (const fastxlsx::detail::PackagePart& part : editor.manifest().parts()) {
        const auto* plan_entry = editor.edit_plan().find_part(part.name);
        check(plan_entry != nullptr,
            "no-op edit plan should include every manifest part");
        check(plan_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "no-op edit plan should keep manifest parts copy-original");
        check(part.write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal
                && part.preserve_original && !part.dirty && !part.generated,
            "no-op manifest should keep source parts copy-original");
    }
    check(editor.edit_plan().package_entries().empty(),
        "no-op edit plan should not record metadata package-entry rewrites");
    check(editor.edit_plan().removed_parts().empty(),
        "no-op edit plan should not remove parts");
    check(editor.edit_plan().removed_package_entries().empty(),
        "no-op edit plan should not omit package entries");
    check(!editor.edit_plan().full_calculation_on_load(),
        "no-op edit plan should not request full calculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "no-op edit plan should preserve calcChain policy");
    check(editor.edit_plan().notes().empty(),
        "no-op edit plan should not add dependency audit notes");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "no-op output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "no-op output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "no-op output plan should not carry audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "no-op output plan should not carry relationship target audits");
    check(output_plan.entries.size() == editor.reader().entries().size(),
        "no-op output plan should include one decision per source entry");
    check(editor.planned_output_entries().size() == output_plan.entries.size(),
        "legacy output-entry preview should match aggregate output plan entries");
    for (const fastxlsx::detail::PackageReaderEntry& entry : editor.reader().entries()) {
        check_output_entry_plan(output_plan.entries, entry.name,
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "no-op output plan should copy every source entry");
    }
    check_source_package_parts_are_file_backed(editor.reader(), output_plan.entries,
        "no-op output plan should file-back every source package part copy");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false, "",
        "no-op output plan should classify content types as metadata entry");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "[Content_Types].xml",
        "content types",
        "no-op output plan should expose file-backed content-types metadata copy");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "_rels/.rels",
        "package relationships",
        "no-op output plan should expose file-backed package relationships metadata copy");
    check_source_metadata_entry_is_file_backed(output_plan.entries, "xl/_rels/workbook.xml.rels",
        "relationships",
        "no-op output plan should expose file-backed workbook relationships metadata copy");
    check_output_entry_part_context(output_plan.entries, "xl/workbook.xml", true,
        "/xl/workbook.xml",
        "no-op output plan should classify workbook XML as a package part");
    const auto* workbook_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/workbook.xml");
    check(workbook_output_plan != nullptr && workbook_output_plan->file_backed_source_copy,
        "no-op output plan should copy workbook source entries through file-backed chunks");
    check_contains(workbook_output_plan->file_backed_source_copy_reason, "package part",
        "workbook file-backed source-copy plan should explain the package-part reason");
    check_output_entry_part_context(output_plan.entries, "xl/worksheets/sheet1.xml", true,
        "/xl/worksheets/sheet1.xml",
        "no-op output plan should classify worksheet XML as a package part");
    const auto* worksheet_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml");
    check(worksheet_output_plan != nullptr && worksheet_output_plan->file_backed_source_copy,
        "no-op output plan should copy worksheet source entries through file-backed chunks");
    check_contains(worksheet_output_plan->file_backed_source_copy_reason, "worksheet",
        "worksheet file-backed source-copy plan should explain the worksheet reason");
    check_output_entry_part_context(output_plan.entries, "xl/sharedStrings.xml", true,
        "/xl/sharedStrings.xml",
        "no-op output plan should classify sharedStrings as a package part");
    const auto* shared_strings_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/sharedStrings.xml");
    check(shared_strings_output_plan != nullptr
            && shared_strings_output_plan->file_backed_source_copy,
        "no-op output plan should copy sharedStrings source entries through file-backed chunks");
    check_contains(shared_strings_output_plan->file_backed_source_copy_reason, "sharedStrings",
        "sharedStrings file-backed source-copy plan should explain the sharedStrings reason");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "no-op output plan should classify unknown extension as a package part");
    const auto* opaque_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(opaque_output_plan != nullptr && opaque_output_plan->file_backed_source_copy,
        "no-op output plan should copy unknown package parts through file-backed chunks");
    check_contains(opaque_output_plan->file_backed_source_copy_reason, "package part",
        "unknown package part file-backed source-copy plan should explain the generic reason");
    check_output_entry_part_context(output_plan.entries, "custom/_rels/opaque-extension.bin.rels",
        false, "",
        "no-op output plan should classify unknown owner relationships as metadata entry");
    check_source_metadata_entry_is_file_backed(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", "relationships",
        "no-op output plan should expose file-backed unknown owner relationships metadata copy");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, "[Content_Types].xml", source.content_types);
    check_entry_bytes(output_reader, "_rels/.rels", source.package_relationships);
    check_entry_bytes(output_reader, "xl/workbook.xml", source.workbook);
    check_entry_bytes(output_reader, "xl/_rels/workbook.xml.rels",
        source.workbook_relationships);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
    check_entry_bytes(output_reader, "xl/worksheets/_rels/sheet1.xml.rels",
        source.worksheet_relationships);
    check_entry_bytes(output_reader, "xl/drawings/drawing1.xml", source.drawing);
    check_entry_bytes(output_reader, "xl/drawings/_rels/drawing1.xml.rels",
        source.drawing_relationships);
    check_entry_bytes(output_reader, "xl/charts/chart1.xml", source.chart);
    check_entry_bytes(output_reader, "xl/media/image1.png", source.media);
    check_entry_bytes(output_reader, "xl/tables/table1.xml", source.table);
    check_entry_bytes(output_reader, "xl/drawings/vmlDrawing1.vml", source.vml_drawing);
    check_entry_bytes(output_reader, "xl/drawings/drawing space.xml",
        source.percent_encoded_drawing);
    check_entry_bytes(output_reader, "xl/sharedStrings.xml", source.shared_strings);
    check_entry_bytes(output_reader, "xl/_rels/sharedStrings.xml.rels",
        source.shared_strings_relationships);
    check_entry_bytes(output_reader, "xl/styles.xml", source.styles);
    check_entry_bytes(output_reader, "xl/vbaProject.bin", source.vba_project);
    check_entry_bytes(output_reader, "xl/calcChain.xml", source.calc_chain);
    check_entry_bytes(output_reader, "custom/opaque-extension.bin",
        source.opaque_extension);
    check_entry_bytes(output_reader, "custom/_rels/opaque-extension.bin.rels",
        source.opaque_extension_relationships);
}

void test_package_editor_file_backs_copy_original_package_part_source_entries()
{
    LinkedObjectSourcePackage source =
        write_linked_object_source_package("fastxlsx-package-editor-large-source-copy.xlsx");
    source.opaque_extension.assign(70U * 1024U, 'L');
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-large-source-copy-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_source_package_parts_are_file_backed(editor.reader(), output_plan.entries,
        "large source-copy output plan should file-back every source package part copy");
    check_output_entry_part_context(output_plan.entries, "custom/opaque-extension.bin", true,
        "/custom/opaque-extension.bin",
        "large source-copy output plan should classify unknown extension as a package part");
    const auto* opaque_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin");
    check(opaque_output_plan != nullptr && opaque_output_plan->file_backed_source_copy,
        "large copy-original package parts should use file-backed source copy");
    check_contains(opaque_output_plan->file_backed_source_copy_reason, "package part",
        "large source-copy output plan should explain the package-part reason");

    const auto* media_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/media/image1.png");
    check(media_output_plan != nullptr && media_output_plan->file_backed_source_copy,
        "small package parts should also use file-backed source copy");
    check_contains(media_output_plan->file_backed_source_copy_reason, "package part",
        "small package parts should carry a generic file-backed reason");
    const auto* workbook_output_plan =
        find_output_entry_plan(output_plan.entries, "xl/workbook.xml");
    check(workbook_output_plan != nullptr && workbook_output_plan->file_backed_source_copy,
        "workbook XML source-copy output should use file-backed source copy");
    check_contains(workbook_output_plan->file_backed_source_copy_reason, "package part",
        "workbook XML source-copy output should carry a generic file-backed reason");
    const auto* opaque_rels_output_plan =
        find_output_entry_plan(output_plan.entries, "custom/_rels/opaque-extension.bin.rels");
    check(opaque_rels_output_plan != nullptr
            && opaque_rels_output_plan->file_backed_source_copy,
        "metadata relationship entries should use file-backed source copy");
    check(opaque_rels_output_plan != nullptr
            && !opaque_rels_output_plan->file_backed_source_copy_reason.empty(),
        "metadata relationship entries should carry a file-backed source-copy reason");
    check_source_metadata_entry_is_file_backed(output_plan.entries,
        "custom/_rels/opaque-extension.bin.rels", "relationships",
        "metadata relationship entries should expose file-backed metadata source-copy reason");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, "custom/opaque-extension.bin",
        source.opaque_extension);
    check_entry_bytes(output_reader, "xl/media/image1.png", source.media);
    check_entry_bytes(output_reader, "xl/workbook.xml", source.workbook);
    check_entry_bytes(output_reader, "custom/_rels/opaque-extension.bin.rels",
        source.opaque_extension_relationships);
}

void test_package_editor_replaces_one_part_and_preserves_unknown_parts()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    check(editor.manifest().find_part(core_part) != nullptr,
        "PackageEditor manifest should include core properties");
    const auto* initial_unknown_plan = editor.edit_plan().find_part(unknown_part);
    check(initial_unknown_plan != nullptr,
        "PackageEditor initial plan should include unknown part");
    check(initial_unknown_plan->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor initial plan should copy unknown part");

    const std::string replacement_core =
        "<cp:coreProperties><dc:creator>Updated</dc:creator></cp:coreProperties>";
    editor.replace_part(core_part, replacement_core,
        fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "test core properties rewrite");

    const auto* core_plan = editor.edit_plan().find_part(core_part);
    check(core_plan != nullptr, "PackageEditor edit plan should include replaced part");
    check(core_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor edit plan should mark replaced part local-DOM-rewrite");
    const auto* core_manifest_part = editor.manifest().find_part(core_part);
    check(core_manifest_part != nullptr,
        "PackageEditor manifest should keep replaced part visible");
    check(core_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "PackageEditor manifest should mirror replaced part write mode");
    check(core_manifest_part->dirty && !core_manifest_part->preserve_original
            && !core_manifest_part->generated,
        "PackageEditor manifest should mark local-DOM replacement dirty");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageEditor edit plan should keep unknown part copy-original");
    const auto* unknown_manifest_part = editor.manifest().find_part(unknown_part);
    check(unknown_manifest_part != nullptr,
        "PackageEditor manifest should keep unknown part visible");
    check(unknown_manifest_part->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal
            && unknown_manifest_part->preserve_original && !unknown_manifest_part->dirty,
        "PackageEditor manifest should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan replacement_output_plan =
        editor.planned_output();
    check_output_entry_materialized_replacement(replacement_output_plan.entries,
        core_part.zip_path(), true,
        "ordinary core properties replacement should expose materialized replacement output");
    check_output_entry_materialized_replacement_reason(replacement_output_plan.entries,
        core_part.zip_path(), "core properties package part",
        "ordinary core properties replacement should explain materialized replacement boundary");
    check_output_entry_staged_replacement_chunks(replacement_output_plan.entries,
        core_part.zip_path(), false,
        "ordinary core properties replacement should not expose staged chunks");
    check_output_entry_materialized_replacement(replacement_output_plan.entries,
        unknown_part.zip_path(), false,
        "copy-original unknown part should not expose materialized replacement output");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, core_part.zip_path());
    check(output_reader.read_entry("docProps/core.xml") == replacement_core,
        "PackageEditor should write replacement part bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "PackageEditor should preserve unknown part bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "PackageEditor should preserve untouched workbook bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "PackageEditor should preserve content types bytes when unchanged");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "PackageEditor should preserve package relationships bytes when unchanged");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels") == source.workbook_relationships,
        "PackageEditor should preserve workbook relationships bytes when unchanged");

    const auto* package_relationship =
        output_reader.package_relationships().find_by_id("rId2");
    check(package_relationship != nullptr,
        "preserved package relationships should remain readable");
    check(package_relationship->target == "docProps/core.xml",
        "preserved core properties relationship target mismatch");
}

void test_package_editor_staged_chunk_part_replacement_writes_chunks()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-chunks-source.xlsx");
    const std::filesystem::path chunk_output =
        output_path("fastxlsx-package-editor-staged-chunks-output.xlsx");
    const std::filesystem::path final_output =
        output_path("fastxlsx-package-editor-staged-chunks-final-output.xlsx");
    const std::filesystem::path restore_output =
        output_path("fastxlsx-package-editor-staged-chunks-restore-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-staged-chunks-body.xml");
    const std::string opaque_prefix = "chunked:";
    const std::string opaque_body = "file-backed-body";
    const std::string opaque_suffix = ":done";
    const std::string expected_chunked_opaque =
        opaque_prefix + opaque_body + opaque_suffix;
    write_binary_file(body_path, opaque_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "staged opaque stream chunks");

    const auto* opaque_plan = editor.edit_plan().find_part(opaque_part);
    check(opaque_plan != nullptr,
        "staged chunk replacement should record opaque edit-plan entry");
    check(opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunk replacement should be a stream rewrite");
    check(!editor.edit_plan().full_calculation_on_load(),
        "generic non-worksheet staged chunks should not request recalculation");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunk replacement should update manifest write mode");
    const fastxlsx::detail::PackageEditorOutputPlan chunk_plan = editor.planned_output();
    check_output_entry_plan(chunk_plan.entries, opaque_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "staged chunk replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(chunk_plan.entries, opaque_part.zip_path(),
        true,
        "staged chunk replacement output plan should expose active chunked replacement");
    check_output_entry_materialized_replacement(chunk_plan.entries, opaque_part.zip_path(),
        false,
        "staged chunk replacement output plan should not expose materialized replacement");

    editor.save_as(chunk_output);

    const fastxlsx::detail::PackageReader chunk_output_reader =
        fastxlsx::detail::PackageReader::open(chunk_output);
    check_preserved_source_entries(editor.reader(), chunk_output_reader, opaque_part.zip_path());
    check_entry_bytes(
        chunk_output_reader, opaque_part.zip_path(), expected_chunked_opaque);
    check_entry_bytes(chunk_output_reader, "xl/worksheets/sheet1.xml", source.worksheet);

    const std::string final_opaque = "final opaque bytes";
    replace_part_with_memory_chunks(editor, opaque_part, final_opaque,
        "final opaque staged rewrite");

    const auto* final_opaque_plan = editor.edit_plan().find_part(opaque_part);
    check(final_opaque_plan != nullptr,
        "final string replacement should keep opaque edit-plan entry");
    check(final_opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "final staged replacement should keep staged chunk write mode");
    const fastxlsx::detail::PackageEditorOutputPlan final_plan = editor.planned_output();
    check_output_entry_staged_replacement_chunks(final_plan.entries, opaque_part.zip_path(),
        true,
        "final staged replacement output plan should keep active chunked replacement marker");
    check_output_entry_materialized_replacement(final_plan.entries, opaque_part.zip_path(),
        false,
        "final staged replacement output plan should not expose active materialized replacement");

    editor.save_as(final_output);

    const fastxlsx::detail::PackageReader final_output_reader =
        fastxlsx::detail::PackageReader::open(final_output);
    check_preserved_source_entries(editor.reader(), final_output_reader, opaque_part.zip_path());
    check_entry_bytes(final_output_reader, opaque_part.zip_path(), final_opaque);

    fastxlsx::detail::PackageEditor restore_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    restore_editor.remove_part(opaque_part, "temporary opaque removal before staged chunks");
    restore_editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "restored opaque staged stream chunks");

    const auto* restored_opaque_plan = restore_editor.edit_plan().find_part(opaque_part);
    check(restored_opaque_plan != nullptr,
        "staged chunks after removal should restore the opaque edit-plan entry");
    check(restored_opaque_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunks after removal should keep stream rewrite mode");
    check(restore_editor.edit_plan().find_removed_part(opaque_part) == nullptr,
        "staged chunks after removal should clear stale removed-part audit");
    check(restore_editor.edit_plan().removed_package_entries().empty(),
        "staged chunks after removal should clear stale removed package entries");
    check_manifest_write_mode(restore_editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "staged chunks after removal should restore manifest write mode");
    const fastxlsx::detail::PackageEditorOutputPlan restore_plan =
        restore_editor.planned_output();
    check_output_entry_plan(restore_plan.entries,
        opaque_part.zip_path(), fastxlsx::detail::PartWriteMode::StreamRewrite,
        true, false, false, false,
        "staged chunks after removal should appear as active staged output");
    check_output_entry_staged_replacement_chunks(restore_plan.entries,
        opaque_part.zip_path(), true,
        "staged chunks after removal output plan should expose active chunked replacement");
    check_output_entry_materialized_replacement(restore_plan.entries,
        opaque_part.zip_path(), false,
        "staged chunks after removal output plan should not expose materialized replacement");

    restore_editor.save_as(restore_output);

    const fastxlsx::detail::PackageReader restore_output_reader =
        fastxlsx::detail::PackageReader::open(restore_output);
    check_preserved_source_entries(
        restore_editor.reader(), restore_output_reader, opaque_part.zip_path());
    check_entry_bytes(
        restore_output_reader, opaque_part.zip_path(), expected_chunked_opaque);
}

void test_package_editor_source_part_stored_entry_chunks_reference_source_package_payload()
{
    const CalcSourcePackage source =
        write_calc_source_package("fastxlsx-package-editor-source-entry-range-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    std::vector<fastxlsx::detail::PackageEntryChunk> chunks =
        editor.source_part_stored_entry_chunks(worksheet_part);
    check(chunks.size() == 1,
        "source stored-entry helper should expose one package file range chunk");
    const fastxlsx::detail::PackageEntryChunk& chunk = chunks.front();
    check(chunk.kind == fastxlsx::detail::PackageEntryChunk::Kind::File,
        "source stored-entry helper should expose a file-backed chunk");
    check(chunk.has_file_range,
        "source stored-entry helper should expose an explicit package payload range");
    check(chunk.path == editor.reader().path(),
        "source stored-entry helper should reference the source package file");
    check(chunk.has_expected_size
            && chunk.expected_size == static_cast<std::uint64_t>(source.worksheet.size()),
        "source stored-entry helper should record the source worksheet entry size");
    const fastxlsx::detail::PackageReaderEntry* source_entry =
        editor.reader().find_entry(worksheet_part.zip_path());
    check(source_entry != nullptr,
        "source stored-entry helper test should find the source worksheet entry");
    check(chunk.has_expected_crc32 && chunk.expected_crc32 == source_entry->crc32,
        "source stored-entry helper should reuse the source worksheet entry CRC");
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(chunks)
            == source.worksheet,
        "source stored-entry helper should read the worksheet bytes from the package payload");

    std::vector<fastxlsx::detail::PackageEntryChunk> by_name_chunks =
        editor.source_worksheet_part_stored_entry_chunks_by_name("Sheet1");
    check(by_name_chunks.size() == 1 && by_name_chunks.front().has_expected_crc32
            && by_name_chunks.front().expected_crc32 == source_entry->crc32,
        "by-name source stored-entry helper should reuse the source worksheet entry CRC");
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(
              std::move(by_name_chunks)) == source.worksheet,
        "by-name source stored-entry helper should resolve the source worksheet part");

    bool failed = false;
    try {
        (void)editor.source_part_stored_entry_chunks(
            fastxlsx::detail::PartName("/xl/worksheets/missing.xml"));
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "source package entry is missing",
            "missing source entry failure should explain the missing package payload");
    }
    check(failed, "source stored-entry helper should reject missing source entries");
}

void test_package_editor_prevalidated_staged_chunk_part_replacement_by_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-prevalidated-staged-chunk-source.xlsx");
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-prevalidated-staged-chunk-output.xlsx");
    const std::filesystem::path replacement_path =
        output_path("fastxlsx-package-editor-prevalidated-staged-chunk-worksheet.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string replacement_worksheet =
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheetData><row r="1"><c r="A1"><v>99</v></c></row></sheetData></worksheet>)";
    write_binary_file(replacement_path, replacement_worksheet);
    std::vector<fastxlsx::detail::PackageEntryChunk> prevalidated_chunks {
        fastxlsx::detail::PackageEntryChunk::file_range(
            replacement_path, 0, static_cast<std::uint64_t>(replacement_worksheet.size())),
    };
    check(!prevalidated_chunks.front().has_expected_crc32,
        "prevalidated file-range replacement should not need a descriptor-time CRC contract");
    fastxlsx::detail::ReferencePolicy policy;
    policy.request_full_calculation_on_sheet_rewrite = false;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_worksheet_part_prevalidated_chunks_by_name("Sheet1",
        std::move(prevalidated_chunks),
        policy);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "prevalidated staged chunk replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prevalidated staged chunk replacement should plan a stream rewrite");
    check_contains(worksheet_plan->reason, "prevalidated staged stream rewrite",
        "prevalidated staged chunk replacement should record the fast-path reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "prevalidated staged chunk replacement should update manifest write mode");
    check(!editor.edit_plan().full_calculation_on_load(),
        "prevalidated staged chunk replacement should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "prevalidated staged chunk replacement should preserve calcChain policy");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "prevalidated staged chunk replacement should appear as a staged stream rewrite");
    check_output_entry_staged_replacement_chunks(output_plan.entries, worksheet_part.zip_path(),
        true,
        "prevalidated staged chunk replacement output plan should expose staged chunks");
    check_output_entry_materialized_replacement(output_plan.entries, worksheet_part.zip_path(),
        false,
        "prevalidated staged chunk replacement output plan should not expose materialized replacement");
    const auto* output_entry_plan =
        find_output_entry_plan(output_plan.entries, worksheet_part.zip_path());
    check(output_entry_plan != nullptr,
        "prevalidated staged chunk replacement output plan should include the worksheet entry");
    check(output_entry_plan->staged_replacement_chunk_count == 1,
        "prevalidated staged chunk replacement output plan should expose chunk count");
    check(output_entry_plan->staged_replacement_file_chunk_count == 1,
        "prevalidated staged chunk replacement output plan should expose file chunk count");
    check(output_entry_plan->staged_replacement_file_range_chunk_count == 1,
        "prevalidated staged chunk replacement output plan should expose file-range chunk count");
    check(output_entry_plan->staged_replacement_memory_chunk_count == 0,
        "prevalidated staged chunk replacement output plan should not report memory chunks");
    check(output_entry_plan->staged_replacement_expected_bytes
            == static_cast<std::uint64_t>(replacement_worksheet.size()),
        "prevalidated staged chunk replacement output plan should expose expected bytes");
    check(output_entry_plan->staged_replacement_file_range_bytes
            == static_cast<std::uint64_t>(replacement_worksheet.size()),
        "prevalidated staged chunk replacement output plan should expose file-range bytes");
    check(output_entry_plan->staged_replacement_expected_bytes_complete,
        "prevalidated staged chunk replacement output plan should have complete expected bytes");
    check_contains(output_entry_plan->reason, "prevalidated staged stream rewrite",
        "prevalidated staged chunk replacement output plan should expose the fast-path reason");
    check(editor.edit_plan().worksheet_payload_dependency_audits().empty(),
        "prevalidated staged chunk replacement should skip payload audit collection");
    check(editor.edit_plan().worksheet_relationship_reference_audits().empty(),
        "prevalidated staged chunk replacement should skip relationship audit collection");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), replacement_worksheet);
    check_preserved_source_entries(editor.reader(), output_reader, worksheet_part.zip_path());
}

void test_package_editor_staged_chunk_range_reader_slices_memory_and_file_chunks()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-staged-range-body.bin");
    const std::string prefix = "alpha:";
    const std::string body = "FILE-RANGE-BODY";
    const std::string suffix = ":omega";
    const std::string expected = prefix + body + suffix;
    write_binary_file(body_path, body);

    const std::vector<fastxlsx::detail::PackageEntryChunk> chunks {
        fastxlsx::detail::PackageEntryChunk::memory(prefix),
        fastxlsx::detail::PackageEntryChunk::file(body_path),
        fastxlsx::detail::PackageEntryChunk::memory(suffix),
    };
    const auto total_size = static_cast<std::uint64_t>(expected.size());

    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              chunks, 0, total_size)
            == expected,
        "staged chunk range reader should emit the full memory/file composition");
    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              chunks, 0, 0)
            .empty(),
        "staged chunk range reader should accept an empty range at the beginning");
    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              chunks, total_size, 0)
            .empty(),
        "staged chunk range reader should accept an empty range at the end");

    const auto file_only_offset =
        static_cast<std::uint64_t>(prefix.size() + 5);
    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              chunks, file_only_offset, 5)
            == "RANGE",
        "staged chunk range reader should slice inside a file-backed chunk");

    const auto spanning_offset =
        static_cast<std::uint64_t>(prefix.size() - 2);
    const auto spanning_size =
        static_cast<std::uint64_t>(body.size() + 6);
    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              chunks, spanning_offset, spanning_size)
            == expected.substr(static_cast<std::size_t>(spanning_offset),
                static_cast<std::size_t>(spanning_size)),
        "staged chunk range reader should slice across memory/file/memory chunks");
}

void test_package_editor_file_range_chunks_read_and_write_ranges()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-file-range-body.bin");
    const std::string body = "0123456789abcdef";
    write_binary_file(body_path, body);

    const auto make_chunks = [&] {
        return std::vector<fastxlsx::detail::PackageEntryChunk> {
            fastxlsx::detail::PackageEntryChunk::memory("pre-"),
            fastxlsx::detail::PackageEntryChunk::file_range(body_path, 2, 5),
            fastxlsx::detail::PackageEntryChunk::memory("-post"),
        };
    };
    const auto make_gapped_chunks = [&] {
        return std::vector<fastxlsx::detail::PackageEntryChunk> {
            fastxlsx::detail::PackageEntryChunk::file_range(body_path, 0, 2),
            fastxlsx::detail::PackageEntryChunk::memory("+"),
            fastxlsx::detail::PackageEntryChunk::file_range(body_path, 4, 2),
            fastxlsx::detail::PackageEntryChunk::memory("+"),
            fastxlsx::detail::PackageEntryChunk::file_range(body_path, 8, 2),
        };
    };
    const std::string expected = "pre-23456-post";
    const std::string expected_gapped = "01+45+89";

    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(make_chunks())
            == expected,
        "file-range chunks should replay only the requested file slice");
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(make_gapped_chunks())
            == expected_gapped,
        "file-range chunks should omit skipped source gaps");
    check(fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
              make_chunks(), 2, 9)
            == "e-23456-p",
        "file-range chunks should support second-level range slicing");

    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-file-range-chunks-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-file-range-chunks-output.xlsx");
    const std::filesystem::path gapped_output =
        output_path("fastxlsx-package-editor-file-range-chunks-gapped-output.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_part_chunks(
        opaque_part, make_chunks(), "file-range staged opaque chunks");
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, opaque_part.zip_path(), expected);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);

    fastxlsx::detail::PackageEditor gapped_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    gapped_editor.replace_part_chunks(
        opaque_part, make_gapped_chunks(), "gapped file-range staged opaque chunks");
    gapped_editor.save_as(gapped_output);

    const fastxlsx::detail::PackageReader gapped_output_reader =
        fastxlsx::detail::PackageReader::open(gapped_output);
    check_entry_bytes(gapped_output_reader, opaque_part.zip_path(), expected_gapped);
    check_entry_bytes(gapped_output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
}

void test_package_editor_rejects_invalid_staged_chunk_ranges()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-invalid-staged-range-body.bin");
    const std::filesystem::path missing_path =
        output_path("fastxlsx-package-editor-invalid-staged-range-missing.bin");
    const std::string body = "file";
    write_binary_file(body_path, body);
    std::error_code ignored;
    std::filesystem::remove(missing_path, ignored);

    const std::vector<fastxlsx::detail::PackageEntryChunk> chunks {
        fastxlsx::detail::PackageEntryChunk::memory("pre"),
        fastxlsx::detail::PackageEntryChunk::file(body_path),
        fastxlsx::detail::PackageEntryChunk::memory("post"),
    };
    const auto total_size = static_cast<std::uint64_t>(
        std::string("pre").size() + body.size() + std::string("post").size());

    bool failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            chunks, total_size - 1, 2);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "range exceeds staged payload size",
            "out-of-range staged chunk slice should report range overflow");
        check_contains(error.what(), "offset 10",
            "out-of-range staged chunk slice should report requested offset");
        check_contains(error.what(), "length 2",
            "out-of-range staged chunk slice should report requested length");
        check_contains(error.what(), "total 11",
            "out-of-range staged chunk slice should report total staged size");
    }
    check(failed, "staged chunk range reader should reject ranges beyond total size");

    fastxlsx::detail::PackageEntryChunk expected_size_mismatch =
        fastxlsx::detail::PackageEntryChunk::file(body_path);
    expected_size_mismatch.has_expected_size = true;
    expected_size_mismatch.expected_size = static_cast<std::uint64_t>(body.size() + 1);
    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            {expected_size_mismatch}, 0, 1);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "staged package-entry chunk 0",
            "expected-size mismatch should identify the failing chunk");
        check_contains(error.what(), "size changed after validation",
            "expected-size mismatch should preserve validation detail");
    }
    check(failed,
        "staged chunk range reader should reject a changed expected file size");

    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            {fastxlsx::detail::PackageEntryChunk::file(missing_path)}, 0, 1);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "failed to measure staged package-entry chunk file",
            "missing file chunk should fail during range layout validation");
    }
    check(failed, "staged chunk range reader should reject missing file chunks");

    fastxlsx::detail::PackageEntryChunk invalid_file_range =
        fastxlsx::detail::PackageEntryChunk::file_range(
            body_path, 2, static_cast<std::uint64_t>(body.size()));
    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            {invalid_file_range}, 0, 1);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "file range exceeds file size",
            "invalid file-range chunk should report the file-size boundary");
        check_contains(error.what(), "range offset 2",
            "invalid file-range chunk should report the range offset");
    }
    check(failed, "staged chunk range reader should reject file ranges beyond file size");

    fastxlsx::detail::PackageEntryChunk mixed =
        fastxlsx::detail::PackageEntryChunk::memory("mixed");
    mixed.path = body_path;
    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            {mixed}, 0, 1);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "cannot mix memory and file sources",
            "mixed memory/file chunk should fail during range layout validation");
    }
    check(failed, "staged chunk range reader should reject mixed-source chunks");

    fastxlsx::detail::PackageEntryChunk unsupported =
        fastxlsx::detail::PackageEntryChunk::memory("unsupported");
    unsupported.kind = static_cast<fastxlsx::detail::PackageEntryChunk::Kind>(99);
    failed = false;
    try {
        (void)fastxlsx::detail::testing_read_package_entry_chunk_range_to_string(
            {unsupported}, 0, 1);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "unsupported staged package-entry chunk kind",
            "unsupported chunk kind should fail during range layout validation");
    }
    check(failed, "staged chunk range reader should reject unsupported chunk kinds");
}

void test_package_editor_indexed_staged_chunk_replacement_slices_source_chunks()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-indexed-staged-replace-body.xml");
    const std::string prefix =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c>)";
    const std::string body =
        R"(<c r="B1"><v>2</v></c></row><row r="2"><c r="A2"><v>3</v></c>)";
    const std::string suffix = R"(</row></sheetData></worksheet>)";
    write_binary_file(body_path, body);

    const std::string source_xml = prefix + body + suffix;
    const std::vector<fastxlsx::detail::PackageEntryChunk> source_chunks {
        fastxlsx::detail::PackageEntryChunk::memory(prefix),
        fastxlsx::detail::PackageEntryChunk::file(body_path),
        fastxlsx::detail::PackageEntryChunk::memory(suffix),
    };

    const fastxlsx::detail::WorksheetCellIndex index =
        fastxlsx::detail::WorksheetCellIndex::build_from_xml(source_xml);

    const std::string a1_replacement_head = R"(<c r="A1"><v>)";
    const std::string a1_replacement_tail = R"(101</v></c>)";
    const std::array<std::string_view, 2> a1_replacement_chunks {
        a1_replacement_head,
        a1_replacement_tail,
    };
    const std::string a2_replacement_xml = R"(<c r="A2"><v>202</v></c>)";

    std::vector<fastxlsx::detail::WorksheetCellReplacement> replacements;
    replacements.push_back(fastxlsx::detail::WorksheetCellReplacement {
        "A2",
        fastxlsx::detail::WorksheetCellReplacementPayload::from_materialized_xml(
            a2_replacement_xml),
    });
    replacements.push_back(fastxlsx::detail::WorksheetCellReplacement {
        "A1",
        fastxlsx::detail::WorksheetCellReplacementPayload::from_chunks(
            a1_replacement_chunks),
    });
    const fastxlsx::detail::WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);

    const fastxlsx::detail::IndexedChunkReplacementTestResult result =
        fastxlsx::detail::
            testing_emit_indexed_cell_replacement_package_entry_chunks_to_string(
                source_chunks, index, replacement_plan);

    const std::string expected =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>101</v></c>)"
        R"(<c r="B1"><v>2</v></c></row><row r="2"><c r="A2"><v>202</v></c>)"
        R"(</row></sheetData></worksheet>)";
    check(result.xml == expected,
        "indexed staged chunk replacement should splice source chunks and replay payload chunks");
    check(result.summary.matched_replacement_count == 2,
        "indexed staged chunk replacement should report matched replacements");
    check(result.summary.inserted_cell_count == 0,
        "indexed staged chunk replacement should not report inserts");
    check(result.summary.missing_cell_references.empty(),
        "indexed staged chunk replacement should not report missing cells");

    const std::array<std::string_view, 2> requested_rewrites {"A2", "A1"};
    const std::vector<fastxlsx::detail::WorksheetIndexedCellRewrite> preplanned_rewrites =
        fastxlsx::detail::plan_indexed_cell_rewrites(index, requested_rewrites);
    std::string preplanned_output;
    const fastxlsx::detail::WorksheetTransformSummary preplanned_summary =
        fastxlsx::detail::emit_indexed_cell_replacement_from_package_entry_chunks(
            source_chunks,
            preplanned_rewrites,
            replacement_plan,
            [&](std::string_view chunk) {
                preplanned_output += chunk;
            });
    check(preplanned_output == expected,
        "preplanned indexed staged chunk replacement should splice source chunks");
    check(preplanned_summary.matched_replacement_count == 2,
        "preplanned indexed staged chunk replacement should report matched replacements");

    const fastxlsx::detail::IndexedPackageEntryChunkReplacementResult descriptor_result =
        fastxlsx::detail::make_indexed_cell_replacement_package_entry_chunks(
            source_chunks, preplanned_rewrites, replacement_plan);
    check(descriptor_result.output_bytes == static_cast<std::uint64_t>(expected.size()),
        "indexed descriptor replacement should report output bytes");
    check(descriptor_result.summary.matched_replacement_count == 2,
        "indexed descriptor replacement should report matched replacements");
    check(fastxlsx::detail::testing_read_package_entry_chunks_to_string(
              descriptor_result.chunks)
            == expected,
        "indexed descriptor replacement should replay source file ranges and payload chunks");
    const bool has_file_range_chunk =
        std::any_of(descriptor_result.chunks.begin(), descriptor_result.chunks.end(),
            [](const fastxlsx::detail::PackageEntryChunk& chunk) {
                return chunk.kind == fastxlsx::detail::PackageEntryChunk::Kind::File
                    && chunk.has_file_range;
            });
    check(has_file_range_chunk,
        "indexed descriptor replacement should preserve source pass-through as file ranges");
    const bool file_range_has_crc_contract =
        std::any_of(descriptor_result.chunks.begin(), descriptor_result.chunks.end(),
            [](const fastxlsx::detail::PackageEntryChunk& chunk) {
                return chunk.kind == fastxlsx::detail::PackageEntryChunk::Kind::File
                    && chunk.has_file_range && chunk.has_expected_crc32;
            });
    check(!file_range_has_crc_contract,
        "indexed descriptor replacement should leave source file ranges without CRC prepass");
}

void test_package_editor_cell_replacement_uses_indexed_source_entry_fast_path()
{
    const SourcePackage source = write_source_package(
        "fastxlsx-package-editor-cell-replace-indexed-source-entry-source.xlsx");
    const std::filesystem::path output = output_path(
        "fastxlsx-package-editor-cell-replace-indexed-source-entry-output.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const std::string replacement_cell = R"(<c r="A1"><v>42</v></c>)";
    const std::string expected_worksheet =
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>42</v></c></row></sheetData></worksheet>)";
    rewrite_package_entry_as_stored(source.path, worksheet_part.zip_path(),
        R"(<worksheet><dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");

    fastxlsx::detail::ReferencePolicy policy;
    policy.request_full_calculation_on_sheet_rewrite = false;
    policy.calc_chain_action = fastxlsx::detail::CalcChainAction::Preserve;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::array replacements {
        worksheet_cell_replacement("A1", replacement_cell),
    };
    editor.replace_worksheet_cells(worksheet_part, replacements, policy);

    check(has_note_containing(editor.edit_plan().notes(),
              {"indexed source-entry direct-range", "matched 1 replacement targets"}),
        "cell replacement should record the indexed source-entry fast path");
    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "indexed source-entry cell replacement should plan the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "indexed source-entry cell replacement should use stream rewrite");
    check_contains(worksheet_plan->reason, "indexed direct-range",
        "indexed source-entry cell replacement should expose the fast-path reason");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "indexed source-entry cell replacement should update manifest write mode");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, worksheet_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "indexed source-entry cell replacement should appear as a staged stream rewrite");
    check_output_entry_staged_replacement_chunks(output_plan.entries, worksheet_part.zip_path(),
        true,
        "indexed source-entry cell replacement should expose staged chunks");
    check_output_entry_materialized_replacement(output_plan.entries, worksheet_part.zip_path(),
        false,
        "indexed source-entry cell replacement should not materialize the worksheet XML");
    const auto* output_entry_plan =
        find_output_entry_plan(output_plan.entries, worksheet_part.zip_path());
    check(output_entry_plan != nullptr,
        "indexed source-entry output plan should include the worksheet entry");
    check(output_entry_plan->staged_replacement_chunk_count
            == output_entry_plan->staged_replacement_file_range_chunk_count
                + output_entry_plan->staged_replacement_memory_chunk_count,
        "indexed source-entry output plan should account for every staged chunk");
    check(output_entry_plan->staged_replacement_file_range_chunk_count > 0,
        "indexed source-entry output plan should preserve source XML as file ranges");
    check(output_entry_plan->staged_replacement_memory_chunk_count == 1,
        "indexed source-entry output plan should stage only the replacement cell as memory");
    check(output_entry_plan->staged_replacement_file_range_bytes > 0,
        "indexed source-entry output plan should report source file-range bytes");
    check(output_entry_plan->staged_replacement_memory_bytes
            == static_cast<std::uint64_t>(replacement_cell.size()),
        "indexed source-entry output plan should report replacement memory bytes");
    check(output_entry_plan->staged_replacement_expected_bytes
            == static_cast<std::uint64_t>(expected_worksheet.size()),
        "indexed source-entry output plan should report final worksheet bytes");
    check(output_entry_plan->staged_replacement_expected_bytes_complete,
        "indexed source-entry output plan should have complete expected bytes");
    check(output_entry_plan->indexed_source_entry_direct_range,
        "indexed source-entry output plan should expose structured direct-range telemetry");
    check(output_entry_plan->indexed_source_entry_scanned_source_cell_count == 1,
        "indexed source-entry output plan should report scanned source cells");
    check(output_entry_plan->indexed_source_entry_matched_replacement_count == 1,
        "indexed source-entry output plan should report matched replacements");
    check(output_entry_plan->indexed_source_entry_staged_output_bytes
            == static_cast<std::uint64_t>(expected_worksheet.size()),
        "indexed source-entry output plan should report staged output bytes");
    check_contains(output_entry_plan->reason, "indexed direct-range",
        "indexed source-entry output plan should expose the fast-path reason");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, worksheet_part.zip_path(), expected_worksheet);
    check_preserved_source_entries(editor.reader(), output_reader, worksheet_part.zip_path());
}

void test_package_editor_indexed_staged_chunk_replacement_rejects_invalid_inputs()
{
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-indexed-staged-replace-invalid-body.xml");
    const std::string source_xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    write_binary_file(body_path, source_xml);

    const std::vector<fastxlsx::detail::PackageEntryChunk> source_chunks {
        fastxlsx::detail::PackageEntryChunk::file(body_path),
    };
    const fastxlsx::detail::WorksheetCellIndex index =
        fastxlsx::detail::WorksheetCellIndex::build_from_xml(source_xml);
    const std::string replacement_xml = R"(<c r="A1"><v>11</v></c>)";
    const std::vector<fastxlsx::detail::WorksheetCellReplacement> replacements {
        fastxlsx::detail::WorksheetCellReplacement {
            "A1",
            fastxlsx::detail::WorksheetCellReplacementPayload::from_materialized_xml(
                replacement_xml),
        },
    };
    const fastxlsx::detail::WorksheetCellReplacementPlan replacement_plan =
        fastxlsx::detail::make_worksheet_cell_replacement_plan(replacements);

    bool failed = false;
    try {
        const std::string missing_replacement_xml = R"(<c r="C9"><v>99</v></c>)";
        const std::vector<fastxlsx::detail::WorksheetCellReplacement>
            missing_replacements {
                fastxlsx::detail::WorksheetCellReplacement {
                    "C9",
                    fastxlsx::detail::WorksheetCellReplacementPayload::
                        from_materialized_xml(missing_replacement_xml),
                },
            };
        const fastxlsx::detail::WorksheetCellReplacementPlan missing_plan =
            fastxlsx::detail::make_worksheet_cell_replacement_plan(
                missing_replacements);
        (void)fastxlsx::detail::
            testing_emit_indexed_cell_replacement_package_entry_chunks_to_string(
                source_chunks, index, missing_plan);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "target cell is missing from source index",
            "indexed staged chunk replacement should reject missing source cells");
    }
    check(failed,
        "indexed staged chunk replacement should reject a target absent from the index");

    fastxlsx::detail::WorksheetCellIndex stale_index;
    stale_index.add_cell("A1",
        fastxlsx::detail::WorksheetCellIndexedRange {
            0,
            static_cast<std::uint64_t>(source_xml.size() + 1),
        });
    failed = false;
    try {
        (void)fastxlsx::detail::
            testing_emit_indexed_cell_replacement_package_entry_chunks_to_string(
                source_chunks, stale_index, replacement_plan);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "source range exceeds staged chunks",
            "stale indexed range should report the staged chunk boundary");
    }
    check(failed,
        "indexed staged chunk replacement should reject stale source ranges");

    fastxlsx::detail::PackageEntryChunk changed_size_chunk =
        fastxlsx::detail::PackageEntryChunk::file(body_path);
    changed_size_chunk.has_expected_size = true;
    changed_size_chunk.expected_size =
        static_cast<std::uint64_t>(source_xml.size() + 1);
    failed = false;
    try {
        (void)fastxlsx::detail::
            testing_emit_indexed_cell_replacement_package_entry_chunks_to_string(
                {changed_size_chunk}, index, replacement_plan);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "size changed after validation",
            "indexed staged chunk replacement should preflight staged source sizes");
    }
    check(failed,
        "indexed staged chunk replacement should reject changed staged source size");
}

void test_package_editor_save_as_rejects_changed_staged_chunk_size_without_state_changes()
{
    struct ChunkMutationCase {
        std::string_view name;
        std::string_view mutated_body;
    };

    const std::array cases {
        ChunkMutationCase {"truncated", "file"},
        ChunkMutationCase {"extended", "file-backed-body-extended"},
    };

    for (const ChunkMutationCase& test_case : cases) {
        const SourcePackage source = write_source_package(
            "fastxlsx-package-editor-staged-size-" + std::string(test_case.name)
            + "-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-staged-size-"
                + std::string(test_case.name) + "-output.xlsx");
        const std::filesystem::path body_path =
            output_path("fastxlsx-package-editor-staged-size-"
                + std::string(test_case.name) + "-body.bin");
        const std::string output_sentinel =
            "do not overwrite staged size failure output";
        write_binary_file(output, output_sentinel);

        const std::string opaque_prefix = "chunked:";
        const std::string opaque_body = "file-backed-body";
        const std::string opaque_suffix = ":done";
        const std::string expected_chunked_opaque =
            opaque_prefix + opaque_body + opaque_suffix;
        write_binary_file(body_path, opaque_body);

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);
        const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
        editor.replace_part_chunks(opaque_part,
            {
                fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
                fastxlsx::detail::PackageEntryChunk::file(body_path),
                fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
            },
            "staged opaque stream chunks with expected size");

        const std::size_t initial_plan_size = editor.edit_plan().size();
        const std::size_t initial_note_count = editor.edit_plan().notes().size();
        const bool initial_full_calculation =
            editor.edit_plan().full_calculation_on_load();
        const std::vector<std::filesystem::path> temp_files_before =
            package_editor_temp_files();
        const std::vector<std::filesystem::path> output_temp_files_before =
            package_editor_output_sibling_temp_files(output);

        write_binary_file(body_path, test_case.mutated_body);

        bool save_failed = false;
        try {
            editor.save_as(output);
        } catch (const std::exception& error) {
            save_failed = true;
            check_contains(error.what(),
                "ZIP entry chunk size changed after staging",
                "save_as staged chunk size mutation should report the size contract");
            check_contains(error.what(),
                std::string("expected ") + std::to_string(opaque_body.size()) + " bytes",
                "save_as staged chunk size mutation should report expected bytes");
            check_contains(error.what(),
                std::string("actual ") + std::to_string(test_case.mutated_body.size()) + " bytes",
                "save_as staged chunk size mutation should report actual bytes");
            check_contains(error.what(), "ZIP entry 'custom/opaque.bin' chunk 1",
                "save_as staged chunk size mutation should report entry/chunk context");
            check_contains(error.what(), body_path.filename().generic_string(),
                "save_as staged chunk size mutation should include the file-backed chunk path");
        }

        check(save_failed,
            "save_as should reject staged file chunks whose size changed after validation");
        check(fastxlsx::test::read_file(output) == output_sentinel,
            "save_as staged chunk size failure should preserve existing output bytes");
        check(editor.edit_plan().size() == initial_plan_size,
            "save_as staged chunk size failure should preserve edit-plan size");
        check(editor.edit_plan().notes().size() == initial_note_count,
            "save_as staged chunk size failure should not append notes");
        check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
            "save_as staged chunk size failure should preserve calc policy");
        check_manifest_write_mode(editor, opaque_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "save_as staged chunk size failure should keep prior staged part plan");
        check_no_new_package_editor_temp_files(temp_files_before,
            "save_as staged chunk size failure should clean source-copy temp files");
        check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
            "save_as staged chunk size failure should clean output sibling temp files");

        write_binary_file(body_path, opaque_body);
        editor.save_as(output);

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_entry_bytes(output_reader, opaque_part.zip_path(), expected_chunked_opaque);
        check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
    }
}

void test_package_editor_save_as_rejects_changed_staged_chunk_crc_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-crc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-staged-crc-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-staged-crc-body.bin");
    const std::string output_sentinel =
        "do not overwrite staged crc failure output";
    write_binary_file(output, output_sentinel);

    const std::string opaque_prefix = "chunked:";
    const std::string opaque_body = "file-backed-body";
    const std::string opaque_suffix = ":done";
    const std::string expected_chunked_opaque =
        opaque_prefix + opaque_body + opaque_suffix;
    write_binary_file(body_path, opaque_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(opaque_prefix),
            fastxlsx::detail::PackageEntryChunk::file(body_path),
            fastxlsx::detail::PackageEntryChunk::memory(opaque_suffix),
        },
        "staged opaque stream chunks with expected CRC");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();
    const bool initial_full_calculation =
        editor.edit_plan().full_calculation_on_load();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    const std::vector<std::filesystem::path> output_temp_files_before =
        package_editor_output_sibling_temp_files(output);

    write_binary_file(body_path, same_size_different_payload(opaque_body));

    bool save_failed = false;
    try {
        editor.save_as(output);
    } catch (const std::exception& error) {
        save_failed = true;
        check_contains(error.what(),
            "ZIP entry chunk CRC32 changed after staging",
            "save_as staged chunk CRC mutation should report the CRC contract");
        check_contains(error.what(), "expected ",
            "save_as staged chunk CRC mutation should report expected CRC");
        check_contains(error.what(), "actual ",
            "save_as staged chunk CRC mutation should report actual CRC");
        check_contains(error.what(), "ZIP entry 'custom/opaque.bin' chunk 1",
            "save_as staged chunk CRC mutation should report entry/chunk context");
        check_contains(error.what(), body_path.filename().generic_string(),
            "save_as staged chunk CRC mutation should include the file-backed chunk path");
    }

    check(save_failed,
        "save_as should reject staged file chunks whose CRC changed after validation");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "save_as staged chunk CRC failure should preserve existing output bytes");
    check(editor.edit_plan().size() == initial_plan_size,
        "save_as staged chunk CRC failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "save_as staged chunk CRC failure should not append notes");
    check(editor.edit_plan().full_calculation_on_load() == initial_full_calculation,
        "save_as staged chunk CRC failure should preserve calc policy");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "save_as staged chunk CRC failure should keep prior staged part plan");
    check_no_new_package_editor_temp_files(temp_files_before,
        "save_as staged chunk CRC failure should clean source-copy temp files");
    check_no_new_package_editor_output_sibling_temp_files(output_temp_files_before, output,
        "save_as staged chunk CRC failure should clean output sibling temp files");

    write_binary_file(body_path, opaque_body);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, opaque_part.zip_path(), expected_chunked_opaque);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
}

void test_package_editor_save_as_rejects_changed_single_staged_chunk_crc()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-single-staged-crc-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-single-staged-crc-output.xlsx");
    const std::filesystem::path body_path =
        output_path("fastxlsx-package-editor-single-staged-crc-body.bin");
    const std::string output_sentinel =
        "do not overwrite single staged crc failure output";
    write_binary_file(output, output_sentinel);

    const std::string opaque_body = "single-file-backed-body";
    write_binary_file(body_path, opaque_body);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    editor.replace_part_chunks(opaque_part,
        {
            fastxlsx::detail::PackageEntryChunk::file(body_path),
        },
        "single staged opaque stream chunk with expected CRC");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    const std::size_t initial_note_count = editor.edit_plan().notes().size();

    write_binary_file(body_path, same_size_different_payload(opaque_body));

    bool save_failed = false;
    try {
        editor.save_as(output);
    } catch (const std::exception& error) {
        save_failed = true;
        check_contains(error.what(),
            "ZIP entry chunk CRC32 changed after staging",
            "single staged chunk CRC mutation should still be validated while writing");
        check_contains(error.what(), "ZIP entry 'custom/opaque.bin' chunk 0",
            "single staged chunk CRC mutation should report chunk context");
        check_contains(error.what(), body_path.filename().generic_string(),
            "single staged chunk CRC mutation should include the file-backed chunk path");
    }

    check(save_failed,
        "save_as should reject a single staged file chunk whose CRC changed after validation");
    check(fastxlsx::test::read_file(output) == output_sentinel,
        "single staged chunk CRC failure should preserve existing output bytes");
    check(editor.edit_plan().size() == initial_plan_size,
        "single staged chunk CRC failure should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "single staged chunk CRC failure should not append notes");

    write_binary_file(body_path, opaque_body);
    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_entry_bytes(output_reader, opaque_part.zip_path(), opaque_body);
    check_entry_bytes(output_reader, "xl/worksheets/sheet1.xml", source.worksheet);
}

void test_package_editor_rejects_invalid_generic_staged_chunks_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-generic-staged-chunks-source.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const auto expect_invalid_chunk =
        [&](fastxlsx::detail::PackageEntryChunk chunk,
            std::string_view expected_error_fragment,
            std::string_view output_name,
            const char* scenario) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();

            bool failed = false;
            try {
                editor.replace_part_chunks(opaque_part, {std::move(chunk)},
                    std::string(scenario));
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(),
                    "generic package part staged replacement has invalid staged chunks",
                    "invalid generic staged chunk should fail before commit");
                check_contains(error.what(), "staged package-entry chunk 0",
                    "invalid generic staged chunk should identify the failing chunk");
                check_contains(error.what(), expected_error_fragment,
                    "invalid generic staged chunk should preserve chunk validation detail");
            }
            check(failed, "PackageEditor should reject invalid generic staged chunks");
            check(editor.edit_plan().size() == initial_plan_size,
                "invalid generic staged chunk failure should not mutate edit-plan parts");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "invalid generic staged chunk failure should not add notes");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "invalid generic staged chunk failure should not add package-entry audits");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "invalid generic staged chunk failure should not add removed package-entry audits");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid generic staged chunk failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid generic staged chunk failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid generic staged chunk failure should not change calcChain policy");

            check_manifest_write_mode(editor, opaque_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep opaque copy-original");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid generic staged chunk failure should keep worksheet copy-original");

            const fastxlsx::detail::PackageEditorOutputPlan failed_plan =
                editor.planned_output();
            check_output_entry_plan(failed_plan.entries, opaque_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid generic staged chunk failure should preserve opaque source entry");
            check_output_entry_staged_replacement_chunks(failed_plan.entries,
                opaque_part.zip_path(), false,
                "invalid generic staged chunk failure should not stage opaque chunks");
            check_output_entry_materialized_replacement(failed_plan.entries,
                opaque_part.zip_path(), false,
                "invalid generic staged chunk failure should not mark materialized replacement");

            const std::filesystem::path output = output_path(std::string(output_name));
            editor.save_as(output);
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output);
            check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
            check_entry_bytes(output_reader, worksheet_part.zip_path(), source.worksheet);
        };

    fastxlsx::detail::PackageEntryChunk mixed_chunk =
        fastxlsx::detail::PackageEntryChunk::memory("invalid mixed payload");
    mixed_chunk.path =
        output_path("fastxlsx-package-editor-invalid-generic-staged-chunks-mixed.bin");
    expect_invalid_chunk(std::move(mixed_chunk), "cannot mix memory and file sources",
        "fastxlsx-package-editor-invalid-generic-staged-chunks-mixed-output.xlsx",
        "mixed memory/file generic staged chunk");

    fastxlsx::detail::PackageEntryChunk missing_file_chunk =
        fastxlsx::detail::PackageEntryChunk::file(
            output_path("fastxlsx-package-editor-invalid-generic-staged-chunks-missing.bin"));
    expect_invalid_chunk(std::move(missing_file_chunk),
        "failed to measure staged package-entry chunk file",
        "fastxlsx-package-editor-invalid-generic-staged-chunks-missing-output.xlsx",
        "missing file generic staged chunk");

    const std::filesystem::path range_body_path =
        output_path("fastxlsx-package-editor-invalid-generic-staged-chunks-range.bin");
    write_binary_file(range_body_path, "range");
    fastxlsx::detail::PackageEntryChunk invalid_range_chunk =
        fastxlsx::detail::PackageEntryChunk::file_range(range_body_path, 3, 8);
    expect_invalid_chunk(std::move(invalid_range_chunk),
        "file range exceeds file size",
        "fastxlsx-package-editor-invalid-generic-staged-chunks-range-output.xlsx",
        "invalid file-range generic staged chunk");
}

void test_package_editor_rejects_invalid_worksheet_staged_chunks_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-invalid-worksheet-staged-chunks-source.xlsx");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const auto expect_invalid_chunk =
        [&](fastxlsx::detail::PackageEntryChunk chunk,
            std::string_view expected_error_fragment,
            std::string_view output_name,
            const char* scenario) {
            fastxlsx::detail::PackageEditor editor =
                fastxlsx::detail::PackageEditor::open(source.path);
            const std::size_t initial_plan_size = editor.edit_plan().size();
            const std::size_t initial_note_count = editor.edit_plan().notes().size();
            const std::size_t initial_package_entry_count =
                editor.edit_plan().package_entries().size();
            const std::size_t initial_removed_package_entry_count =
                editor.edit_plan().removed_package_entries().size();
            const std::size_t initial_relationship_target_audit_count =
                editor.edit_plan().relationship_target_audits().size();
            const std::size_t initial_worksheet_relationship_audit_count =
                editor.edit_plan().worksheet_relationship_reference_audits().size();
            const std::size_t initial_worksheet_payload_audit_count =
                editor.edit_plan().worksheet_payload_dependency_audits().size();

            bool failed = false;
            try {
                editor.replace_worksheet_part_chunks(worksheet_part, {std::move(chunk)},
                    {}, std::string(scenario));
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(),
                    "worksheet staged replacement has invalid staged chunks",
                    "invalid worksheet staged chunk should fail before worksheet scanning");
                check_contains(error.what(), "staged package-entry chunk 0",
                    "invalid worksheet staged chunk should identify the failing chunk");
                check_contains(error.what(), expected_error_fragment,
                    "invalid worksheet staged chunk should preserve chunk validation detail");
            }
            check(failed, "PackageEditor should reject invalid worksheet staged chunks");
            check(editor.edit_plan().size() == initial_plan_size,
                "invalid worksheet staged chunk failure should not mutate edit-plan parts");
            check(editor.edit_plan().notes().size() == initial_note_count,
                "invalid worksheet staged chunk failure should not add notes");
            check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
                "invalid worksheet staged chunk failure should not add package-entry audits");
            check(editor.edit_plan().removed_package_entries().size()
                    == initial_removed_package_entry_count,
                "invalid worksheet staged chunk failure should not add removed package-entry audits");
            check(editor.edit_plan().relationship_target_audits().size()
                    == initial_relationship_target_audit_count,
                "invalid worksheet staged chunk failure should not add relationship target audits");
            check(editor.edit_plan().worksheet_relationship_reference_audits().size()
                    == initial_worksheet_relationship_audit_count,
                "invalid worksheet staged chunk failure should not add worksheet relationship audits");
            check(editor.edit_plan().worksheet_payload_dependency_audits().size()
                    == initial_worksheet_payload_audit_count,
                "invalid worksheet staged chunk failure should not add worksheet payload audits");
            check(editor.edit_plan().removed_parts().empty(),
                "invalid worksheet staged chunk failure should not record removed parts");
            check(!editor.edit_plan().full_calculation_on_load(),
                "invalid worksheet staged chunk failure should not request recalculation");
            check(editor.edit_plan().calc_chain_action()
                    == fastxlsx::detail::CalcChainAction::Preserve,
                "invalid worksheet staged chunk failure should not change calcChain policy");

            check_manifest_write_mode(editor, opaque_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep opaque copy-original");
            check_manifest_write_mode(editor, workbook_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep workbook copy-original");
            check_manifest_write_mode(editor, worksheet_part,
                fastxlsx::detail::PartWriteMode::CopyOriginal,
                "invalid worksheet staged chunk failure should keep worksheet copy-original");

            const fastxlsx::detail::PackageEditorOutputPlan failed_plan =
                editor.planned_output();
            check_output_entry_plan(failed_plan.entries, worksheet_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid worksheet staged chunk failure should preserve worksheet source entry");
            check_output_entry_plan(failed_plan.entries, workbook_part.zip_path(),
                fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
                "invalid worksheet staged chunk failure should preserve workbook source entry");
            check_output_entry_staged_replacement_chunks(failed_plan.entries,
                worksheet_part.zip_path(), false,
                "invalid worksheet staged chunk failure should not stage worksheet chunks");
            check_output_entry_materialized_replacement(failed_plan.entries,
                worksheet_part.zip_path(), false,
                "invalid worksheet staged chunk failure should not mark materialized worksheet replacement");
            check(failed_plan.worksheet_relationship_reference_audits.empty(),
                "invalid worksheet staged chunk output plan should not add worksheet relationship audits");
            check(failed_plan.worksheet_payload_dependency_audits.empty(),
                "invalid worksheet staged chunk output plan should not add worksheet payload audits");

            const std::filesystem::path output = output_path(std::string(output_name));
            editor.save_as(output);
            const fastxlsx::detail::PackageReader output_reader =
                fastxlsx::detail::PackageReader::open(output);
            check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
            check_entry_bytes(output_reader, workbook_part.zip_path(), source.workbook);
            check_entry_bytes(output_reader, worksheet_part.zip_path(), source.worksheet);
        };

    fastxlsx::detail::PackageEntryChunk mixed_chunk =
        fastxlsx::detail::PackageEntryChunk::memory("<worksheet/>");
    mixed_chunk.path =
        output_path("fastxlsx-package-editor-invalid-worksheet-staged-chunks-mixed.bin");
    expect_invalid_chunk(std::move(mixed_chunk), "cannot mix memory and file sources",
        "fastxlsx-package-editor-invalid-worksheet-staged-chunks-mixed-output.xlsx",
        "mixed memory/file worksheet staged chunk");

    const std::filesystem::path missing_file_path =
        output_path("fastxlsx-package-editor-invalid-worksheet-staged-chunks-missing.xml");
    std::error_code ignored;
    std::filesystem::remove(missing_file_path, ignored);
    fastxlsx::detail::PackageEntryChunk missing_file_chunk =
        fastxlsx::detail::PackageEntryChunk::file(missing_file_path);
    expect_invalid_chunk(std::move(missing_file_chunk),
        "failed to measure staged package-entry chunk file",
        "fastxlsx-package-editor-invalid-worksheet-staged-chunks-missing-output.xlsx",
        "missing file worksheet staged chunk");
}

void test_package_editor_rejects_materialized_stream_rewrite_part_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-materialized-stream-rewrite-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-materialized-stream-rewrite-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(opaque_part, "materialized bytes",
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "materialized stream rewrite should fail");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(),
            "materialized replace_part cannot use stream-rewrite write mode",
            "materialized stream-rewrite replacement should report the staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "materialized stream-rewrite replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject materialized replace_part StreamRewrite");
    check(editor.edit_plan().size() == initial_plan_size,
        "materialized StreamRewrite failure should not mutate the edit plan");
    check_manifest_write_mode(editor, opaque_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "materialized StreamRewrite failure should leave target copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, opaque_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "materialized StreamRewrite failure should keep output copy-original");
    check_output_entry_staged_replacement_chunks(failed_plan.entries, opaque_part.zip_path(),
        false,
        "materialized StreamRewrite failure should not queue staged chunks");
    check_output_entry_materialized_replacement(failed_plan.entries, opaque_part.zip_path(),
        false,
        "materialized StreamRewrite failure should not queue materialized replacement bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader);
    check_entry_bytes(output_reader, opaque_part.zip_path(), source.unknown);
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "core")) {
            test_package_editor_noop_save_preserves_all_source_entries();
            test_package_editor_file_backs_copy_original_package_part_source_entries();
            test_package_editor_replaces_one_part_and_preserves_unknown_parts();
            test_package_editor_staged_chunk_part_replacement_writes_chunks();
            test_package_editor_source_part_stored_entry_chunks_reference_source_package_payload();
            test_package_editor_prevalidated_staged_chunk_part_replacement_by_name();
            test_package_editor_staged_chunk_range_reader_slices_memory_and_file_chunks();
            test_package_editor_file_range_chunks_read_and_write_ranges();
            test_package_editor_rejects_invalid_staged_chunk_ranges();
            test_package_editor_indexed_staged_chunk_replacement_slices_source_chunks();
            test_package_editor_cell_replacement_uses_indexed_source_entry_fast_path();
            test_package_editor_indexed_staged_chunk_replacement_rejects_invalid_inputs();
            test_package_editor_save_as_rejects_changed_staged_chunk_size_without_state_changes();
            test_package_editor_save_as_rejects_changed_staged_chunk_crc_without_state_changes();
            test_package_editor_save_as_rejects_changed_single_staged_chunk_crc();
            test_package_editor_rejects_invalid_generic_staged_chunks_without_state_changes();
            test_package_editor_rejects_invalid_worksheet_staged_chunks_without_state_changes();
            test_package_editor_rejects_materialized_stream_rewrite_part_without_state_changes();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
