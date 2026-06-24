#include "test_package_editor_sheetdata_catalog_common.hpp"

void test_package_editor_rejects_staged_small_xml_replacements_without_state_changes()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-staged-small-xml-source.xlsx");
    const std::string app_properties =
        "<Properties><Application>FastXLSX</Application></Properties>";
    source.content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>)"
        R"(<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>)"
        R"(</Relationships>)";
    fastxlsx::detail::write_package(source.path,
        {
            {"[Content_Types].xml", source.content_types},
            {"_rels/.rels", source.package_relationships},
            {"xl/workbook.xml", source.workbook},
            {"xl/_rels/workbook.xml.rels", source.workbook_relationships},
            {"docProps/core.xml", source.core_properties},
            {"docProps/app.xml", app_properties},
            {"xl/worksheets/sheet1.xml", source.worksheet},
            {"custom/opaque.bin", source.unknown},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-staged-small-xml-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

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
    const std::size_t initial_removed_package_entry_count =
        editor.edit_plan().removed_package_entries().size();

    const std::string workbook_prefix =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets>)";
    const std::string workbook_suffix =
        R"(<sheet name="Renamed" sheetId="1" r:id="rId1"/></sheets></workbook>)";

    const auto expect_staged_small_xml_rejection =
        [&](const fastxlsx::detail::PartName& part_name,
            std::vector<fastxlsx::detail::PackageEntryChunk> chunks,
            const char* expected_failure_message) {
            bool failed = false;
            try {
                editor.replace_part_chunks(
                    part_name, std::move(chunks), "staged small-XML chunks");
            } catch (const std::exception& error) {
                failed = true;
                check_contains(error.what(), "staged replacement is not allowed",
                    "staged small-XML replacement failure should explain the rejected staged path");
                check_contains(error.what(), "workbook/core/app small XML",
                    "staged small-XML replacement failure should identify the metadata boundary");
                check_contains(error.what(), "replace_part()",
                    "staged small-XML replacement failure should point to materialized small-XML API");
            }
            check(failed, expected_failure_message);
        };

    expect_staged_small_xml_rejection(workbook_part,
        {
            fastxlsx::detail::PackageEntryChunk::memory(workbook_prefix),
            fastxlsx::detail::PackageEntryChunk::memory(workbook_suffix),
        },
        "PackageEditor should reject staged replacement for workbook small XML");
    expect_staged_small_xml_rejection(core_part,
        {fastxlsx::detail::PackageEntryChunk::memory(
            "<cp:coreProperties><dc:creator>Staged</dc:creator></cp:coreProperties>")},
        "PackageEditor should reject staged replacement for core properties small XML");
    expect_staged_small_xml_rejection(app_part,
        {fastxlsx::detail::PackageEntryChunk::memory(
            "<Properties><Application>Staged</Application></Properties>")},
        "PackageEditor should reject staged replacement for app properties small XML");

    check(editor.edit_plan().size() == initial_plan_size,
        "staged small-XML rejection should preserve edit-plan size");
    check(editor.edit_plan().notes().size() == initial_note_count,
        "staged small-XML rejection should not append notes");
    check(editor.edit_plan().relationship_target_audits().size()
            == initial_relationship_target_audit_count,
        "staged small-XML rejection should not append relationship target audits");
    check(editor.edit_plan().worksheet_relationship_reference_audits().size()
            == initial_worksheet_relationship_reference_audit_count,
        "staged small-XML rejection should not append worksheet relationship audits");
    check(editor.edit_plan().worksheet_payload_dependency_audits().size()
            == initial_worksheet_payload_dependency_audit_count,
        "staged small-XML rejection should not append worksheet payload audits");
    check(editor.edit_plan().workbook_payload_dependency_audits().size()
            == initial_workbook_payload_dependency_audit_count,
        "staged small-XML rejection should not append workbook payload audits");
    check(editor.edit_plan().package_entries().size() == initial_package_entry_count,
        "staged small-XML rejection should preserve package-entry audit count");
    check(editor.edit_plan().removed_package_entries().size()
            == initial_removed_package_entry_count,
        "staged small-XML rejection should preserve removed package-entry audit count");
    check(editor.edit_plan().removed_parts().empty(),
        "staged small-XML rejection should not record removed parts");
    check(!editor.edit_plan().full_calculation_on_load(),
        "staged small-XML rejection should not request recalculation");
    check(editor.edit_plan().calc_chain_action() == fastxlsx::detail::CalcChainAction::Preserve,
        "staged small-XML rejection should not change calcChain policy");

    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave workbook copy-original");
    check_manifest_write_mode(editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave core properties copy-original");
    check_manifest_write_mode(editor, app_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave app properties copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave worksheet copy-original");
    check_manifest_write_mode(editor, unknown_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "staged small-XML rejection should leave unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "staged small-XML rejection output plan should not request recalculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "staged small-XML rejection output plan should preserve calcChain policy");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve workbook bytes");
    check_output_entry_plan(output_plan.entries, "docProps/core.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve core properties bytes");
    check_output_entry_plan(output_plan.entries, "docProps/app.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve app properties bytes");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve worksheet bytes");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "staged small-XML rejection output plan should preserve unknown bytes");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "staged small-XML rejection output should preserve source workbook bytes");
    check(output_reader.read_entry("docProps/core.xml") == source.core_properties,
        "staged small-XML rejection output should preserve source core properties bytes");
    check(output_reader.read_entry("docProps/app.xml") == app_properties,
        "staged small-XML rejection output should preserve source app properties bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "staged small-XML rejection output should preserve source worksheet bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "staged small-XML rejection output should preserve unknown bytes");
}

void test_package_editor_rejects_oversized_workbook_xml_materialization_without_state_changes()
{
    SourcePackage source =
        write_source_package("fastxlsx-package-editor-oversized-workbook-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="Huge">)"
        + std::string(
            fastxlsx::detail::package_editor_workbook_xml_materialization_byte_limit + 1U, 'A')
        + R"(</definedName></definedNames></workbook>)";
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

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    fastxlsx::detail::PackageEditor replacement_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_replacement_plan_size =
        replacement_editor.edit_plan().size();
    bool replacement_failed = false;
    try {
        replacement_editor.replace_part(workbook_part, source.workbook,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "oversized workbook package-part replacement");
    } catch (const std::exception& error) {
        replacement_failed = true;
        check_contains(error.what(), "materialized workbook XML exceeds small XML limit",
            "oversized workbook replacement should report the small XML boundary");
        check_contains(error.what(), "workbook package-part replacement",
            "oversized workbook replacement should report the materialization purpose");
    }
    check(replacement_failed,
        "PackageEditor should reject oversized materialized workbook replacement");
    check(replacement_editor.edit_plan().size() == initial_replacement_plan_size,
        "oversized workbook replacement failure should not mutate the edit plan");
    check_manifest_write_mode(replacement_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized workbook replacement failure should leave workbook copy-original");

    fastxlsx::detail::PackageEditor by_name_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_by_name_plan_size = by_name_editor.edit_plan().size();
    bool worksheet_source_consumed = false;
    const fastxlsx::detail::WorksheetInputChunkCallback worksheet_source =
        [&](std::string& chunk) {
            worksheet_source_consumed = true;
            chunk = "<worksheet><sheetData/></worksheet>";
            return true;
        };

    bool by_name_failed = false;
    try {
        by_name_editor.replace_worksheet_part_from_chunk_source_by_name(
            "Sheet1", worksheet_source);
    } catch (const std::exception& error) {
        by_name_failed = true;
        check_contains(error.what(), "materialized workbook sheet catalog XML exceeds small XML limit",
            "oversized source workbook catalog should report the small XML boundary");
    }
    check(by_name_failed,
        "PackageEditor should reject oversized source workbook catalog materialization");
    check(!worksheet_source_consumed,
        "oversized source workbook catalog failure should not consume worksheet chunks");
    check(by_name_editor.edit_plan().size() == initial_by_name_plan_size,
        "oversized source workbook catalog failure should not mutate the edit plan");
    check_manifest_write_mode(by_name_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized source workbook catalog failure should leave workbook copy-original");
    check_manifest_write_mode(by_name_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized source workbook catalog failure should leave worksheet copy-original");
}

void test_package_editor_rejects_shared_strings_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-shared-strings-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName shared_strings_part("/xl/sharedStrings.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><si><t>staged only</t></si></sst>)";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(shared_strings_part, replacement_shared_strings,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "sharedStrings materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "sharedStrings materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "sharedStrings materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject source sharedStrings materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "sharedStrings materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, shared_strings_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep sharedStrings copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "sharedStrings materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, shared_strings_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sharedStrings materialized replacement failure should preserve sharedStrings");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings materialized replacement failure should not stage sharedStrings chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(shared_strings_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_shared_strings)},
        "sharedStrings staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, shared_strings_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "sharedStrings staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        shared_strings_part.zip_path(), true,
        "sharedStrings staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        shared_strings_part.zip_path(), false,
        "sharedStrings staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_styles_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-styles-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName styles_part("/xl/styles.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_styles =
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<fonts count="1"><font><b/></font></fonts>)"
        R"(<fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs>)"
        R"(</styleSheet>)";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(styles_part, replacement_styles,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "styles materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "styles materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "styles materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject source styles materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "styles materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, styles_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep styles copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "styles materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, styles_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "styles materialized replacement failure should preserve styles");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        styles_part.zip_path(), false,
        "styles materialized replacement failure should not stage styles chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        styles_part.zip_path(), false,
        "styles materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(styles_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_styles)},
        "styles staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, styles_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "styles staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        styles_part.zip_path(), true,
        "styles staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        styles_part.zip_path(), false,
        "styles staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_generic_source_part_materialized_replacement_without_state_changes()
{
    const LinkedObjectSourcePackage source =
        write_linked_object_source_package(
            "fastxlsx-package-editor-generic-source-part-materialized-source.xlsx");
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string replacement_opaque_payload = "opaque extension materialized payload";

    const std::size_t initial_plan_size = editor.edit_plan().size();
    bool failed = false;
    try {
        editor.replace_part(opaque_extension_part, replacement_opaque_payload,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "opaque extension materialized replacement");
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "materialized source package part replacement is not allowed",
            "generic source part materialized replacement should report staged-only boundary");
        check_contains(error.what(), "replace_part_chunks",
            "generic source part materialized replacement should point to staged chunks");
    }
    check(failed,
        "PackageEditor should reject generic source package part materialized replacement");
    check(editor.edit_plan().size() == initial_plan_size,
        "generic source part materialized replacement failure should not mutate edit plan");
    check_manifest_write_mode(editor, opaque_extension_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep opaque copy-original");
    check_manifest_write_mode(editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep workbook copy-original");
    check_manifest_write_mode(editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "generic source part materialized replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_plan = editor.planned_output();
    check_output_entry_plan(failed_plan.entries, opaque_extension_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "generic source part materialized replacement failure should preserve opaque part");
    check_output_entry_staged_replacement_chunks(failed_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part materialized replacement failure should not stage chunks");
    check_output_entry_materialized_replacement(failed_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part materialized replacement failure should not mark materialized replacement");

    editor.replace_part_chunks(opaque_extension_part,
        {fastxlsx::detail::PackageEntryChunk::memory(replacement_opaque_payload)},
        "opaque extension staged chunk replacement");
    const fastxlsx::detail::PackageEditorOutputPlan staged_plan = editor.planned_output();
    check_output_entry_plan(staged_plan.entries, opaque_extension_part.zip_path(),
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "generic source part staged replacement should appear in planned output");
    check_output_entry_staged_replacement_chunks(staged_plan.entries,
        opaque_extension_part.zip_path(), true,
        "generic source part staged replacement should expose staged chunks");
    check_output_entry_materialized_replacement(staged_plan.entries,
        opaque_extension_part.zip_path(), false,
        "generic source part staged replacement should not expose materialized replacement");
}

void test_package_editor_rejects_oversized_metadata_xml_materialization_without_state_changes()
{
    const SourcePackage source =
        write_source_package("fastxlsx-package-editor-oversized-metadata-xml-source.xlsx");
    const fastxlsx::detail::PartName core_part("/docProps/core.xml");
    const fastxlsx::detail::PartName app_part("/docProps/app.xml");
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string oversized_core_properties =
        "<cp:coreProperties><dc:creator>"
        + std::string(
            fastxlsx::detail::package_editor_metadata_xml_materialization_byte_limit + 1U,
            'M')
        + "</dc:creator></cp:coreProperties>";

    fastxlsx::detail::PackageEditor part_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_part_plan_size = part_editor.edit_plan().size();
    const std::size_t initial_part_package_entry_count =
        part_editor.edit_plan().package_entries().size();

    bool part_failed = false;
    try {
        part_editor.replace_part(core_part, oversized_core_properties,
            fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "oversized core properties materialized replacement");
    } catch (const std::exception& error) {
        part_failed = true;
        check_contains(error.what(), "materialized metadata XML exceeds small XML limit",
            "oversized core properties replacement should report metadata XML limit");
        check_contains(error.what(), "package-part replacement",
            "oversized core properties replacement should report the replacement boundary");
    }
    check(part_failed,
        "PackageEditor should reject oversized materialized metadata part replacement");
    check(part_editor.edit_plan().size() == initial_part_plan_size,
        "oversized metadata part replacement failure should not mutate edit plan");
    check(part_editor.edit_plan().package_entries().size()
            == initial_part_package_entry_count,
        "oversized metadata part replacement failure should not add package-entry audit");
    check_manifest_write_mode(part_editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep core properties copy-original");
    check_manifest_write_mode(part_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep workbook copy-original");
    check_manifest_write_mode(part_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized metadata part replacement failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_part_plan =
        part_editor.planned_output();
    check_output_entry_plan(failed_part_plan.entries, core_part.zip_path(),
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "oversized metadata part replacement failure should preserve core properties");
    check_output_entry_materialized_replacement(failed_part_plan.entries,
        core_part.zip_path(), false,
        "oversized metadata part replacement failure should not mark materialized replacement");

    fastxlsx::detail::PackageEditor properties_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_properties_plan_size =
        properties_editor.edit_plan().size();
    const std::size_t initial_properties_note_count =
        properties_editor.edit_plan().notes().size();
    const std::size_t initial_properties_package_entry_count =
        properties_editor.edit_plan().package_entries().size();
    const std::size_t initial_properties_removed_part_count =
        properties_editor.edit_plan().removed_parts().size();
    const std::size_t initial_properties_removed_package_entry_count =
        properties_editor.edit_plan().removed_package_entries().size();

    fastxlsx::DocumentProperties properties;
    properties.creator = std::string(
        fastxlsx::detail::package_editor_metadata_xml_materialization_byte_limit + 1U,
        'C');

    bool properties_failed = false;
    try {
        properties_editor.set_document_properties(properties);
    } catch (const std::exception& error) {
        properties_failed = true;
        check_contains(error.what(), "materialized metadata XML exceeds small XML limit",
            "oversized generated document properties should report metadata XML limit");
        check_contains(error.what(), "core document properties replacement",
            "oversized generated document properties should report the generated part boundary");
    }
    check(properties_failed,
        "PackageEditor should reject oversized generated document properties XML");
    check(properties_editor.edit_plan().size() == initial_properties_plan_size,
        "oversized generated metadata failure should not mutate edit plan");
    check(properties_editor.edit_plan().notes().size() == initial_properties_note_count,
        "oversized generated metadata failure should not add notes");
    check(properties_editor.edit_plan().package_entries().size()
            == initial_properties_package_entry_count,
        "oversized generated metadata failure should not add package-entry audit");
    check(properties_editor.edit_plan().removed_parts().size()
            == initial_properties_removed_part_count,
        "oversized generated metadata failure should not record removed parts");
    check(properties_editor.edit_plan().removed_package_entries().size()
            == initial_properties_removed_package_entry_count,
        "oversized generated metadata failure should not record removed package entries");
    check(properties_editor.manifest().find_part(app_part) == nullptr,
        "oversized generated metadata failure should not add missing app properties part");
    check_manifest_write_mode(properties_editor, core_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep core properties copy-original");
    check_manifest_write_mode(properties_editor, workbook_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep workbook copy-original");
    check_manifest_write_mode(properties_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "oversized generated metadata failure should keep worksheet copy-original");
    const fastxlsx::detail::PackageEditorOutputPlan failed_properties_plan =
        properties_editor.planned_output();
    check_output_plan_preserves_source_copy_original(properties_editor,
        failed_properties_plan,
        "oversized generated metadata failure should preserve source copy-original output");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor shard: " << shard << '\n';
        if (!should_run_package_editor_shard(shard, "sheetdata-catalog-guards")) {
            throw TestFailure("wrong shard for sheetdata-catalog-guards executable");
        }

        test_package_editor_rejects_staged_small_xml_replacements_without_state_changes();
        test_package_editor_rejects_oversized_workbook_xml_materialization_without_state_changes();
        test_package_editor_rejects_shared_strings_materialized_replacement_without_state_changes();
        test_package_editor_rejects_styles_materialized_replacement_without_state_changes();
        test_package_editor_rejects_generic_source_part_materialized_replacement_without_state_changes();
        test_package_editor_rejects_oversized_metadata_xml_materialization_without_state_changes();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
