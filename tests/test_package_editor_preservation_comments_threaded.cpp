#include "test_package_editor_preservation_comments_common.hpp"

void test_package_editor_replaces_threaded_comments_and_preserves_person_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-linked-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-linked-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T01:00:00Z">)"
        R"(<text>Patched threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";
    replace_part_with_memory_chunks(editor, threaded_comments_part, replacement_threaded_comments,
        "threaded comments local-DOM rewrite");

    const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
    check(threaded_plan != nullptr,
        "threaded comments replacement should be present in the edit plan");
    check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "threaded comments replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, threaded_comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "threaded comments replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments replacement should keep unknown part copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "threaded comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "threaded comments replacement output plan should keep calcChain preserve state");
    check(output_plan.notes.empty(),
        "threaded comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "threaded comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "threaded comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "threaded comments replacement output plan should not expose removed package entries");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "threaded comments replacement output plan should rewrite threaded comments part");
    check_output_entry_part_context(output_plan.entries,
        "xl/threadedComments/threadedComment1.xml", true, threaded_comments_part.value(),
        "threaded comments replacement output plan should classify threaded comments part");
    const auto* output_threaded_comments_plan =
        find_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml");
    check(output_threaded_comments_plan->reason.find("threaded comments") != std::string::npos,
        "threaded comments replacement output plan should keep replacement reason");
    check(find_output_entry_plan(
              output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve content types");
    check_output_entry_part_context(output_plan.entries, "[Content_Types].xml",
        false, "",
        "threaded comments replacement output plan should classify content types as metadata");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve persons");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "threaded comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, threaded_comments_part.zip_path());
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == replacement_threaded_comments,
        "threaded comments replacement should write replacement threaded comments XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "threaded comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "threaded comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "threaded comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "threaded comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "threaded comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "threaded comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "threaded comments replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "threaded comments replacement should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "threaded comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "threaded comments replacement should keep worksheet relationships readable");
    const auto* threaded_relationship =
        worksheet_relationships->find_by_id("rIdThreaded");
    check(threaded_relationship != nullptr,
        "threaded comments replacement should keep threaded comments relationship id");
    check(threaded_relationship->target == "../threadedComments/threadedComment1.xml",
        "threaded comments replacement should keep threaded comments relationship target");
    check(threaded_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "threaded comments replacement should keep threaded comments target mode");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "threaded comments replacement should keep legacy comments relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "threaded comments replacement should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "threaded comments replacement should keep persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "threaded comments replacement should keep persons relationship target");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after replacement");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after threaded comments replacement");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "threaded comments replacement should not invent threaded comments owner relationships");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "threaded comments replacement should keep threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "threaded comments replacement should keep persons content type override");
}

void test_package_editor_repeated_threaded_comments_replacement_updates_final_state()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-repeat-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-000000000001}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T03:00:00Z">)"
        R"(<text>Stale threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";
    const std::string final_threaded_comments =
        R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<threadedComment ref="A1" id="{aaaaaaaa-bbbb-cccc-dddd-000000000002}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T04:00:00Z">)"
        R"(<text>Final threaded comment</text>)"
        R"(</threadedComment>)"
        R"(</ThreadedComments>)";

    replace_part_with_memory_chunks(editor, threaded_comments_part, stale_threaded_comments,
        "stale repeated threaded comments local-DOM rewrite");
    replace_part_with_memory_chunks(editor, threaded_comments_part, final_threaded_comments,
        "final repeated threaded comments local-DOM rewrite");

    const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
    check(threaded_plan != nullptr,
        "repeated threaded comments replacement should keep an active edit-plan part");
    check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated threaded comments replacement should keep final local-DOM-rewrite mode");
    check(threaded_plan->reason.find("final repeated") != std::string::npos,
        "repeated threaded comments replacement should keep final reason");
    check(threaded_plan->reason.find("stale repeated") == std::string::npos,
        "repeated threaded comments replacement should drop stale reason");
    check_manifest_write_mode(editor, threaded_comments_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated threaded comments replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(threaded_comments_part) != nullptr,
        "repeated threaded comments replacement should keep content type override");
    check(editor.edit_plan().find_removed_part(threaded_comments_part) == nullptr,
        "repeated threaded comments replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated threaded comments replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement should not rewrite worksheet relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated threaded comments replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated threaded comments replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated threaded comments replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated threaded comments replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated threaded comments replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated threaded comments replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated threaded comments replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated threaded comments replacement output plan should rewrite threaded comments");
    const auto* output_threaded_plan =
        find_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml");
    check(output_threaded_plan->reason.find("final repeated") != std::string::npos,
        "repeated threaded comments replacement output plan should keep final reason");
    check(output_threaded_plan->reason.find("stale repeated") == std::string::npos,
        "repeated threaded comments replacement output plan should drop stale reason");
    check(find_output_entry_plan(
              output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "repeated threaded comments replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve persons");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated threaded comments replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(
        editor.reader(), output_reader, threaded_comments_part.zip_path());
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == final_threaded_comments,
        "repeated threaded comments replacement should write final threaded comments payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated threaded comments replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated threaded comments replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated threaded comments replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated threaded comments replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated threaded comments replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated threaded comments replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "repeated threaded comments replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "repeated threaded comments replacement should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated threaded comments replacement should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated threaded comments replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "repeated threaded comments replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "repeated threaded comments replacement should keep legacy comments relationship");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated threaded comments replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "repeated threaded comments replacement should keep persons relationship");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "repeated threaded comments replacement should not invent owner relationships");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "repeated threaded comments replacement should keep threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "repeated threaded comments replacement should keep persons content type override");
}

void test_package_editor_removes_threaded_comments_and_preserves_person_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-remove-threaded-comments-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-threaded-comments-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(threaded_comments_part, "explicit threaded comments part removal");

    check(editor.edit_plan().find_part(threaded_comments_part) == nullptr,
        "threaded comments removal should clear the active edit-plan part");
    const auto* removed_threaded =
        editor.edit_plan().find_removed_part(threaded_comments_part);
    check(removed_threaded != nullptr,
        "threaded comments removal should record removed-part audit");
    check(removed_threaded->reason.find("threaded comments part") != std::string::npos,
        "threaded comments removal should retain the removal reason");
    check(removed_threaded->reason.find("inbound relationship preserved")
            != std::string::npos,
        "threaded comments removal should audit preserved inbound relationships");
    check(removed_threaded->inbound_relationships.size() == 1,
        "threaded comments removal should keep structured inbound audit");
    const auto& threaded_inbound = removed_threaded->inbound_relationships.front();
    check(threaded_inbound.owner_part == worksheet_part.value(),
        "threaded comments removal should keep inbound owner part");
    check(threaded_inbound.owner_entry == "xl/worksheets/_rels/sheet1.xml.rels",
        "threaded comments removal should keep inbound owner relationship entry");
    check(threaded_inbound.relationship_id == "rIdThreaded",
        "threaded comments removal should keep inbound relationship id");
    check(threaded_inbound.relationship_type
            == "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment",
        "threaded comments removal should keep inbound relationship type");
    check(threaded_inbound.relationship_target == "../threadedComments/threadedComment1.xml",
        "threaded comments removal should keep inbound raw target");
    check(threaded_inbound.target_part == threaded_comments_part,
        "threaded comments removal should keep normalized target part");
    check(editor.manifest().find_part(threaded_comments_part) == nullptr,
        "threaded comments removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(threaded_comments_part) == nullptr,
        "threaded comments removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "threaded comments removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "threaded comments removal content types rewrite should be local-DOM-rewrite");
    check(content_types_entry->audit_kind
            == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
        "threaded comments removal content types audit should keep structured role");
    check(editor.edit_plan().find_removed_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry(
              "xl/threadedComments/_rels/threadedComment1.xml.rels")
            == nullptr,
        "threaded comments removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(persons_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep persons copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "threaded comments removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/threadedComments/threadedComment1.xml") == entries.end(),
        "threaded comments removal output should omit threaded comments part");
    check(entries.find("xl/worksheets/_rels/sheet1.xml.rels") != entries.end(),
        "threaded comments removal output should keep worksheet relationships");
    check(entries.find("xl/persons/person.xml") != entries.end(),
        "threaded comments removal output should keep persons part");
    check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
            == entries.end(),
        "threaded comments removal output should not invent owner relationships");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(threaded_comments_part) == nullptr,
        "threaded comments removal output should remove threaded comments content type override");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "threaded comments removal output should keep persons content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/threadedComments/threadedComment1.xml",
        "threaded comments removal content types XML should omit threaded comments override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "threaded comments removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "threaded comments removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "threaded comments removal should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "threaded comments removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "threaded comments removal should not prune inbound worksheet relationships");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "threaded comments removal should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
        "threaded comments removal should preserve persons bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "threaded comments removal should preserve unknown bytes");

    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "threaded comments removal should keep worksheet relationships readable");
    const auto* threaded_relationship =
        worksheet_relationships->find_by_id("rIdThreaded");
    check(threaded_relationship != nullptr,
        "threaded comments removal should keep inbound threaded comments relationship id");
    check(threaded_relationship->target == "../threadedComments/threadedComment1.xml",
        "threaded comments removal should not rewrite inbound threaded comments target");
    check(threaded_relationship->target_mode
            == fastxlsx::detail::Relationship::TargetMode::Internal,
        "threaded comments removal should keep inbound threaded comments target mode");
    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "threaded comments removal should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "threaded comments removal should keep persons relationship");
    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep inbound threaded comments relationship after removal");
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after threaded comments removal");
    check(output_reader.relationships_for(threaded_comments_part) == nullptr,
        "threaded comments removal should not keep owner relationships for absent part");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-comments-threaded")) {
            test_package_editor_replaces_threaded_comments_and_preserves_person_links();
            test_package_editor_repeated_threaded_comments_replacement_updates_final_state();
            test_package_editor_removes_threaded_comments_and_preserves_person_links();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
