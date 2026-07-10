#include "zip_test_utils.hpp"

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

std::filesystem::path write_source(std::string_view name, std::string worksheet,
    std::string shared_strings = {})
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    std::string content_types =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (!shared_strings.empty()) {
        content_types +=
            R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)";
    }
    content_types += R"(</Types>)";

    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (!shared_strings.empty()) {
        workbook_relationships +=
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)";
    }
    workbook_relationships += R"(</Relationships>)";

    std::map<std::string, std::string> entries;
    entries.emplace("[Content_Types].xml", std::move(content_types));
    entries.emplace("_rels/.rels",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)");
    entries.emplace("xl/workbook.xml",
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets></workbook>)");
    entries.emplace("xl/_rels/workbook.xml.rels", std::move(workbook_relationships));
    entries.emplace("xl/worksheets/sheet1.xml", std::move(worksheet));
    if (!shared_strings.empty()) {
        entries.emplace("xl/sharedStrings.xml", std::move(shared_strings));
    }
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

fastxlsx::WorksheetEditorOptions lossy_options()
{
    fastxlsx::WorksheetEditorOptions options;
    options.materialization_policy =
        fastxlsx::WorksheetMaterializationPolicy::AllowLossyProjection;
    return options;
}

void check_failed_materialization_is_clean(
    fastxlsx::WorkbookEditor& editor, const char* context)
{
    check(!editor.has_pending_changes(), context);
    check(!editor.has_unsaved_changes(), context);
    check(editor.pending_change_count() == 0, context);
    check(editor.unsaved_change_count() == 0, context);
}

void test_strict_rejects_inline_rich_text_and_lossy_opt_in_flattens()
{
    const auto source = write_source("fastxlsx-materialization-policy-inline-rich.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><rPr><b/></rPr><t>Rich</t></r><r><t> Text</t></r></is></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(throws_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "strict materialization should reject inline rich text");
    check_failed_materialization_is_clean(editor,
        "failed strict inline-rich materialization should not pollute editor state");

    fastxlsx::WorksheetEditor worksheet = editor.worksheet("Data", lossy_options());
    const auto value = worksheet.try_cell("A1");
    check(value.has_value() && value->text_value() == "Rich Text",
        "lossy materialization should explicitly flatten inline rich text");
    check(!editor.has_unsaved_changes(),
        "read-only lossy materialization should remain clean");
}

void test_strict_rejects_referenced_shared_rich_text_only()
{
    const std::string shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="2" uniqueCount="2"><si><r><t>Rich</t></r><r><t> Shared</t></r></si><si><t>Plain</t></si></sst>)";
    const auto rich_source = write_source("fastxlsx-materialization-policy-shared-rich.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)",
        shared_strings);
    fastxlsx::WorkbookEditor rich_editor = fastxlsx::WorkbookEditor::open(rich_source);
    check(throws_fastxlsx_error([&] { (void)rich_editor.worksheet("Data"); }),
        "strict materialization should reject referenced shared rich text");
    check_failed_materialization_is_clean(rich_editor,
        "failed strict shared-rich materialization should not pollute editor state");
    const auto rich_value = rich_editor.worksheet("Data", lossy_options()).try_cell("A1");
    check(rich_value.has_value() && rich_value->text_value() == "Rich Shared",
        "lossy materialization should explicitly flatten shared rich text");

    const auto plain_source = write_source("fastxlsx-materialization-policy-shared-plain.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="s"><v>1</v></c></row></sheetData></worksheet>)",
        shared_strings);
    fastxlsx::WorkbookEditor plain_editor = fastxlsx::WorkbookEditor::open(plain_source);
    const auto plain_value = plain_editor.worksheet("Data").try_cell("A1");
    check(plain_value.has_value() && plain_value->text_value() == "Plain",
        "strict materialization should allow a plain shared string even when another item is rich text");
}

void test_strict_rejects_formula_metadata_and_cached_results()
{
    const auto metadata_source = write_source("fastxlsx-materialization-policy-formula-metadata.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f t="array" ref="A1:A2">SUM(B1:B2)</f></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor metadata_editor = fastxlsx::WorkbookEditor::open(metadata_source);
    check(throws_fastxlsx_error([&] { (void)metadata_editor.worksheet("Data"); }),
        "strict materialization should reject formula metadata");
    const auto formula = metadata_editor.worksheet("Data", lossy_options()).try_cell("A1");
    check(formula.has_value() && formula->text_value() == "SUM(B1:B2)",
        "lossy materialization should explicitly flatten formula metadata");

    const auto cached_source = write_source("fastxlsx-materialization-policy-formula-cache.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f>1+1</f><v>2</v></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor cached_editor = fastxlsx::WorkbookEditor::open(cached_source);
    check(throws_fastxlsx_error([&] { (void)cached_editor.worksheet("Data"); }),
        "strict materialization should reject cached formula results");
    const auto cached_formula = cached_editor.worksheet("Data", lossy_options()).try_cell("A1");
    check(cached_formula.has_value() && cached_formula->text_value() == "1+1",
        "lossy materialization should explicitly discard cached formula results");
}

void test_materialization_policy_is_part_of_session_identity()
{
    const auto source = write_source("fastxlsx-materialization-policy-session-identity.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    (void)editor.worksheet("Data");
    check(throws_fastxlsx_error([&] { (void)editor.worksheet("Data", lossy_options()); }),
        "materialization policy mismatch should not silently reuse an existing session");
}

} // namespace

int main()
{
    try {
        test_strict_rejects_inline_rich_text_and_lossy_opt_in_flattens();
        test_strict_rejects_referenced_shared_rich_text_only();
        test_strict_rejects_formula_metadata_and_cached_results();
        test_materialization_policy_is_part_of_session_identity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}