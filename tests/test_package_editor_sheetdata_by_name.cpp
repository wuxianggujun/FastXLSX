#include "test_package_editor_sheetdata_common.hpp"

void test_package_editor_replaces_worksheet_sheet_data_from_chunk_source()
{
    const LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-chunk-source-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-chunk-source-output.xlsx");
    const std::filesystem::path by_name_output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-chunk-source-output.xlsx");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");

    const std::string sheet_data_prefix = "<sheetData>";
    const std::string sheet_data_body =
        R"(<row r="6"><c r="A6" t="s"><v>0</v></c><c r="B6" s="1"><f>A6</f></c></row>)";
    const std::string sheet_data_suffix = "</sheetData>";
    const std::string replacement_sheet_data =
        sheet_data_prefix + sheet_data_body + sheet_data_suffix;

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
        make_test_chunk_source({sheet_data_prefix, sheet_data_body, sheet_data_suffix}));

    check(has_note_containing(editor.edit_plan().notes(),
              {"sheetData replacement", "chunk-source replacement/output",
                  "consumed directly", "file-backed staged chunk"}),
        "chunk-source sheetData replacement should expose file-backed handoff");
    const auto& payload_audits = editor.edit_plan().worksheet_payload_dependency_audits();
    using PayloadAuditKind = fastxlsx::detail::WorksheetPayloadDependencyAuditKind;
    using PayloadAuditScope = fastxlsx::detail::WorksheetPayloadDependencyAuditScope;
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"shared string indexes", "xl/sharedStrings.xml"}),
        "chunk-source sheetData replacement should audit shared string references");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Styles,
              PayloadAuditScope::SheetDataReplacement, "c",
              {"style id references", "xl/styles.xml"}),
        "chunk-source sheetData replacement should audit style references");
    check(has_payload_audit(payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f",
              {"formulas", "calcChain policy"}),
        "chunk-source sheetData replacement should audit formulas");
    const fastxlsx::detail::PackageEditorOutputPlan sheet_data_output_plan =
        editor.planned_output();
    check_output_entry_staged_replacement_chunks(sheet_data_output_plan.entries,
        worksheet_part.zip_path(), true,
        "chunk-source sheetData replacement output plan should expose rewritten worksheet staged chunks");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"validates replacement sheetData root",
                  "replacement payload dependency audit",
                  "without staging or replaying a separate replacement sheetData chunk"}),
        "chunk-source sheetData replacement should expose direct payload insert validation/audit");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"sheetData replacement output writer", "relationship-id audit",
                  "without a separate post-output worksheet validation or audit reread"}),
        "chunk-source sheetData replacement should expose fused output relationship audit");
    check(has_note_containing(sheet_data_output_plan.notes,
              {"sheetData replacement output writer", "preserved worksheet metadata audit",
                  "without a separate preservation-only worksheet reread"}),
        "chunk-source sheetData replacement should expose fused preservation audit");

    editor.save_as(output);
    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_contains(output_reader.read_entry(worksheet_part.zip_path()),
        replacement_sheet_data,
        "chunk-source sheetData replacement output should contain replacement payload");

    fastxlsx::detail::PackageEditor by_name_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    by_name_editor.replace_worksheet_sheet_data_from_chunk_source_by_name("Sheet1",
        make_test_chunk_source({sheet_data_prefix, sheet_data_body, sheet_data_suffix}));
    check(has_note_containing(by_name_editor.edit_plan().notes(),
              {"by-name sheetData chunk-source replacement",
                  "planned/source workbook catalog"}),
        "by-name chunk-source sheetData replacement should expose catalog handoff");
    by_name_editor.save_as(by_name_output);
    const fastxlsx::detail::PackageReader by_name_output_reader =
        fastxlsx::detail::PackageReader::open(by_name_output);
    check_contains(by_name_output_reader.read_entry(worksheet_part.zip_path()),
        replacement_sheet_data,
        "by-name chunk-source sheetData replacement output should contain replacement payload");

    const std::vector<std::string> cdata_sheet_data_chunks {
        R"(<sheetData><row r="7"><c r="A7" t="inlineStr"><is><t><![CDATA[literal > )",
        R"(<c r="Z99" t="s"><f>not-a-formula</f></c>)",
        R"(]]></t></is></c></row>)",
        R"(</sheetData>)",
    };
    fastxlsx::detail::PackageEditor cdata_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    cdata_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(cdata_sheet_data_chunks));
    const auto& cdata_payload_audits =
        cdata_editor.edit_plan().worksheet_payload_dependency_audits();
    check(!has_payload_audit(cdata_payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c"),
        "sheetData CDATA text should not be scanned as shared string cell markup");
    check(!has_payload_audit(cdata_payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f"),
        "sheetData CDATA text should not be scanned as formula markup");

    std::vector<std::string> long_cdata_sheet_data_chunks {
        R"(<sheetData><row r="8"><c r="A8" t="inlineStr"><is><t><![CDATA[)",
    };
    const std::string cdata_padding_chunk(1024, 'y');
    const std::size_t cdata_padding_chunk_count =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit
        / cdata_padding_chunk.size() - 8U;
    for (std::size_t index = 0; index < cdata_padding_chunk_count; ++index) {
        long_cdata_sheet_data_chunks.push_back(cdata_padding_chunk);
    }
    long_cdata_sheet_data_chunks.push_back(
        R"(<drawing r:id="rIdLongCdataText"/><f>not-a-formula</f>]]>ok</t></is></c></row></sheetData>)");
    fastxlsx::detail::PackageEditor long_cdata_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    long_cdata_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(long_cdata_sheet_data_chunks));
    check(!has_note_containing(long_cdata_editor.edit_plan().notes(),
              {"relationship-id audit", "could not parse"}),
        "long sheetData CDATA text should not trip the relationship scanner retained window");
    check(!has_note_containing(long_cdata_editor.edit_plan().notes(),
              {"rIdLongCdataText"}),
        "long sheetData CDATA text should not be scanned as a relationship reference");

    std::vector<std::string> pi_sheet_data_chunks {
        R"(<sheetData><row r="9"><c r="A9" t="inlineStr"><is><?fastxlsx )",
    };
    const std::string pi_padding_chunk(1024, 'x');
    const std::size_t pi_padding_chunk_count =
        fastxlsx::detail::package_editor_sheet_data_replacement_payload_byte_limit
        / pi_padding_chunk.size() - 8U;
    for (std::size_t index = 0; index < pi_padding_chunk_count; ++index) {
        pi_sheet_data_chunks.push_back(pi_padding_chunk);
    }
    pi_sheet_data_chunks.push_back(
        R"(<drawing r:id="rIdLongPiText"/><c r="Z99" t="s"><f>not-a-formula</f></c> ?><t>ok</t></is></c></row></sheetData>)");
    fastxlsx::detail::PackageEditor pi_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    pi_editor.replace_worksheet_sheet_data_from_chunk_source(
        worksheet_part, make_test_chunk_source(pi_sheet_data_chunks));
    const auto& pi_payload_audits =
        pi_editor.edit_plan().worksheet_payload_dependency_audits();
    check(!has_payload_audit(pi_payload_audits, worksheet_part,
              PayloadAuditKind::SharedStrings,
              PayloadAuditScope::SheetDataReplacement, "c"),
        "sheetData processing instruction text should not be scanned as shared string cell markup");
    check(!has_payload_audit(pi_payload_audits, worksheet_part,
              PayloadAuditKind::Formula,
              PayloadAuditScope::SheetDataReplacement, "f"),
        "sheetData processing instruction text should not be scanned as formula markup");
    check(!has_note_containing(pi_editor.edit_plan().notes(),
              {"relationship-id audit", "could not parse"}),
        "long sheetData processing instruction should not trip the relationship scanner retained window");
    check(!has_note_containing(pi_editor.edit_plan().notes(),
              {"rIdLongPiText"}),
        "long sheetData processing instruction text should not be scanned as a relationship reference");

    fastxlsx::detail::PackageEditor invalid_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t initial_plan_size = invalid_editor.edit_plan().size();
    const std::size_t initial_note_count = invalid_editor.edit_plan().notes().size();
    bool invalid_failed = false;
    try {
        invalid_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            make_test_chunk_source({"<row/>"}));
    } catch (const std::exception& error) {
        invalid_failed = true;
        check_contains(error.what(), "sheetData",
            "invalid chunk-source sheetData replacement should name sheetData");
        check_not_contains(error.what(), "current worksheet input",
            "invalid replacement sheetData should not be mislabeled as source worksheet input");
    }
    check(invalid_failed,
        "invalid chunk-source sheetData replacement should fail");
    check(invalid_editor.edit_plan().size() == initial_plan_size,
        "invalid chunk-source sheetData replacement should not change edit-plan size");
    check(invalid_editor.edit_plan().notes().size() == initial_note_count,
        "invalid chunk-source sheetData replacement should not add notes");
    check(!invalid_editor.edit_plan().full_calculation_on_load(),
        "invalid chunk-source sheetData replacement should not request recalculation");

    fastxlsx::detail::PackageEditor throwing_sheet_data_editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const std::size_t throwing_sheet_data_initial_plan_size =
        throwing_sheet_data_editor.edit_plan().size();
    const std::size_t throwing_sheet_data_initial_note_count =
        throwing_sheet_data_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> throwing_sheet_data_temp_files_before =
        package_editor_temp_files();
    int throwing_sheet_data_reads = 0;
    bool throwing_sheet_data_failed = false;
    try {
        throwing_sheet_data_editor.replace_worksheet_sheet_data_from_chunk_source(
            worksheet_part,
            [&](std::string& chunk) {
                ++throwing_sheet_data_reads;
                if (throwing_sheet_data_reads == 1) {
                    chunk = "<sheetData>";
                    return true;
                }
                throw std::runtime_error("caller sheetData stream stopped");
            });
    } catch (const std::exception& error) {
        throwing_sheet_data_failed = true;
        check_contains(error.what(), "failed while reading sheetData replacement XML",
            "throwing sheetData chunk source should name the replacement read boundary");
        check_contains(error.what(), "caller sheetData stream stopped",
            "throwing sheetData chunk source should preserve the caller failure");
        check_not_contains(error.what(), "current worksheet input",
            "throwing replacement sheetData source should not be mislabeled as current worksheet input");
    }
    check(throwing_sheet_data_failed,
        "throwing chunk-source sheetData replacement should fail");
    check(throwing_sheet_data_reads == 2,
        "throwing chunk-source sheetData replacement should stop at the throwing read");
    check(throwing_sheet_data_editor.edit_plan().size()
            == throwing_sheet_data_initial_plan_size,
        "throwing chunk-source sheetData replacement should not change edit-plan size");
    check(throwing_sheet_data_editor.edit_plan().notes().size()
            == throwing_sheet_data_initial_note_count,
        "throwing chunk-source sheetData replacement should not add notes");
    check(!throwing_sheet_data_editor.edit_plan().full_calculation_on_load(),
        "throwing chunk-source sheetData replacement should not request recalculation");
    check_manifest_write_mode(throwing_sheet_data_editor, worksheet_part,
        fastxlsx::detail::PartWriteMode::CopyOriginal,
        "throwing chunk-source sheetData replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(throwing_sheet_data_temp_files_before,
        "throwing chunk-source sheetData replacement should not leak staged temp files");

    const SourcePackage missing_entry_source =
        write_missing_worksheet_entry_source_package(
            "fastxlsx-package-editor-sheetdata-missing-source-entry-source.xlsx");
    fastxlsx::detail::PackageEditor missing_entry_editor =
        fastxlsx::detail::PackageEditor::open(missing_entry_source.path);
    const std::size_t missing_entry_initial_plan_size =
        missing_entry_editor.edit_plan().size();
    const std::size_t missing_entry_initial_note_count =
        missing_entry_editor.edit_plan().notes().size();
    const std::vector<std::filesystem::path> temp_files_before =
        package_editor_temp_files();
    int missing_entry_reads = 0;
    bool missing_entry_failed = false;
    try {
        missing_entry_editor.replace_worksheet_sheet_data_from_chunk_source(worksheet_part,
            [&](std::string& chunk) {
                ++missing_entry_reads;
                chunk = "<sheetData/>";
                return true;
            });
    } catch (const std::exception& error) {
        missing_entry_failed = true;
        check_contains(error.what(), "worksheet sheetData replacement target",
            "missing worksheet-entry sheetData failure should explain the target preflight");
        check_contains(error.what(), "worksheet part '/xl/worksheets/sheet1.xml'",
            "missing worksheet-entry sheetData failure should include the worksheet part");
        check_contains(error.what(), "ZIP entry 'xl/worksheets/sheet1.xml'",
            "missing worksheet-entry sheetData failure should include the worksheet entry");
    }
    check(missing_entry_failed,
        "missing worksheet-entry sheetData replacement should fail");
    check(missing_entry_reads == 0,
        "missing worksheet-entry sheetData replacement should fail before consuming input");
    check(missing_entry_editor.edit_plan().size() == missing_entry_initial_plan_size,
        "missing worksheet-entry sheetData replacement should not change edit-plan size");
    check(missing_entry_editor.edit_plan().notes().size() == missing_entry_initial_note_count,
        "missing worksheet-entry sheetData replacement should not add notes");
    check(!missing_entry_editor.edit_plan().full_calculation_on_load(),
        "missing worksheet-entry sheetData replacement should not request recalculation");
    const auto* missing_entry_manifest_part =
        missing_entry_editor.manifest().find_part(worksheet_part);
    check(missing_entry_manifest_part == nullptr
            || missing_entry_manifest_part->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "missing worksheet-entry sheetData replacement should not change worksheet manifest state");
    check_no_new_package_editor_temp_files(temp_files_before,
        "missing worksheet-entry sheetData replacement should not create staged temp files");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-source.xlsx");
    source.workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst><ext><sheets><sheet name="Decoy Sheet" sheetId="777" r:id="rId1"/></sheets></ext></extLst>)"
        R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
        R"(<definedNames><definedName name="ReportRange">Sheet1!$A$1:$B$2</definedName></definedNames>)"
        R"(</workbook>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="4"><c r="A4"><v>44</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "sheet-name sheetData replacement should resolve the worksheet part");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "sheet-name sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "sheet-name sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "sheet-name sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "sheet-name sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "sheet-name sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "sheet-name sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "sheet-name sheetData replacement should ignore decoy workbook sheets catalogs");
    bool decoy_lookup_failed = false;
    try {
        (void)output_reader.worksheet_part_by_sheet_name("Decoy Sheet");
    } catch (const std::exception&) {
        decoy_lookup_failed = true;
    }
    check(decoy_lookup_failed,
        "sheet-name sheetData replacement should not expose decoy workbook sheets catalogs");
    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "sheet-name sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "sheet-name sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "sheet-name sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "sheet-name sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "sheet-name sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_absolute_targets()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-absolute-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="/xl/workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-absolute-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="6"><c r="A6"><v>66</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "absolute-target sheetData replacement should resolve the worksheet part by name");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "absolute-target sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "absolute-target sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "absolute-target sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "absolute-target sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "absolute-target sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "absolute-target sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "absolute-target sheetData replacement output should keep sheet-name lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "/xl/workbook.xml",
        "absolute-target sheetData replacement should preserve absolute package workbook target");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "absolute-target sheetData replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "/xl/worksheets/sheet1.xml",
        "absolute-target sheetData replacement should preserve absolute worksheet target");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "absolute-target sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "absolute-target sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "absolute-target sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "absolute-target sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "absolute-target sheetData replacement should keep unknown extension default content type");
}

void test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_dot_segment_targets()
{
    LinkedObjectSourcePackage source =
        write_sheet_data_patch_source_package(
            "fastxlsx-package-editor-sheetdata-by-name-dot-segments-source.xlsx");
    source.package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
    source.workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="./worksheets/../worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/vbaProject" Target="vbaProject.bin"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/calcChain" Target="calcChain.xml"/>)"
        R"(</Relationships>)";
    rewrite_linked_object_source_package(source);

    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-sheetdata-by-name-dot-segments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName calc_chain_part("/xl/calcChain.xml");
    const fastxlsx::detail::PartName opaque_extension_part("/custom/opaque-extension.bin");

    check(editor.reader().worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "dot-segment source should resolve the sheet catalog before patching");

    const std::string replacement_sheet_data =
        R"(<sheetData><row r="7"><c r="A7"><v>77</v></c></row></sheetData>)";
    replace_worksheet_sheet_data_by_name_from_single_chunk_source(editor, "Sheet1", replacement_sheet_data);

    const auto* worksheet_plan = editor.edit_plan().find_part(worksheet_part);
    check(worksheet_plan != nullptr,
        "dot-segment sheetData replacement should resolve the worksheet part by name");
    check(worksheet_plan->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "dot-segment sheetData replacement should plan worksheet local-DOM rewrite");
    check(editor.edit_plan().find_removed_part(calc_chain_part) != nullptr,
        "dot-segment sheetData replacement should remove stale calcChain by default");
    check(editor.edit_plan().full_calculation_on_load(),
        "dot-segment sheetData replacement should request full calculation");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "dot-segment sheetData output plan should local-DOM-rewrite worksheet");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "dot-segment sheetData output plan should rewrite workbook relationships for calcChain removal");
    check_output_entry_plan(output_plan.entries, "custom/opaque-extension.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "dot-segment sheetData output plan should preserve unknown extension");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/calcChain.xml") == entries.end(),
        "dot-segment sheetData replacement output should omit stale calcChain");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.worksheet_part_by_sheet_name("Sheet1") == worksheet_part,
        "dot-segment sheetData replacement output should keep sheet-name lookup readable");
    const auto* package_workbook_relationship =
        output_reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "dot-segment sheetData replacement should preserve package workbook target bytes");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "dot-segment sheetData replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rId1") != nullptr
            && workbook_relationships->find_by_id("rId1")->target
                == "./worksheets/../worksheets/sheet1.xml",
        "dot-segment sheetData replacement should preserve worksheet target bytes");
    const std::string output_workbook_relationships =
        output_reader.read_entry("xl/_rels/workbook.xml.rels");
    check_contains(output_workbook_relationships,
        R"(Target="./worksheets/../worksheets/sheet1.xml")",
        "dot-segment sheetData replacement should preserve worksheet target text");
    check_not_contains(output_workbook_relationships, "relationships/calcChain",
        "dot-segment sheetData replacement should remove stale calcChain relationship");

    const std::string worksheet_xml =
        output_reader.read_entry("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, replacement_sheet_data,
        "dot-segment sheetData replacement should write replacement sheetData");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "dot-segment sheetData replacement should remove old sheetData rows");
    check_contains(output_reader.read_entry("xl/workbook.xml"),
        R"(fullCalcOnLoad="1")",
        "dot-segment sheetData replacement should update workbook calc metadata");
    check(output_reader.read_entry("custom/opaque-extension.bin")
            == source.opaque_extension,
        "dot-segment sheetData replacement should preserve unknown extension bytes");
    check(output_reader.content_types().override_for(opaque_extension_part) == nullptr,
        "dot-segment sheetData replacement should keep unknown extension default content type");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor sheetdata shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "sheetdata-by-name")) {
            test_package_editor_replaces_worksheet_sheet_data_from_chunk_source();
            test_package_editor_replaces_worksheet_sheet_data_by_sheet_name();
            test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_absolute_targets();
            test_package_editor_replaces_worksheet_sheet_data_by_sheet_name_with_dot_segment_targets();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
