#include "test_workbook_editor_source_success_common.hpp"

void test_public_worksheet_editor_defers_source_shared_strings_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-dirty-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-missing-target-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(2.0),
            fastxlsx::CellView::boolean(true),
            fastxlsx::CellView::formula("A1+1")});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("requires-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    check(entries.find("xl/sharedStrings.xml") != entries.end(),
        "lazy sharedStrings fixture should contain a sharedStrings part for the second sheet");
    check_not_contains(entries.at("xl/worksheets/sheet1.xml"), R"(t="s")",
        "lazy sharedStrings fixture Data sheet should not contain shared string indexes");
    check_contains(entries.at("xl/worksheets/sheet2.xml"), R"(t="s")",
        "lazy sharedStrings fixture Shared sheet should contain shared string indexes");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");

    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(Target="sharedStrings.xml")",
        R"(Target="missingSharedStrings.xml")");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 2.0,
        "WorksheetEditor should materialize non-shared-string numbers without loading sharedStrings");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Boolean
            && b1->boolean_value(),
        "WorksheetEditor should materialize non-shared-string booleans without loading sharedStrings");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Formula
            && c1->text_value() == "A1+1",
        "WorksheetEditor should materialize formulas without loading sharedStrings");
    check(!sheet.has_pending_changes(),
        "lazy sharedStrings non-index materialization should start clean");
    check(!editor.has_pending_changes(),
        "lazy sharedStrings non-index materialization should not dirty the editor");

    sheet.set_cell("D1", fastxlsx::CellValue::text("inline-after-lazy-sharedStrings"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>inline-after-lazy-sharedStrings</t></is></c>)",
        "dirty lazy sharedStrings projection should still write new text as inlineStr");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty lazy sharedStrings projection should not introduce shared string indexes");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy sharedStrings projection should preserve the source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"),
        R"(Target="missingSharedStrings.xml")",
        "dirty lazy sharedStrings projection should not repair the stale workbook relationship");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy sharedStrings materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship targets an unknown package part",
        "usable-after-lazy-missing-sharedstrings-target",
        "lazy missing sharedStrings target",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-duplicate-rel-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(7.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("duplicate-rel-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& workbook_relationships = entries.at("xl/_rels/workbook.xml.rels");
    replace_first_or_throw(workbook_relationships,
        R"(</Relationships>)",
        R"(<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)"
        R"(</Relationships>)");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 7.0,
        "duplicate sharedStrings relationships should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy duplicate sharedStrings relationship read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-duplicate-rel-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy duplicate-rel projection should preserve source sharedStrings bytes");
    check_contains(output_entries.at("xl/_rels/workbook.xml.rels"), R"(Id="rId99")",
        "dirty lazy duplicate-rel projection should preserve duplicate relationship bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-duplicate-rel-lazy-load</t></is></c>)",
        "dirty lazy duplicate-rel projection should still write new text as inlineStr");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings lookup found multiple sharedStrings relationships",
        "usable-after-lazy-duplicate-sharedstrings-relationship",
        "lazy duplicate sharedStrings relationship",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-malformed-xml-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(11.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("malformed-xml-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    entries.at("xl/sharedStrings.xml") = R"(<notSst/>)";
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 11.0,
        "malformed sharedStrings XML should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy malformed sharedStrings XML read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-malformed-xml-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == R"(<notSst/>)",
        "dirty lazy malformed-xml projection should preserve malformed sharedStrings bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-malformed-xml-lazy-load</t></is></c>)",
        "dirty lazy malformed-xml projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy malformed sharedStrings XML materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "CellStore sharedStrings loader root is missing an sst element",
        "usable-after-lazy-malformed-sharedstrings",
        "lazy malformed sharedStrings XML",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-source.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-output.xlsx");
    const std::filesystem::path failure_recovery_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-lazy-wrong-content-type-failure-recovery-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(13.0)});
        fastxlsx::WorksheetWriter shared = writer.add_worksheet("Shared");
        shared.append_row({fastxlsx::CellView::text("wrong-content-type-needs-sharedStrings")});
        writer.close();
    }

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(source);
    std::string& content_types = entries.at("[Content_Types].xml");
    replace_first_or_throw(content_types,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    const std::string shared_strings_before = entries.at("xl/sharedStrings.xml");
    write_stored_zip_entries(source, entries);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Number
            && a1->number_value() == 13.0,
        "wrong sharedStrings content type should not block a non-index sheet");
    check(!editor.has_pending_changes(),
        "lazy wrong sharedStrings content type read should not dirty the editor");

    sheet.set_cell("B1", fastxlsx::CellValue::text("after-wrong-content-type-lazy-load"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty lazy wrong-content-type projection should preserve sharedStrings bytes");
    check_contains(output_entries.at("[Content_Types].xml"),
        R"(PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")",
        "dirty lazy wrong-content-type projection should preserve wrong content type metadata");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1" t="inlineStr"><is><t>after-wrong-content-type-lazy-load</t></is></c>)",
        "dirty lazy wrong-content-type projection should still write new text as inlineStr");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "lazy wrong sharedStrings content type materialization should not mutate the source package");

    check_public_worksheet_materialization_failure_hygiene(
        source,
        failure_recovery_output,
        "workbook sharedStrings relationship target is not a sharedStrings part",
        "usable-after-lazy-wrong-sharedstrings-content-type",
        "lazy wrong sharedStrings content type",
        "Data",
        "xl/worksheets/sheet1.xml",
        "Shared");
}

void test_public_worksheet_editor_materializes_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-a"),
            fastxlsx::CellView::text("A&B <C>")});
        data.append_row({fastxlsx::CellView::text("shared-a")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-shared")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part for materialization");
    std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    replace_first_or_throw(shared_strings_before, "?><sst",
        "?><?fastxlsx sharedStrings-trivia?>"
        "<?fastxlsx.data-1:probe legal-target?>"
        "<?_fastxlsx legal-start?>"
        "<?:fastxlsx legal-colon-start?>"
        "<?fastxlsx?>"
        "<?xml-stylesheet type=\"text/xsl\" href=\"sharedStrings.xsl\"?><sst");
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_before);
    {
        std::string updated_workbook_rels = source_entries.at("xl/_rels/workbook.xml.rels");
        replace_first_or_throw(updated_workbook_rels,
            R"(Target="sharedStrings.xml")",
            R"(Target="./sharedStrings.xml")");
        rewrite_package_entry_as_stored(
            source, "xl/_rels/workbook.xml.rels", updated_workbook_rels);
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> a2 = sheet.try_cell("A2");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "shared-a",
        "WorksheetEditor should materialize A1 shared string text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "A&B <C>",
        "WorksheetEditor should decode XML entities from source sharedStrings");
    check(a2.has_value() && a2->kind() == fastxlsx::CellValueKind::Text
            && a2->text_value() == "shared-a",
        "WorksheetEditor should materialize repeated shared string indexes");
    check(shared_strings_before.find("<?fastxlsx sharedStrings-trivia?>")
            != std::string::npos,
        "source sharedStrings success fixture should include prolog processing instruction trivia");
    check(shared_strings_before.find("<?fastxlsx.data-1:probe legal-target?>")
            != std::string::npos,
        "source sharedStrings success fixture should include legal PI target continuation trivia");
    check(shared_strings_before.find("<?_fastxlsx legal-start?>") != std::string::npos,
        "source sharedStrings success fixture should include underscore-start PI target trivia");
    check(shared_strings_before.find("<?:fastxlsx legal-colon-start?>") != std::string::npos,
        "source sharedStrings success fixture should include colon-start PI target trivia");
    check(shared_strings_before.find("<?fastxlsx?>") != std::string::npos,
        "source sharedStrings success fixture should include empty-data PI trivia");
    check(shared_strings_before.find("<?xml-stylesheet") != std::string::npos,
        "source sharedStrings success fixture should include xml-stylesheet PI trivia");
    check(shared_strings_before.find(R"(standalone="yes")") != std::string::npos,
        "source sharedStrings success fixture should include legal standalone declaration metadata");
    check(!sheet.has_pending_changes(),
        "read-only source sharedStrings materialization should start clean");
    check(!editor.has_pending_changes(),
        "read-only source sharedStrings materialization should not dirty WorkbookEditor");
    check(editor.pending_change_count() == 0,
        "read-only source sharedStrings materialization should not queue Patch edits");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-output.xlsx");
    sheet.set_cell("C3", fastxlsx::CellValue::text("new-inline"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "flushed WorksheetEditor source shared string should reuse its existing shared string index");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "flushed WorksheetEditor source shared string should keep the decoded table index");
    check_contains(worksheet_xml,
        R"(<c r="A2" t="s"><v>0</v></c>)",
        "flushed WorksheetEditor repeated source text should reuse the same shared string index");
    check_contains(worksheet_xml,
        R"(<c r="C3" t="s"><v>3</v></c>)",
        "new WorksheetEditor text should append to the existing sharedStrings table");
    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<?fastxlsx sharedStrings-trivia?>)",
        "WorksheetEditor sharedStrings append should preserve source prolog trivia");
    check_contains(shared_strings_after,
        R"(<si><t>new-inline</t></si></sst>)",
        "WorksheetEditor sharedStrings append should add the dirty text item before the sst close");
    check_contains(shared_strings_after, R"(count="5")",
        "WorksheetEditor sharedStrings append should advance the conservative count metadata");
    check_contains(shared_strings_after, R"(uniqueCount="4")",
        "WorksheetEditor sharedStrings append should advance uniqueCount metadata");
}

void test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations()
{
    struct LegalDeclarationCase {
        std::string_view name;
        std::string_view declaration;
        std::string_view expected_text;
    };

    const std::array<LegalDeclarationCase, 2> cases{{
        {"single-quoted-version-1-1-with-encoding-and-standalone-no",
            "<?xml version='1.1' encoding='UTF_8-Test.1' standalone='no'?>",
            "legal-declaration-version-1-1"},
        {"version-only-single-quoted",
            "<?xml version='1.0'?>",
            "legal-declaration-version-only"},
    }};

    for (const LegalDeclarationCase& test_case : cases) {
        const std::filesystem::path source = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-source.xlsx");
        const std::filesystem::path output = artifact(
            "fastxlsx-workbook-editor-public-sharedstrings-xml-declaration-"
            + std::string(test_case.name) + "-output.xlsx");
        {
            fastxlsx::WorkbookWriterOptions options;
            options.string_strategy = fastxlsx::StringStrategy::SharedString;
            fastxlsx::WorkbookWriter writer =
                fastxlsx::WorkbookWriter::create(source, options);
            fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
            data.append_row({fastxlsx::CellView::text("declaration-placeholder")});
            fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
            untouched.append_row({fastxlsx::CellView::text("keep-legal-declaration")});
            writer.close();
        }

        const std::string shared_strings_xml =
            std::string(test_case.declaration)
            + R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
              R"(<si><t>)"
            + std::string(test_case.expected_text)
            + R"(</t></si><si><t>keep-legal-declaration</t></si></sst>)";
        rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings_xml);
        const auto source_entries = fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

        const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
        check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
                && a1->text_value() == test_case.expected_text,
            std::string(test_case.name)
                + " should materialize source sharedStrings text");
        check(!sheet.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should start clean");
        check(!editor.has_pending_changes(),
            std::string(test_case.name)
                + " legal declaration materialization should not dirty WorkbookEditor");
        check(editor.pending_change_count() == 0,
            std::string(test_case.name)
                + " legal declaration materialization should not queue Patch edits");

        sheet.set_cell("B2", fastxlsx::CellValue::text("legal-declaration-new-inline"));
        editor.save_as(output);

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml,
            R"(<c r="A1" t="s"><v>0</v></c>)",
            std::string(test_case.name)
                + " dirty projection should reuse materialized shared string index");
        check_contains(worksheet_xml,
            R"(<c r="B2" t="s"><v>2</v></c>)",
            std::string(test_case.name)
                + " dirty projection should append edits beside legal declaration source text");
        check_contains(output_entries.at("xl/sharedStrings.xml"),
            R"(<si><t>legal-declaration-new-inline</t></si></sst>)",
            std::string(test_case.name)
                + " dirty projection should append legal declaration dirty text to sharedStrings");
        check_contains(output_entries.at("xl/sharedStrings.xml"), R"(count="3")",
            std::string(test_case.name)
                + " dirty projection should update legal declaration count metadata");
        check_contains(output_entries.at("xl/sharedStrings.xml"), R"(uniqueCount="3")",
            std::string(test_case.name)
                + " dirty projection should update legal declaration uniqueCount metadata");
        check(output_entries.at("xl/worksheets/sheet2.xml")
                == source_entries.at("xl/worksheets/sheet2.xml"),
            std::string(test_case.name)
                + " dirty projection should preserve untouched sheet bytes");
    }
}

void test_public_worksheet_editor_flattens_rich_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-rich-sharedstrings-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("rich-placeholder"),
            fastxlsx::CellView::text("plain-placeholder")});
        writer.close();
    }

    const std::string rich_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><r><t>rich-</t></r><r><t>A&amp;B</t></r><rPh sb="0" eb="1"><t>ignored-phonetic</t></rPh><phoneticPr fontId="1"/></si>)"
        R"(<si><t>plain</t></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", rich_shared_strings);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "rich-A&B",
        "WorksheetEditor should flatten simple source sharedStrings rich text runs");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "plain",
        "WorksheetEditor should still materialize plain shared string items beside rich text");
    check(!sheet.has_pending_changes(),
        "rich sharedStrings read-only materialization should start clean");
}

void test_public_worksheet_editor_materializes_prefixed_source_shared_strings()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-prefixed-sharedstrings-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("prefix-placeholder-a"),
            fastxlsx::CellView::text("prefix-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-prefixed-shared")});
        writer.close();
    }

    const std::string prefixed_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:sst xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="2" uniqueCount="2">)"
        R"(<x:si><x:t>prefixed-A&amp;B</x:t></x:si>)"
        R"(<x:si><x:r><x:rPr><x:b/></x:rPr><x:t>rich-</x:t></x:r><x:r><x:t xml:space="preserve"> tail </x:t></x:r>)"
        R"(<x:rPh sb="1" eb="1"/><x:phoneticPr fontId="1"/><x:extLst/>)"
        R"(<x:rPh sb="0" eb="1"><fx:opaque><x:r><x:t>ignored-nested-phonetic</x:t></x:r></fx:opaque></x:rPh>)"
        R"(<x:extLst><x:ext uri="{fastxlsx-test}"><fx:opaque><x:r><x:t>ignored-nested-ext</x:t></x:r></fx:opaque></x:ext></x:extLst></x:si>)"
        R"(<x:phoneticPr fontId="2"/><x:extLst/><x:extLst><x:ext uri="{fastxlsx-root}"><fx:opaque><x:t>ignored-root-ext</x:t></fx:opaque></x:ext></x:extLst>)"
        R"(</x:sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", prefixed_shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, "<x:sst",
        "prefixed sharedStrings fixture should use a qualified root element");
    check_contains(shared_strings_before, "<x:si>",
        "prefixed sharedStrings fixture should use qualified shared string items");
    check_contains(shared_strings_before, "<x:t>",
        "prefixed sharedStrings fixture should use qualified text elements");
    check_contains(shared_strings_before, "ignored-nested-ext",
        "prefixed sharedStrings fixture should carry nested ignored extension text");
    check_contains(shared_strings_before, "ignored-root-ext",
        "prefixed sharedStrings fixture should carry root-level ignored extension text");
    check_contains(shared_strings_before, "<x:extLst/>",
        "prefixed sharedStrings fixture should carry self-closing ignored metadata");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "prefixed-A&B",
        "WorksheetEditor should materialize prefixed source sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "rich- tail ",
        "WorksheetEditor should flatten prefixed rich sharedStrings by local-name");
    check(!sheet.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should start clean");
    check(!editor.has_pending_changes(),
        "prefixed sharedStrings read-only materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after prefixed sharedStrings materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("prefixed-shared-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>prefixed-A&amp;B</t></is></c>)",
        "dirty projection should write prefixed source sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t xml:space="preserve">rich- tail </t></is></c>)",
        "dirty projection should preserve flattened prefixed rich sharedStrings whitespace");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>prefixed-shared-dirty</t></is></c>)",
        "dirty projection should include edits beside prefixed source sharedStrings");
    check_not_contains(worksheet_xml, "ignored-nested-phonetic",
        "dirty projection should not leak nested ignored sharedStrings phonetic text");
    check_not_contains(worksheet_xml, "ignored-nested-ext",
        "dirty projection should not leak nested ignored sharedStrings extension text");
    check_not_contains(worksheet_xml, "ignored-root-ext",
        "dirty projection should not leak root-level ignored sharedStrings extension text");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve prefixed source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty prefixed sharedStrings projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_local_names_without_namespace_validation()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-noop.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-local-name-no-namespace-validation-dirty.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("wrong-ns-placeholder-a"),
            fastxlsx::CellView::text("wrong-ns-placeholder-b")});
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-wrong-namespace")});
        writer.close();
    }

    const std::string wrong_namespace_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<bad:sst xmlns:bad="urn:fastxlsx:not-spreadsheetml" count="2" uniqueCount="2">)"
        R"(<bad:si><bad:t>wrong-ns-shared</bad:t></bad:si>)"
        R"(<bad:si><bad:r><bad:t>wrong-rich-</bad:t></bad:r><bad:r><bad:t>tail</bad:t></bad:r></bad:si>)"
        R"(</bad:sst>)";
    rewrite_package_entry_as_stored(
        source, "xl/sharedStrings.xml", wrong_namespace_shared_strings);

    const std::string wrong_namespace_worksheet =
        std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)")
        + R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)"
          R"(<bad:sheetData><bad:row r="1">)"
          R"(<bad:c r="A1" t="s"><bad:v>0</bad:v></bad:c>)"
          R"(<bad:c r="B1" t="inlineStr"><bad:is><bad:t>wrong-ns-inline</bad:t></bad:is></bad:c>)"
          R"(<bad:c r="C1" t="s"><bad:v>1</bad:v></bad:c>)"
          R"(</bad:row></bad:sheetData>)"
          R"(</bad:worksheet>)";
    rewrite_package_entry_as_stored(
        source, "xl/worksheets/sheet1.xml", wrong_namespace_worksheet);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check_contains(source_entries.at("xl/sharedStrings.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml sharedStrings URI");
    check_contains(source_entries.at("xl/worksheets/sheet1.xml"), "urn:fastxlsx:not-spreadsheetml",
        "wrong-namespace local-name fixture should use a non-spreadsheetml worksheet URI");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    const std::optional<fastxlsx::CellValue> c1 = sheet.try_cell("C1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "wrong-ns-shared",
        "WorksheetEditor should materialize sharedStrings by local-name without namespace URI validation");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "wrong-ns-inline",
        "WorksheetEditor should materialize inline strings by local-name without namespace URI validation");
    check(c1.has_value() && c1->kind() == fastxlsx::CellValueKind::Text
            && c1->text_value() == "wrong-rich-tail",
        "WorksheetEditor should flatten rich sharedStrings by local-name without namespace URI validation");
    check(!sheet.has_pending_changes(),
        "wrong-namespace local-name materialization should start clean");
    check(!editor.has_pending_changes(),
        "wrong-namespace local-name materialization should not dirty WorkbookEditor");

    editor.save_as(noop_output);
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after wrong-namespace local-name materialization should copy source entries");

    sheet.set_cell("D1", fastxlsx::CellValue::text("wrong-ns-dirty"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>wrong-ns-shared</t></is></c>)",
        "dirty projection should write wrong-namespace sharedStrings text as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>wrong-ns-inline</t></is></c>)",
        "dirty projection should write wrong-namespace inline text as plain inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>wrong-rich-tail</t></is></c>)",
        "dirty projection should write flattened wrong-namespace sharedStrings rich text");
    check_contains(worksheet_xml,
        R"(<c r="D1" t="inlineStr"><is><t>wrong-ns-dirty</t></is></c>)",
        "dirty projection should include edits beside wrong-namespace local-name source cells");
    check_contains(worksheet_xml,
        R"(<bad:worksheet xmlns:bad="urn:fastxlsx:not-spreadsheetml">)",
        "dirty sheetData flush should preserve wrong source worksheet namespace declarations");
    check_contains(worksheet_xml, R"(<bad:dimension ref="A1:D1"/>)",
        "dirty sheetData flush should refresh dimension using the source worksheet prefix");
    check_not_contains(worksheet_xml, "<bad:c",
        "dirty sheetData flush should not preserve wrong source cell namespace prefixes");
    check_not_contains(worksheet_xml, "<bad:v",
        "dirty sheetData flush should not preserve wrong source value namespace prefixes");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml")
                == source_entries.at("xl/sharedStrings.xml"),
        "dirty wrong-namespace projection should preserve source sharedStrings bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty wrong-namespace projection should preserve untouched sheets");
}

void test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-xml-space-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("space-placeholder"),
            fastxlsx::CellView::text("rich-space-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2">)"
        R"(<si><t xml:space="preserve">  plain &amp; space  </t></si>)"
        R"(<si><r><t xml:space="preserve">  rich </t></r><r><t>&amp; B</t></r><r><t xml:space="preserve"> tail  </t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "  plain & space  ",
        "WorksheetEditor should preserve xml:space whitespace from plain sharedStrings text");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "  rich & B tail  ",
        "WorksheetEditor should preserve xml:space whitespace while flattening rich sharedStrings runs");
    check(!sheet.has_pending_changes(),
        "source sharedStrings xml:space materialization should start clean");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings xml:space materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings xml:space materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("dirty-space-trigger"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="s"><v>0</v></c>)",
        "dirty projection should preserve source sharedStrings whitespace via shared string index");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="s"><v>1</v></c>)",
        "dirty projection should preserve flattened rich sharedStrings text via shared string index");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="s"><v>2</v></c>)",
        "dirty projection should append the new trigger edit to sharedStrings");
    const std::string shared_strings_after = output_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_after,
        R"(<si><t>dirty-space-trigger</t></si></sst>)",
        "dirty projection should append new text while preserving source sharedStrings markup");
    check_contains(shared_strings_after, R"(count="3")",
        "dirty projection should update sharedStrings xml:space count metadata");
    check_contains(shared_strings_after, R"(uniqueCount="3")",
        "dirty projection should update sharedStrings xml:space uniqueCount metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings xml:space projection should not mutate the source package");
}

void test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-noop-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-public-sharedstrings-metadata-counts-dirty-output.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("first-placeholder"),
            fastxlsx::CellView::text("second-placeholder")});
        writer.close();
    }

    const std::string shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:fx="urn:fastxlsx:test" count="999" uniqueCount="0" fx:root="ignored">)"
        R"(<si fx:item="first"><t fx:text="first">first-meta</t></si>)"
        R"(<si fx:item="second"><r fx:run="1"><t fx:text="second">second</t></r><r fx:run="2"><t>-meta</t></r></si>)"
        R"(</sst>)";
    rewrite_package_entry_as_stored(source, "xl/sharedStrings.xml", shared_strings);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string shared_strings_before = source_entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_before, R"(count="999")",
        "source sharedStrings metadata fixture should carry inconsistent count");
    check_contains(shared_strings_before, R"(uniqueCount="0")",
        "source sharedStrings metadata fixture should carry inconsistent uniqueCount");
    check_contains(shared_strings_before, R"(fx:root="ignored")",
        "source sharedStrings metadata fixture should carry unknown root attributes");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> a1 = sheet.try_cell("A1");
    const std::optional<fastxlsx::CellValue> b1 = sheet.try_cell("B1");
    check(a1.has_value() && a1->kind() == fastxlsx::CellValueKind::Text
            && a1->text_value() == "first-meta",
        "WorksheetEditor should use actual sharedStrings item text, not root count metadata");
    check(b1.has_value() && b1->kind() == fastxlsx::CellValueKind::Text
            && b1->text_value() == "second-meta",
        "WorksheetEditor should ignore unknown sharedStrings item/run/text attributes");
    check(!sheet.has_pending_changes(),
        "source sharedStrings count/attribute materialization should start clean");
    check(!editor.has_pending_changes(),
        "source sharedStrings count/attribute materialization should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "source sharedStrings count/attribute materialization should not queue Patch edits");

    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as after sharedStrings count/attribute materialization should keep editor clean");
    check(fastxlsx::test::read_zip_entries(noop_output) == source_entries,
        "no-op save_as after sharedStrings count/attribute materialization should copy source entries");

    sheet.set_cell("C1", fastxlsx::CellValue::text("after-metadata"));
    editor.save_as(dirty_output);

    const auto output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>first-meta</t></is></c>)",
        "dirty projection should write count-mismatched source sharedStrings as inline text");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>second-meta</t></is></c>)",
        "dirty projection should write unknown-attribute source sharedStrings as flattened inline text");
    check_contains(worksheet_xml,
        R"(<c r="C1" t="inlineStr"><is><t>after-metadata</t></is></c>)",
        "dirty projection should include the metadata-boundary trigger edit");
    check_not_contains(worksheet_xml, R"(t="s")",
        "dirty projection should not write shared string indexes after materialization");
    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "dirty projection should preserve source sharedStrings bytes with inconsistent metadata");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "source sharedStrings count/attribute projection should not mutate the source package");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_defers_source_shared_strings_until_index_cells();
        test_public_worksheet_editor_defers_duplicate_shared_strings_relationship_until_index_cells();
        test_public_worksheet_editor_defers_malformed_shared_strings_xml_until_index_cells();
        test_public_worksheet_editor_defers_wrong_shared_strings_content_type_until_index_cells();
        test_public_worksheet_editor_materializes_source_shared_strings();
        test_public_worksheet_editor_accepts_legal_source_shared_strings_xml_declarations();
        test_public_worksheet_editor_flattens_rich_source_shared_strings();
        test_public_worksheet_editor_materializes_prefixed_source_shared_strings();
        test_public_worksheet_editor_materializes_local_names_without_namespace_validation();
        test_public_worksheet_editor_materializes_source_shared_strings_xml_space_and_projects_inline();
        test_public_worksheet_editor_ignores_source_shared_strings_counts_and_unknown_attributes();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor source-success sharedStrings check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor source-success sharedStrings tests passed\n");
    return 0;
}
