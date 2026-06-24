#include "test_package_editor_preservation_comments_common.hpp"

void test_package_editor_replaces_comments_and_preserves_worksheet_links()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-linked-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Patch</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>patched comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, replacement_comments,
        "linked fixture comments local-DOM rewrite");

    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "linked fixture comments replacement should be present in the edit plan");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture comments replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "linked fixture comments replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "linked fixture comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture workbook should remain copy-original after comments replacement");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture worksheet should remain copy-original after comments replacement");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "linked fixture unknown part should remain copy-original after comments replacement");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "linked fixture comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "linked fixture comments replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "linked fixture comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "linked fixture comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "linked fixture comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "linked fixture comments replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "linked fixture comments replacement output plan should rewrite comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "linked fixture comments replacement output plan should classify comments part");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("comments") != std::string::npos,
        "linked fixture comments replacement output plan should keep replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "linked fixture comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "linked fixture comments replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "linked fixture comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == replacement_comments,
        "linked fixture comments replacement should write replacement comments XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "linked fixture comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "linked fixture comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "linked fixture comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "linked fixture comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "linked fixture comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "linked fixture comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "linked fixture comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "linked fixture comments replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "linked fixture comments replacement should keep worksheet comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "linked fixture comments replacement should keep worksheet comments relationship target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "linked fixture comments replacement should keep worksheet comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "linked fixture comments replacement should not invent comments owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "linked fixture comments replacement should keep comments content type override");
}

void test_package_editor_repeated_comments_replacement_updates_final_state()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-repeat-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Stale</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>stale comment</t></text></comment></commentList>)"
        R"(</comments>)";
    const std::string final_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Final</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>final comment</t></text></comment></commentList>)"
        R"(</comments>)";

    replace_part_with_memory_chunks(editor, comments_part, stale_comments,
        "stale repeated comments local-DOM rewrite");
    replace_part_with_memory_chunks(editor, comments_part, final_comments,
        "final repeated comments local-DOM rewrite");

    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "repeated comments replacement should keep an active edit-plan part");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated comments replacement should keep final local-DOM-rewrite mode");
    check(comments_plan->reason.find("final repeated") != std::string::npos,
        "repeated comments replacement should keep final reason");
    check(comments_plan->reason.find("stale repeated") == std::string::npos,
        "repeated comments replacement should drop stale reason");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated comments replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(comments_part) != nullptr,
        "repeated comments replacement should keep comments content type override");
    check(editor.edit_plan().find_removed_part(comments_part) == nullptr,
        "repeated comments replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated comments replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "repeated comments replacement should not rewrite inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated comments replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated comments replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated comments replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated comments replacement output plan should rewrite comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("final repeated") != std::string::npos,
        "repeated comments replacement output plan should keep final reason");
    check(output_comments_plan->reason.find("stale repeated") == std::string::npos,
        "repeated comments replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "repeated comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == final_comments,
        "repeated comments replacement should write final comments payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated comments replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "repeated comments replacement should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "repeated comments replacement should not rewrite inbound comments target");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "repeated comments replacement should not invent comments owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "repeated comments replacement should keep comments content type override");
}

void test_package_editor_removes_comments_and_preserves_worksheet_links()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-remove-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(comments_part, "explicit comments part removal");

    check(editor.edit_plan().find_part(comments_part) == nullptr,
        "explicit comments removal should clear the active edit-plan part");
    const auto* removed_comments = editor.edit_plan().find_removed_part(comments_part);
    check(removed_comments != nullptr,
        "explicit comments removal should record removed-part audit");
    check(removed_comments->reason.find("comments part") != std::string::npos,
        "explicit comments removal should retain the removal reason");
    check(removed_comments->reason.find("inbound relationship preserved")
            != std::string::npos,
        "explicit comments removal should audit preserved inbound relationships");
    check(removed_comments->reason.find("/xl/worksheets/sheet1.xml")
            != std::string::npos,
        "explicit comments removal inbound audit should include owner part");
    check(removed_comments->reason.find("rId1") != std::string::npos,
        "explicit comments removal inbound audit should include relationship id");
    check(removed_comments->reason.find("../comments/comment1.xml")
            != std::string::npos,
        "explicit comments removal inbound audit should include original target");
    check(removed_comments->inbound_relationships.size() == 1,
        "explicit comments removal should keep structured inbound audit");
    const auto& comments_inbound = removed_comments->inbound_relationships.front();
    check(comments_inbound.owner_part == worksheet_part.value(),
        "explicit comments removal should keep inbound owner part");
    check(comments_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "explicit comments removal should keep inbound owner relationship entry");
    check(comments_inbound.relationship_id == "rId1",
        "explicit comments removal should keep inbound relationship id");
    check(comments_inbound.relationship_type
            == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
        "explicit comments removal should keep inbound relationship type");
    check(comments_inbound.relationship_target == "../comments/comment1.xml",
        "explicit comments removal should keep inbound raw target");
    check(comments_inbound.target_part == comments_part,
        "explicit comments removal should keep normalized target part");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "explicit comments removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(comments_part) == nullptr,
        "explicit comments removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "explicit comments removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "explicit comments removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "explicit comments removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "explicit comments removal should not invent comments owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "explicit comments removal should not invent comments owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "explicit comments removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") == entries.end(),
        "explicit comments removal output should omit comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "explicit comments removal output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "explicit comments removal output should not invent comments owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(comments_part) == nullptr,
        "explicit comments removal output should remove comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/comments/comment1.xml",
        "explicit comments removal content types XML should omit comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "explicit comments removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "explicit comments removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "explicit comments removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "explicit comments removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "explicit comments removal should not prune inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "explicit comments removal should preserve unknown bytes");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "explicit comments removal should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "explicit comments removal should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "explicit comments removal should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "explicit comments removal should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "explicit comments removal should not keep owner relationships for absent comments");
}

void test_package_editor_comments_replacement_restores_prior_removal()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-replace-after-remove-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-after-remove-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(comments_part, "temporary comments removal");
    check(editor.edit_plan().find_removed_part(comments_part) != nullptr,
        "setup should record removed comments before replacement restore");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "setup should not invent comments owner relationships omission");
    const auto* removal_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(removal_content_types != nullptr,
        "setup should record content types rewrite before comments restore");
    check(removal_content_types->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "setup content types audit should be local-DOM-rewrite after comments removal");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "setup should remove comments from manifest before replacement restore");

    const std::string restored_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Restored</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>restored comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, restored_comments,
        "restored comments after removal");

    check(editor.edit_plan().find_removed_part(comments_part) == nullptr,
        "comments replacement after removal should clear stale removed-part audit");
    const auto* comments_plan = editor.edit_plan().find_part(comments_part);
    check(comments_plan != nullptr,
        "comments replacement after removal should restore active edit-plan part");
    check(comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "comments replacement after removal should keep final write mode");
    check(comments_plan->reason.find("after removal") != std::string::npos,
        "comments replacement after removal should keep final replacement reason");
    check_manifest_write_mode(editor, comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "comments replacement after removal should restore manifest part write mode");
    check(editor.manifest().content_types().override_for(comments_part) != nullptr,
        "comments replacement after removal should restore manifest content type override");
    const auto* restored_content_types =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(restored_content_types != nullptr,
        "comments replacement after removal should keep content types audit");
    check(restored_content_types->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should restore source content types audit");
    check(restored_content_types->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments replacement after removal should keep content types audit role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "comments replacement after removal should not rewrite inbound worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments replacement after removal should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "comments replacement after removal output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "comments replacement after removal output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "comments replacement after removal output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "comments replacement after removal output plan should clear removed parts");
    check(output_plan.removed_package_entries.empty(),
        "comments replacement after removal output plan should clear removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "comments replacement after removal output plan should rewrite comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "comments replacement after removal output plan should classify rewritten comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("after removal") != std::string::npos,
        "comments replacement after removal output plan should keep comments replacement reason");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments replacement after removal output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "comments replacement after removal output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments replacement after removal output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve inbound worksheet relationships");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments replacement after removal output plan should preserve unknown entry");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") != entries.end(),
        "comments replacement after removal output should restore comments part entry");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "comments replacement after removal output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "comments replacement after removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, comments_part.zip_path());
    check(output_reader.read_entry("xl/comments/comment1.xml") == restored_comments,
        "comments replacement after removal should write restored comments bytes");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "comments replacement after removal should restore source content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "comments replacement after removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "comments replacement after removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "comments replacement after removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "comments replacement after removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "comments replacement after removal should preserve inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "comments replacement after removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "comments replacement after removal should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "comments replacement after removal should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "comments replacement after removal should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "comments replacement after removal should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "comments replacement after removal should not create owner relationships");
    check(output_reader.content_types().override_for(comments_part) != nullptr,
        "comments replacement after removal should restore comments content type override");
}

void test_package_editor_comments_removal_overrides_prior_replacement()
{
    const CommentsSourcePackage source =
        write_comments_source_package("fastxlsx-package-editor-remove-after-replace-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-after-replace-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_comments =
        R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<authors><author>Stale</author></authors>)"
        R"(<commentList><comment ref="A1" authorId="0"><text><t>stale comment</t></text></comment></commentList>)"
        R"(</comments>)";
    replace_part_with_memory_chunks(editor, comments_part, replacement_comments,
        "prior comments replacement before removal");
    const auto* prior_comments_plan = editor.edit_plan().find_part(comments_part);
    check(prior_comments_plan != nullptr,
        "setup should record active comments replacement before removal override");
    check(prior_comments_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "setup comments replacement should be local-DOM-rewrite before removal override");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "setup comments replacement should not invent owner relationships audit");

    editor.remove_part(comments_part, "explicit comments removal after replacement");

    check(editor.edit_plan().find_part(comments_part) == nullptr,
        "comments removal after replacement should clear active replacement entry");
    const auto* removed_comments = editor.edit_plan().find_removed_part(comments_part);
    check(removed_comments != nullptr,
        "comments removal after replacement should record removed-part audit");
    check(removed_comments->reason.find("after replacement") != std::string::npos,
        "comments removal after replacement should keep final removal reason");
    check(removed_comments->reason.find("inbound relationship preserved")
            != std::string::npos,
        "comments removal after replacement should keep inbound relationship audit");
    check(removed_comments->inbound_relationships.size() == 1,
        "comments removal after replacement should keep structured inbound audit");
    check(removed_comments->inbound_relationships.front().target_part == comments_part,
        "comments removal after replacement should keep normalized inbound target");
    check(editor.manifest().find_part(comments_part) == nullptr,
        "comments removal after replacement should remove manifest part");
    check(editor.manifest().content_types().override_for(comments_part) == nullptr,
        "comments removal after replacement should remove manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "comments removal after replacement should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "comments removal after replacement content types audit should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments removal after replacement content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "comments removal after replacement should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "comments removal after replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "comments removal after replacement output plan should keep calcChain preserve state");
    check(output_plan.relationship_target_audits.empty(),
        "comments removal after replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.size() == 1,
        "comments removal after replacement output plan should expose one removed part");
    check(output_plan.removed_parts.front().part_name == comments_part,
        "comments removal after replacement output plan should expose removed comments part");
    check(output_plan.removed_parts.front().reason.find("after replacement")
            != std::string::npos,
        "comments removal after replacement output plan should keep removed-part reason");
    check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
        "comments removal after replacement output plan should keep removed-part inbound audit");
    check(output_plan.removed_package_entries.empty(),
        "comments removal after replacement output plan should not omit metadata entries");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
        "comments removal after replacement output plan should omit comments part");
    check_output_entry_part_context(output_plan.entries, "xl/comments/comment1.xml",
        true, comments_part.value(),
        "comments removal after replacement output plan should classify omitted comments");
    const auto* output_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml");
    check(output_comments_plan->reason.find("after replacement") != std::string::npos,
        "comments removal after replacement output plan should keep final removal reason");
    check(output_comments_plan->inbound_relationships.size() == 1,
        "comments removal after replacement output plan should expose inbound audit");
    check_output_entry_has_inbound_relationship(output_plan.entries,
        "xl/comments/comment1.xml", worksheet_part.value(),
        "xl/worksheets/_rels/sheet1.xml.rels", "rId1",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
        "../comments/comment1.xml", comments_part,
        "comments removal after replacement output plan should keep worksheet inbound audit");
    check(find_output_entry_plan(output_plan.entries, "xl/comments/_rels/comment1.xml.rels")
            == nullptr,
        "comments removal after replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
        "comments removal after replacement output plan should rewrite content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
        "",
        "comments removal after replacement output plan should classify content types as metadata");
    const auto* output_content_types_plan =
        find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
    check(output_content_types_plan->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "comments removal after replacement output plan should classify content types metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "comments removal after replacement output plan should preserve inbound worksheet relationships");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/comments/comment1.xml") == entries.end(),
        "comments removal after replacement output should omit comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "comments removal after replacement output should keep worksheet relationships");
    check(entries.find("xl/comments/_rels/comment1.xml.rels") == entries.end(),
        "comments removal after replacement output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(comments_part) == nullptr,
        "comments removal after replacement output should remove comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/comments/comment1.xml",
        "comments removal after replacement content types XML should omit comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "comments removal after replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "comments removal after replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "comments removal after replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "comments removal after replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "comments removal after replacement should preserve inbound worksheet relationships");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "comments removal after replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "comments removal after replacement should keep worksheet relationships readable");
    const auto* comments_relationship = worksheet_relationships->find_by_id("rId1");
    check(comments_relationship != nullptr,
        "comments removal after replacement should keep inbound comments relationship id");
    check(comments_relationship->target == "../comments/comment1.xml",
        "comments removal after replacement should not rewrite inbound comments target");
    check(comments_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "comments removal after replacement should keep inbound comments target mode");
    check(output_reader.relationships_for(comments_part) == nullptr,
        "comments removal after replacement should not keep owner relationships for absent comments");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-comments")) {
            test_package_editor_replaces_comments_and_preserves_worksheet_links();
            test_package_editor_repeated_comments_replacement_updates_final_state();
            test_package_editor_removes_comments_and_preserves_worksheet_links();
            test_package_editor_comments_replacement_restores_prior_removal();
            test_package_editor_comments_removal_overrides_prior_replacement();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
