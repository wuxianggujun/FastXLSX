#include "test_package_editor_preservation_comments_common.hpp"

void test_package_editor_replaces_persons_and_preserves_threaded_comments_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-replace-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-replace-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string replacement_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Patched Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="patched@example.invalid"/>)"
        R"(</personList>)";
    replace_part_with_memory_chunks(editor, persons_part, replacement_persons,
        "persons local-DOM rewrite");

    const auto* persons_plan = editor.edit_plan().find_part(persons_part);
    check(persons_plan != nullptr,
        "persons replacement should be present in the edit plan");
    check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "persons replacement should be local-DOM-rewrite");
    check_manifest_write_mode(editor, persons_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "persons replacement should mirror write mode into manifest");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons replacement should keep unknown part copy-original");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
    check(output_reader.read_entry("xl/persons/person.xml") == replacement_persons,
        "persons replacement should write replacement persons XML");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "persons replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "persons replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "persons replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "persons replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "persons replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "persons replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "persons replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "persons replacement should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "persons replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "persons replacement should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "persons replacement should keep persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "persons replacement should keep persons relationship target");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "persons replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "persons replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "persons replacement should keep legacy comments relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep persons relationship after persons replacement");
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after persons replacement");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "persons replacement should not invent persons owner relationships");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "persons replacement should keep persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "persons replacement should keep threaded comments content type override");
}

void test_package_editor_repeated_persons_replacement_updates_final_state()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-repeat-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-repeat-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    const std::string stale_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Stale Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="stale@example.invalid"/>)"
        R"(</personList>)";
    const std::string final_persons =
        R"(<personList xmlns="http://schemas.microsoft.com/office/spreadsheetml/2018/threadedcomments">)"
        R"(<person displayName="Final Person" id="{22222222-2222-2222-2222-222222222222}" providerId="None" userId="final@example.invalid"/>)"
        R"(</personList>)";

    replace_part_with_memory_chunks(editor, persons_part, stale_persons,
        "stale repeated persons local-DOM rewrite");
    replace_part_with_memory_chunks(editor, persons_part, final_persons,
        "final repeated persons local-DOM rewrite");

    const auto* persons_plan = editor.edit_plan().find_part(persons_part);
    check(persons_plan != nullptr,
        "repeated persons replacement should keep an active edit-plan part");
    check(persons_plan->write_mode == fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated persons replacement should keep final local-DOM-rewrite mode");
    check(persons_plan->reason.find("final repeated") != std::string::npos,
        "repeated persons replacement should keep final reason");
    check(persons_plan->reason.find("stale repeated") == std::string::npos,
        "repeated persons replacement should drop stale reason");
    check_manifest_write_mode(editor, persons_part,
        fastxlsx::detail::PartWriteMode::StreamRewrite,
        "repeated persons replacement should mirror final write mode into manifest");
    check(editor.manifest().content_types().override_for(persons_part) != nullptr,
        "repeated persons replacement should keep content type override");
    check(editor.edit_plan().find_removed_part(persons_part) == nullptr,
        "repeated persons replacement should not leave removed-part audit");
    check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement should not leave owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement should not invent owner relationships audit");
    check(editor.edit_plan().find_package_entry("[Content_Types].xml") == nullptr,
        "repeated persons replacement should not rewrite content types audit");
    check(editor.edit_plan().find_package_entry("xl/_rels/workbook.xml.rels") == nullptr,
        "repeated persons replacement should not rewrite workbook relationships");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "repeated persons replacement should keep unknown copy-original");

    const fastxlsx::detail::PackageEditorOutputPlan output_plan = editor.planned_output();
    check(!output_plan.full_calculation_on_load,
        "repeated persons replacement output plan should not request full calculation");
    check(output_plan.calc_chain_action == fastxlsx::detail::CalcChainAction::Preserve,
        "repeated persons replacement output plan should preserve calcChain policy");
    check(output_plan.notes.empty(),
        "repeated persons replacement output plan should not invent audit notes");
    check(output_plan.relationship_target_audits.empty(),
        "repeated persons replacement output plan should not invent dependency audits");
    check(output_plan.removed_parts.empty(),
        "repeated persons replacement output plan should not expose removed parts");
    check(output_plan.removed_package_entries.empty(),
        "repeated persons replacement output plan should not omit package entries");
    check_output_entry_plan(output_plan.entries, "xl/persons/person.xml",
        fastxlsx::detail::PartWriteMode::StreamRewrite, true, false, false, false,
        "repeated persons replacement output plan should rewrite persons");
    const auto* output_persons_plan =
        find_output_entry_plan(output_plan.entries, "xl/persons/person.xml");
    check(output_persons_plan->reason.find("final repeated") != std::string::npos,
        "repeated persons replacement output plan should keep final reason");
    check(output_persons_plan->reason.find("stale repeated") == std::string::npos,
        "repeated persons replacement output plan should drop stale reason");
    check(find_output_entry_plan(output_plan.entries, "xl/persons/_rels/person.xml.rels")
            == nullptr,
        "repeated persons replacement output plan should not invent owner relationships");
    check_output_entry_plan(output_plan.entries, "[Content_Types].xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve content types");
    check_output_entry_plan(output_plan.entries, "_rels/.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve package relationships");
    check_output_entry_plan(output_plan.entries, "xl/workbook.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve workbook");
    check_output_entry_plan(output_plan.entries, "xl/_rels/workbook.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve workbook relationships");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/sheet1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve worksheet");
    check_output_entry_plan(output_plan.entries, "xl/worksheets/_rels/sheet1.xml.rels",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve worksheet relationships");
    check_output_entry_plan(output_plan.entries, "xl/comments/comment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve legacy comments");
    check_output_entry_plan(output_plan.entries, "xl/threadedComments/threadedComment1.xml",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve threaded comments");
    check_output_entry_plan(output_plan.entries, "custom/opaque.bin",
        fastxlsx::detail::PartWriteMode::CopyOriginal, true, false, true, false,
        "repeated persons replacement output plan should preserve unknown entry");

    editor.save_as(output);

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check_preserved_source_entries(editor.reader(), output_reader, persons_part.zip_path());
    check(output_reader.read_entry("xl/persons/person.xml") == final_persons,
        "repeated persons replacement should write final persons payload");
    check(output_reader.read_entry("[Content_Types].xml") == source.content_types,
        "repeated persons replacement should preserve content types bytes");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "repeated persons replacement should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "repeated persons replacement should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "repeated persons replacement should preserve workbook relationships bytes");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "repeated persons replacement should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "repeated persons replacement should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "repeated persons replacement should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "repeated persons replacement should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "repeated persons replacement should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "repeated persons replacement should keep workbook relationships readable");
    check(workbook_relationships->find_by_id("rIdPerson") != nullptr,
        "repeated persons replacement should keep persons relationship");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "repeated persons replacement should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "repeated persons replacement should keep threaded comments relationship");
    check(worksheet_relationships->find_by_id("rIdLegacy") != nullptr,
        "repeated persons replacement should keep legacy comments relationship");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "repeated persons replacement should not invent owner relationships");
    check(output_reader.content_types().override_for(persons_part) != nullptr,
        "repeated persons replacement should keep persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "repeated persons replacement should keep threaded comments content type override");
}

void test_package_editor_removes_persons_and_preserves_threaded_comments_links()
{
    const ThreadedCommentsSourcePackage source =
        write_threaded_comments_source_package(
            "fastxlsx-package-editor-remove-persons-source.xlsx");
    const std::filesystem::path output =
        output_path("fastxlsx-package-editor-remove-persons-output.xlsx");

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName comments_part("/xl/comments/comment1.xml");
    const fastxlsx::detail::PartName threaded_comments_part(
        "/xl/threadedComments/threadedComment1.xml");
    const fastxlsx::detail::PartName persons_part("/xl/persons/person.xml");
    const fastxlsx::detail::PartName unknown_part("/custom/opaque.bin");

    editor.remove_part(persons_part, "explicit persons part removal");

    check(editor.edit_plan().find_part(persons_part) == nullptr,
        "persons removal should clear the active edit-plan part");
    const auto* removed_persons = editor.edit_plan().find_removed_part(persons_part);
    check(removed_persons != nullptr,
        "persons removal should record removed-part audit");
    check(removed_persons->reason.find("persons part") != std::string::npos,
        "persons removal should retain the removal reason");
    check(removed_persons->reason.find("inbound relationship preserved")
            != std::string::npos,
        "persons removal should audit preserved inbound relationships");
    check(removed_persons->inbound_relationships.size() == 1,
        "persons removal should keep structured workbook inbound audit");
    const auto& persons_inbound = removed_persons->inbound_relationships.front();
    check(persons_inbound.owner_part == workbook_part.value(),
        "persons removal should keep inbound workbook owner part");
    check(persons_inbound.owner_entry == "xl/_rels/workbook.xml.rels",
        "persons removal should keep inbound workbook relationships entry");
    check(persons_inbound.relationship_id == "rIdPerson",
        "persons removal should keep inbound relationship id");
    check(persons_inbound.relationship_type
            == "http://schemas.microsoft.com/office/2017/10/relationships/person",
        "persons removal should keep inbound relationship type");
    check(persons_inbound.relationship_target == "persons/person.xml",
        "persons removal should keep inbound raw target");
    check(persons_inbound.target_part == persons_part,
        "persons removal should keep normalized target part");
    check(editor.manifest().find_part(persons_part) == nullptr,
        "persons removal should remove the part from the manifest");
    check(editor.manifest().content_types().override_for(persons_part) == nullptr,
        "persons removal should remove the manifest content type override");
    const auto* content_types_entry =
        editor.edit_plan().find_package_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "persons removal should record content types rewrite");
    check(content_types_entry->write_mode == fastxlsx::detail::PartWriteMode::LocalDomRewrite,
        "persons removal content types rewrite should be local-DOM-rewrite");
    check(editor.edit_plan().find_removed_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons removal should not invent owner relationships omission");
    check(editor.edit_plan().find_package_entry("xl/persons/_rels/person.xml.rels")
            == nullptr,
        "persons removal should not invent owner relationships audit");
    check(editor.edit_plan().find_part(workbook_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep workbook copy-original");
    check(editor.edit_plan().find_part(worksheet_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep worksheet copy-original");
    check(editor.edit_plan().find_part(comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep legacy comments copy-original");
    check(editor.edit_plan().find_part(threaded_comments_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep threaded comments copy-original");
    check(editor.edit_plan().find_part(unknown_part)->write_mode
            == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "persons removal should keep unknown part copy-original");

    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    check(entries.find("xl/persons/person.xml") == entries.end(),
        "persons removal output should omit persons part");
    check(entries.find("xl/threadedComments/threadedComment1.xml") != entries.end(),
        "persons removal output should keep threaded comments part");
    check(entries.find("xl/comments/comment1.xml") != entries.end(),
        "persons removal output should keep legacy comments part");
    check(entries.find("xl/persons/_rels/person.xml.rels") == entries.end(),
        "persons removal output should not invent owner relationships omission");

    const fastxlsx::detail::PackageReader output_reader =
        fastxlsx::detail::PackageReader::open(output);
    check(output_reader.content_types().override_for(persons_part) == nullptr,
        "persons removal output should remove persons content type override");
    check(output_reader.content_types().override_for(threaded_comments_part) != nullptr,
        "persons removal output should keep threaded comments content type override");
    const std::string output_content_types = output_reader.read_entry("[Content_Types].xml");
    check_not_contains(output_content_types, "/xl/persons/person.xml",
        "persons removal content types XML should omit persons override");
    check(output_reader.read_entry("_rels/.rels") == source.package_relationships,
        "persons removal should preserve package relationships bytes");
    check(output_reader.read_entry("xl/workbook.xml") == source.workbook,
        "persons removal should preserve workbook bytes");
    check(output_reader.read_entry("xl/_rels/workbook.xml.rels")
            == source.workbook_relationships,
        "persons removal should not prune inbound workbook relationships");
    check(output_reader.read_entry("xl/worksheets/sheet1.xml") == source.worksheet,
        "persons removal should preserve worksheet bytes");
    check(output_reader.read_entry("xl/worksheets/_rels/sheet1.xml.rels")
            == source.worksheet_relationships,
        "persons removal should preserve worksheet relationships bytes");
    check(output_reader.read_entry("xl/comments/comment1.xml") == source.comments,
        "persons removal should preserve legacy comments bytes");
    check(output_reader.read_entry("xl/threadedComments/threadedComment1.xml")
            == source.threaded_comments,
        "persons removal should preserve threaded comments bytes");
    check(output_reader.read_entry("custom/opaque.bin") == source.unknown,
        "persons removal should preserve unknown bytes");

    const auto* workbook_relationships = output_reader.relationships_for(workbook_part);
    check(workbook_relationships != nullptr,
        "persons removal should keep workbook relationships readable");
    const auto* persons_relationship = workbook_relationships->find_by_id("rIdPerson");
    check(persons_relationship != nullptr,
        "persons removal should keep inbound persons relationship id");
    check(persons_relationship->target == "persons/person.xml",
        "persons removal should not rewrite inbound persons target");
    const auto* worksheet_relationships = output_reader.relationships_for(worksheet_part);
    check(worksheet_relationships != nullptr,
        "persons removal should keep worksheet relationships readable");
    check(worksheet_relationships->find_by_id("rIdThreaded") != nullptr,
        "persons removal should keep threaded comments relationship");

    const fastxlsx::detail::RelationshipGraph output_graph =
        output_reader.relationship_graph();
    check(output_graph.relationships_for(workbook_part)->find_by_id("rIdPerson")
            != nullptr,
        "relationship graph should keep inbound persons relationship after removal");
    check(output_graph.relationships_for(worksheet_part)->find_by_id("rIdThreaded")
            != nullptr,
        "relationship graph should keep threaded comments relationship after persons removal");
    check(output_reader.relationships_for(persons_part) == nullptr,
        "persons removal should not keep owner relationships for absent part");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        const std::string_view shard = package_editor_shard_from_args(argc, argv);
        std::cout << "fastxlsx.package_editor preservation shard: " << shard << '\n';

        if (should_run_package_editor_shard(shard, "preservation-comments-persons")) {
            test_package_editor_replaces_persons_and_preserves_threaded_comments_links();
            test_package_editor_repeated_persons_replacement_updates_final_state();
            test_package_editor_removes_persons_and_preserves_threaded_comments_links();
        }
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
