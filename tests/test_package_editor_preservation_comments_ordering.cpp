#include "test_package_editor_preservation_comments_common.hpp"

void test_package_editor_threaded_comments_same_path_ordering()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-replace-after-remove-threaded-comments-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-replace-after-remove-threaded-comments-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.remove_part(threaded_comments_part, "temporary threaded comments removal");
        check(editor.edit_plan().find_removed_part(threaded_comments_part) != nullptr,
            "setup should record removed threaded comments before replacement restore");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "setup should not invent threaded comments owner relationships omission");
        check(editor.manifest().find_part(threaded_comments_part) == nullptr,
            "setup should remove threaded comments from manifest before replacement restore");

        const std::string restored_threaded_comments =
            R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<threadedComment ref="A1" id="{aaaaaaaa-1111-2222-3333-bbbbbbbbbbbb}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T02:00:00Z">)"
            R"(<text>Restored threaded comment</text>)"
            R"(</threadedComment>)"
            R"(</ThreadedComments>)";
        replace_part_with_memory_chunks(editor, threaded_comments_part, restored_threaded_comments,
            "restored threaded comments after removal");

        check(editor.edit_plan().find_removed_part(threaded_comments_part) == nullptr,
            "threaded comments replacement after removal should clear stale removed-part audit");
        const auto* threaded_plan = editor.edit_plan().find_part(threaded_comments_part);
        check(threaded_plan != nullptr,
            "threaded comments replacement after removal should restore active edit-plan part");
        check(threaded_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "threaded comments replacement after removal should keep final write mode");
        check(threaded_plan->reason.find("after removal") != std::string::npos,
            "threaded comments replacement after removal should keep final replacement reason");
        check_manifest_write_mode(editor, threaded_comments_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "threaded comments replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(threaded_comments_part) != nullptr,
            "threaded comments replacement after removal should restore content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "threaded comments replacement after removal should keep content types audit");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should restore source content types audit");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not invent owner omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not invent owner audit");
        check(editor.edit_plan().find_package_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal should not rewrite inbound worksheet relationships");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep legacy comments copy-original");
        check(editor.edit_plan().find_part(persons_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep persons copy-original");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments replacement after removal should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "threaded comments replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "threaded comments replacement after removal output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "threaded comments replacement after removal output plan should not invent dependency audits");
        check(output_plan.removed_parts.empty(),
            "threaded comments replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "threaded comments replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "threaded comments replacement after removal output plan should rewrite threaded comments");
        check_output_entry_part_context(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", true, threaded_comments_part.value(),
            "threaded comments replacement after removal output plan should classify rewritten threaded comments");
        const auto* output_threaded_plan = find_output_entry_plan(
            output_plan.entries, "xl/threadedComments/threadedComment1.xml");
        check(output_threaded_plan->reason.find("after removal") != std::string::npos,
            "threaded comments replacement after removal output plan should keep replacement reason");
        check(find_output_entry_plan(
                  output_plan.entries, "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments replacement after removal output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "threaded comments replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "threaded comments replacement after removal output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "_rels/.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve package relationships");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve workbook");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve worksheet");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve inbound worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve legacy comments");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve persons");
        check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments replacement after removal output plan should preserve unknown entry");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/threadedComments/threadedComment1.xml") != entries.end(),
            "threaded comments replacement after removal output should restore threaded comments");
        check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
                == entries.end(),
            "threaded comments replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(
            editor.reader(), output_reader, threaded_comments_part.zip_path());
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == restored_threaded_comments,
            "threaded comments replacement after removal should write restored bytes");
        check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
            "threaded comments replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == source.worksheet_relationships,
            "threaded comments replacement after removal should preserve worksheet relationships");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "threaded comments replacement after removal should preserve legacy comments");
        check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
            "threaded comments replacement after removal should preserve persons");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "threaded comments replacement after removal should preserve unknown bytes");

        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "threaded comments replacement after removal should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "threaded comments replacement after removal should keep threaded inbound relationship");
        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "threaded comments replacement after removal should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "threaded comments replacement after removal should keep persons relationship");
        check(output_reader.relationships_for(threaded_comments_part) == nullptr,
            "threaded comments replacement after removal should not create owner relationships");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "threaded comments replacement after removal should restore threaded content type");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "threaded comments replacement after removal should keep persons content type");
    }

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-remove-after-replace-threaded-comments-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-remove-after-replace-threaded-comments-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        const std::string stale_threaded_comments =
            R"(<ThreadedComments xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<threadedComment ref="A1" id="{bbbbbbbb-1111-2222-3333-aaaaaaaaaaaa}" personId="{22222222-2222-2222-2222-222222222222}" dT="2026-06-08T03:00:00Z">)"
            R"(<text>Stale threaded comment</text>)"
            R"(</threadedComment>)"
            R"(</ThreadedComments>)";
        replace_part_with_memory_chunks(editor, threaded_comments_part, stale_threaded_comments,
            "stale threaded comments replacement before removal");
        check(editor.edit_plan().find_part(threaded_comments_part) != nullptr,
            "setup should record active threaded comments replacement before removal");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "setup threaded comments replacement should not invent owner audit");

        editor.remove_part(threaded_comments_part,
            "explicit threaded comments removal after replacement");

        check(editor.edit_plan().find_part(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should clear active replacement");
        const auto* removed_threaded =
            editor.edit_plan().find_removed_part(threaded_comments_part);
        check(removed_threaded != nullptr,
            "threaded comments removal after replacement should record removed-part audit");
        check(removed_threaded->reason.find("after replacement") != std::string::npos,
            "threaded comments removal after replacement should keep final reason");
        check(removed_threaded->inbound_relationships.size() == 1,
            "threaded comments removal after replacement should keep inbound audit");
        check(removed_threaded->inbound_relationships.front().target_part
                == threaded_comments_part,
            "threaded comments removal after replacement should keep inbound target part");
        check(editor.manifest().find_part(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "threaded comments removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "threaded comments removal after replacement content types should be local-DOM");
        check(editor.edit_plan().find_removed_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement should not invent owner omission");
        check(editor.edit_plan().find_package_entry(
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement should clear owner audit");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep legacy comments");
        check(editor.edit_plan().find_part(persons_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep persons");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "threaded comments removal after replacement should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "threaded comments removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "threaded comments removal after replacement output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "threaded comments removal after replacement output plan should not invent dependency audits");
        check(output_plan.removed_parts.size() == 1,
            "threaded comments removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == threaded_comments_part,
            "threaded comments removal after replacement output plan should expose removed threaded comments part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "threaded comments removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "threaded comments removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "threaded comments removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "threaded comments removal after replacement output plan should omit threaded comments");
        check_output_entry_part_context(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", true,
            threaded_comments_part.value(),
            "threaded comments removal after replacement output plan should classify omitted threaded comments");
        const auto* output_threaded_plan =
            find_output_entry_plan(output_plan.entries,
                "xl/threadedComments/threadedComment1.xml");
        check(output_threaded_plan->reason.find("after replacement") != std::string::npos,
            "threaded comments removal after replacement output plan should keep final removal reason");
        check(output_threaded_plan->inbound_relationships.size() == 1,
            "threaded comments removal after replacement output plan should expose inbound audit");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml", worksheet_part.value(),
            "xl/worksheets/_rels/sheet1.xml.rels", "rIdThreaded",
            "http://schemas.microsoft.com/office/2017/10/relationships/threadedComment",
            "../threadedComments/threadedComment1.xml", threaded_comments_part,
            "threaded comments removal after replacement output plan should keep worksheet inbound audit");
        check(find_output_entry_plan(output_plan.entries,
                  "xl/threadedComments/_rels/threadedComment1.xml.rels")
                == nullptr,
            "threaded comments removal after replacement output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "threaded comments removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "threaded comments removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "threaded comments removal after replacement output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve inbound worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve persons workbook relationship");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "threaded comments removal after replacement output plan should preserve persons part");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/threadedComments/threadedComment1.xml") == entries.end(),
            "threaded comments removal after replacement output should omit threaded comments");
        check(entries.find("xl/threadedComments/_rels/threadedComment1.xml.rels")
                == entries.end(),
            "threaded comments removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.content_types().override_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should remove threaded content type");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "threaded comments removal after replacement should keep persons content type");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/threadedComments/threadedComment1.xml",
            "threaded comments removal after replacement content types should omit threaded override");
        check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
                == source.worksheet_relationships,
            "threaded comments removal after replacement should preserve worksheet relationships");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "threaded comments removal after replacement should preserve legacy comments");
        check(output_reader.read_entry("xl/persons/person.xml") == source.persons,
            "threaded comments removal after replacement should preserve persons");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "threaded comments removal after replacement should preserve unknown bytes");

        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "threaded comments removal after replacement should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "threaded comments removal after replacement should keep inbound relationship");
        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "threaded comments removal after replacement should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "threaded comments removal after replacement should keep persons relationship");
        check(output_reader.relationships_for(threaded_comments_part) == nullptr,
            "threaded comments removal after replacement should not keep owner relationships");
    }
}

void test_package_editor_persons_same_path_ordering()
{
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-replace-after-remove-persons-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-replace-after-remove-persons-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        editor.remove_part(persons_part, "temporary persons removal");
        check(editor.edit_plan().find_removed_part(persons_part) != nullptr,
            "setup should record removed persons before replacement restore");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "setup should not invent persons owner relationships omission");
        check(editor.manifest().find_part(persons_part) == nullptr,
            "setup should remove persons from manifest before replacement restore");

        const std::string restored_persons =
            R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<person displayName="Restored Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="restored@example.invalid"/>)"
            R"(</personList>)";
        replace_part_with_memory_chunks(editor, persons_part, restored_persons,
            "restored persons after removal");

        check(editor.edit_plan().find_removed_part(persons_part) == nullptr,
            "persons replacement after removal should clear stale removed-part audit");
        const auto* persons_plan = editor.edit_plan().find_part(persons_part);
        check(persons_plan != nullptr,
            "persons replacement after removal should restore active edit-plan part");
        check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
            "persons replacement after removal should keep final write mode");
        check(persons_plan->reason.find("after removal") != std::string::npos,
            "persons replacement after removal should keep final replacement reason");
        check_manifest_write_mode(editor, persons_part,
            fastxlsx::detail::PartWriteMode::StreamRewrite,
            "persons replacement after removal should restore manifest write mode");
        check(editor.manifest().content_types().override_for(persons_part) != nullptr,
            "persons replacement after removal should restore content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "persons replacement after removal should keep content types audit");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should restore source content types audit");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal should not invent owner omission");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal should not invent owner audit");
        check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
            "persons replacement after removal should not rewrite inbound workbook relationships");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep legacy comments copy-original");
        check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep threaded comments copy-original");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons replacement after removal should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "persons replacement after removal output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "persons replacement after removal output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "persons replacement after removal output plan should not invent dependency audits");
        check(output_plan.removed_parts.empty(),
            "persons replacement after removal output plan should clear removed parts");
        check(output_plan.removed_package_entries.empty(),
            "persons replacement after removal output plan should clear removed package entries");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
            "persons replacement after removal output plan should rewrite persons");
        check_output_entry_part_context(output_plan.entries, "xl/persons/person.xml",
            true, persons_part.value(),
            "persons replacement after removal output plan should classify rewritten persons");
        const auto* output_persons_plan =
            find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
        check(output_persons_plan->reason.find("after removal") != std::string::npos,
            "persons replacement after removal output plan should keep replacement reason");
        check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons replacement after removal output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "persons replacement after removal output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "persons replacement after removal output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "_rels/.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve package relationships");
        check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve workbook");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve worksheet");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve legacy comments");
        check_output_entry_plan(output_plan.entries,
            "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve threaded comments");
        check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons replacement after removal output plan should preserve unknown entry");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/persons/person.xml") != entries.end(),
            "persons replacement after removal output should restore persons");
        check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
            "persons replacement after removal output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
        check(output_reader.read_entry("xl/persons/person.xml") == restored_persons,
            "persons replacement after removal should write restored persons bytes");
        check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
            "persons replacement after removal should restore source content types bytes");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == source.workbook_relationships,
            "persons replacement after removal should preserve workbook relationships");
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == source.threaded_comments,
            "persons replacement after removal should preserve threaded comments");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "persons replacement after removal should preserve legacy comments");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "persons replacement after removal should preserve unknown bytes");

        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "persons replacement after removal should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "persons replacement after removal should keep persons inbound relationship");
        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "persons replacement after removal should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "persons replacement after removal should keep threaded relationship");
        check(output_reader.relationships_for(persons_part) == nullptr,
            "persons replacement after removal should not create owner relationships");
        check(output_reader.content_types().override_for(persons_part) != nullptr,
            "persons replacement after removal should restore persons content type");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "persons replacement after removal should keep threaded comments content type");
    }

    {
        const ThreadedCommentsSourcePackage source =
            write_threaded_comments_source_package(
                "fastxlsx-package-editor-remove-after-replace-persons-source.xlsx");
        const std::filesystem::path output =
            output_path("fastxlsx-package-editor-remove-after-replace-persons-output.xlsx");

        fastxlsx::detail::PackageEditor editor =
            fastxlsx::detail::PackageEditor::open(source.path);

        const std::string stale_persons =
            R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
            R"(<person displayName="Stale Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="stale@example.invalid"/>)"
            R"(</personList>)";
        replace_part_with_memory_chunks(editor, persons_part, stale_persons,
            "stale persons replacement before removal");
        check(editor.edit_plan().find_part(persons_part) != nullptr,
            "setup should record active persons replacement before removal");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "setup persons replacement should not invent owner audit");

        editor.remove_part(persons_part, "explicit persons removal after replacement");

        check(editor.edit_plan().find_part(persons_part) == nullptr,
            "persons removal after replacement should clear active replacement");
        const auto* removed_persons = editor.edit_plan().find_removed_part(persons_part);
        check(removed_persons != nullptr,
            "persons removal after replacement should record removed-part audit");
        check(removed_persons->reason.find("after replacement") != std::string::npos,
            "persons removal after replacement should keep final reason");
        check(removed_persons->inbound_relationships.size() == 1,
            "persons removal after replacement should keep inbound audit");
        check(removed_persons->inbound_relationships.front().target_part == persons_part,
            "persons removal after replacement should keep inbound target part");
        check(editor.manifest().find_part(persons_part) == nullptr,
            "persons removal after replacement should remove manifest part");
        check(editor.manifest().content_types().override_for(persons_part) == nullptr,
            "persons removal after replacement should remove content type override");
        const auto* content_types_entry =
            editor.edit_plan().find_package_entry("[Content_Types].xml");
        check(content_types_entry != nullptr,
            "persons removal after replacement should record content types rewrite");
        check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
            "persons removal after replacement content types should be local-DOM");
        check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement should not invent owner omission");
        check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement should clear owner audit");
        check(editor.edit_plan().find_part(workbook_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep workbook copy-original");
        check(editor.edit_plan().find_part(worksheet_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep worksheet copy-original");
        check(editor.edit_plan().find_part(comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep legacy comments");
        check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep threaded comments");
        check(editor.edit_plan().find_part(unknown_part)->write_mode
                == fastxlsx::detail::PartWriteMode::CopyOriginal,
            "persons removal after replacement should keep unknown copy-original");

        const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
        check(!output_plan.full_calculation_on_load,
            "persons removal after replacement output plan should not request full calculation");
        check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
            "persons removal after replacement output plan should keep calcChain preserve state");
        check(output_plan.relationship_target_audits.empty(),
            "persons removal after replacement output plan should not invent dependency audits");
        check(output_plan.removed_parts.size() == 1,
            "persons removal after replacement output plan should expose one removed part");
        check(output_plan.removed_parts.front().part_name == persons_part,
            "persons removal after replacement output plan should expose removed persons part");
        check(output_plan.removed_parts.front().reason.find("after replacement")
                != std::string::npos,
            "persons removal after replacement output plan should keep removed-part reason");
        check(output_plan.removed_parts.front().inbound_relationships.size() == 1,
            "persons removal after replacement output plan should keep removed-part inbound audit");
        check(output_plan.removed_package_entries.empty(),
            "persons removal after replacement output plan should not omit metadata entries");
        check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, false, true,
            "persons removal after replacement output plan should omit persons part");
        check_output_entry_part_context(output_plan.entries, "xl/persons/person.xml",
            true, persons_part.value(),
            "persons removal after replacement output plan should classify omitted persons");
        const auto* output_persons_plan =
            find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
        check(output_persons_plan->reason.find("after replacement") != std::string::npos,
            "persons removal after replacement output plan should keep final removal reason");
        check(output_persons_plan->inbound_relationships.size() == 1,
            "persons removal after replacement output plan should expose inbound audit");
        check_output_entry_has_inbound_relationship(output_plan.entries,
            "xl/persons/person.xml", workbook_part.value(),
            "xl/_rels/workbook.xml.rels", "rIdPerson",
            "http://schemas.microsoft.com/office/2017/10/relationships/person",
            "persons/person.xml", persons_part,
            "persons removal after replacement output plan should keep workbook inbound audit");
        check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
                == nullptr,
            "persons removal after replacement output plan should not invent owner relationships");
        check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
            fastxlsx::detail::PartWriteMode::LocalDomRewrite, true, false, false, false,
            "persons removal after replacement output plan should rewrite content types");
        check_output_entry_part_context(output_plan.entries, "[Content_Types].xml", false,
            "",
            "persons removal after replacement output plan should classify content types as metadata");
        const auto* output_content_types_plan =
            find_output_entry_plan(output_plan.entries, "[Content_Types].xml");
        check(output_content_types_plan->audit_kind
                == fastxlsx::detail::PackageEntryAuditKind::ContentTypes,
            "persons removal after replacement output plan should classify content types metadata");
        check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve inbound workbook relationships");
        check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve worksheet relationships");
        check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
            fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
            "persons removal after replacement output plan should preserve threaded comments");

        editor.save_as(output);

        const auto entries = fastxlsx::test::read_zip_entries(output);
        check(entries.find("xl/persons/person.xml") == entries.end(),
            "persons removal after replacement output should omit persons");
        check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
            "persons removal after replacement output should not invent owner relationships");

        const fastxlsx::detail::PackageReader output_reader =
            fastxlsx::detail::PackageReader::open(output);
        check(output_reader.content_types().override_for(persons_part) == nullptr,
            "persons removal after replacement should remove persons content type");
        check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
            "persons removal after replacement should keep threaded comments content type");
        const std::string output_content_types =
            output_reader.read_entry("[Content_Types].xml");
        check_not_contains(output_content_types, "/xl/persons/person.xml",
            "persons removal after replacement content types should omit persons override");
        check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
                == source.workbook_relationships,
            "persons removal after replacement should preserve workbook relationships");
        check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
                == source.threaded_comments,
            "persons removal after replacement should preserve threaded comments");
        check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
            "persons removal after replacement should preserve legacy comments");
        check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
            "persons removal after replacement should preserve unknown bytes");

        const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
        check(workbook_relationships != nullptr,
            "persons removal after replacement should keep workbook relationships");
        check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
            "persons removal after replacement should keep inbound persons relationship");
        const auto* worksheet_relationships =
            output_reader.relationships_for(worksheet_part);
        check(worksheet_relationships != nullptr,
            "persons removal after replacement should keep worksheet relationships");
        check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
            "persons removal after replacement should keep threaded relationship");
        check(output_reader.relationships_for(persons_part) == nullptr,
            "persons removal after replacement should not keep owner relationships");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-comments-ordering")) {
            test_package_editor_threaded_comments_same_path_ordering();
            test_package_editor_persons_same_path_ordering();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
