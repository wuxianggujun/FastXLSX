#include "zip_test_utils.hpp"

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
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

bool throws_materialization_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::WorksheetMaterializationError&) {
        return true;
    } catch (const fastxlsx::FastXlsxError&) {
        return false;
    }
    return false;
}

fastxlsx::WorksheetMaterializationDiagnostic capture_materialization_diagnostic(
    auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::WorksheetMaterializationError& error) {
        return error.diagnostic();
    } catch (const fastxlsx::FastXlsxError& error) {
        throw TestFailure(
            std::string("expected WorksheetMaterializationError, received FastXlsxError: ")
            + error.what());
    }
    throw TestFailure("expected WorksheetMaterializationError");
}

void check_materialization_diagnostic(
    const fastxlsx::WorksheetMaterializationDiagnostic& diagnostic,
    fastxlsx::WorksheetMaterializationLossCategory category,
    std::uint32_t row, std::uint32_t column,
    std::optional<std::size_t> shared_string_index,
    const char* context)
{
    check(diagnostic.category == category, context);
    check(diagnostic.worksheet_name == "Data", context);
    check(diagnostic.row == row, context);
    check(diagnostic.column == column, context);
    check(diagnostic.shared_string_index == shared_string_index, context);
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
    check(!editor.last_edit_error().has_value(), context);
}

void test_strict_rejects_inline_rich_text_and_lossy_opt_in_flattens()
{
    const auto source = write_source("fastxlsx-materialization-policy-inline-rich.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><r><rPr><b/></rPr><t>Rich</t></r><r><t> Text</t></r></is></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetMaterializationDiagnostic diagnostic =
        capture_materialization_diagnostic([&] { (void)editor.worksheet("Data"); });
    check_materialization_diagnostic(diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::RichText,
        1, 1, std::nullopt,
        "strict inline rich text diagnostic should expose category and cell context");
    const auto try_diagnostic = capture_materialization_diagnostic(
        [&] { (void)editor.try_worksheet("Data"); });
    check_materialization_diagnostic(try_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::RichText,
        1, 1, std::nullopt,
        "try_worksheet should propagate the same strict materialization diagnostic");
    check_failed_materialization_is_clean(editor,
        "failed strict inline-rich materialization should not pollute editor state");

    fastxlsx::WorksheetEditor worksheet = editor.worksheet("Data", lossy_options());
    const auto value = worksheet.try_cell("A1");
    check(value.has_value() && value->text_value() == "Rich Text",
        "lossy materialization should explicitly flatten inline rich text");
    check(!editor.has_unsaved_changes(),
        "read-only lossy materialization should remain clean");

    fastxlsx::WorkbookEditor diagnostic_state_editor = fastxlsx::WorkbookEditor::open(source);
    check(throws_fastxlsx_error(
              [&] { diagnostic_state_editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename should seed last_edit_error before strict materialization");
    const std::optional<std::string> prior_edit_error =
        diagnostic_state_editor.last_edit_error();
    check(prior_edit_error.has_value(),
        "invalid rename should expose a prior public edit diagnostic");
    (void)capture_materialization_diagnostic(
        [&] { (void)diagnostic_state_editor.worksheet("Data"); });
    check(diagnostic_state_editor.last_edit_error() == prior_edit_error,
        "strict materialization diagnostic should preserve last_edit_error");
}

void test_strict_distinguishes_inline_phonetic_and_extension_metadata()
{
    const auto phonetic_source = write_source(
        "fastxlsx-materialization-policy-inline-phonetic.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="2"><c r="B2" t="inlineStr" ph="1"><is><t>Phonetic</t></is></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor phonetic_editor =
        fastxlsx::WorkbookEditor::open(phonetic_source);
    const auto phonetic_diagnostic = capture_materialization_diagnostic(
        [&] { (void)phonetic_editor.worksheet("Data"); });
    check_materialization_diagnostic(phonetic_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::PhoneticMetadata,
        2, 2, std::nullopt,
        "inline phonetic diagnostic should expose category and B2 context");
    check_failed_materialization_is_clean(phonetic_editor,
        "failed inline phonetic materialization should not pollute editor state");

    const auto extension_source = write_source(
        "fastxlsx-materialization-policy-inline-extension.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="3"><c r="C3" t="inlineStr"><is><t>Extension</t><extLst/></is></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor extension_editor =
        fastxlsx::WorkbookEditor::open(extension_source);
    const auto extension_diagnostic = capture_materialization_diagnostic(
        [&] { (void)extension_editor.worksheet("Data"); });
    check_materialization_diagnostic(extension_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::ExtensionMetadata,
        3, 3, std::nullopt,
        "inline extension diagnostic should expose category and C3 context");
    check_failed_materialization_is_clean(extension_editor,
        "failed inline extension materialization should not pollute editor state");
}

void test_strict_rejects_referenced_shared_rich_text_only()
{
    const std::string shared_strings =
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4"><si><r><t>Rich</t></r><r><t> Shared</t></r></si><si><t>Plain</t></si><si><t>Phonetic</t><rPh><t>Guide</t></rPh></si><si><t>Extension</t><extLst><ext uri="diagnostic"/></extLst></si></sst>)";
    const auto rich_source = write_source("fastxlsx-materialization-policy-shared-rich.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)",
        shared_strings);
    fastxlsx::WorkbookEditor rich_editor = fastxlsx::WorkbookEditor::open(rich_source);
    const auto rich_diagnostic = capture_materialization_diagnostic(
        [&] { (void)rich_editor.worksheet("Data"); });
    check_materialization_diagnostic(rich_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::RichText,
        1, 1, std::optional<std::size_t> {0},
        "shared rich text diagnostic should expose category, A1, and index zero");
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

    const auto phonetic_source = write_source(
        "fastxlsx-materialization-policy-shared-phonetic.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="2"><c r="B2" t="s"><v>2</v></c></row></sheetData></worksheet>)",
        shared_strings);
    fastxlsx::WorkbookEditor phonetic_editor =
        fastxlsx::WorkbookEditor::open(phonetic_source);
    const auto phonetic_diagnostic = capture_materialization_diagnostic(
        [&] { (void)phonetic_editor.worksheet("Data"); });
    check_materialization_diagnostic(phonetic_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::PhoneticMetadata,
        2, 2, std::optional<std::size_t> {2},
        "shared phonetic diagnostic should expose category, B2, and index two");

    const auto extension_source = write_source(
        "fastxlsx-materialization-policy-shared-extension.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="3"><c r="C3" t="s"><v>3</v></c></row></sheetData></worksheet>)",
        shared_strings);
    fastxlsx::WorkbookEditor extension_editor =
        fastxlsx::WorkbookEditor::open(extension_source);
    const auto extension_diagnostic = capture_materialization_diagnostic(
        [&] { (void)extension_editor.worksheet("Data"); });
    check_materialization_diagnostic(extension_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::ExtensionMetadata,
        3, 3, std::optional<std::size_t> {3},
        "shared extension diagnostic should expose category, C3, and index three");
}

void test_strict_rejects_formula_metadata_and_cached_results()
{
    const auto metadata_source = write_source("fastxlsx-materialization-policy-formula-metadata.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f t="array" ref="A1:A2">SUM(B1:B2)</f></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor metadata_editor = fastxlsx::WorkbookEditor::open(metadata_source);
    const auto metadata_diagnostic = capture_materialization_diagnostic(
        [&] { (void)metadata_editor.worksheet("Data"); });
    check_materialization_diagnostic(metadata_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::FormulaMetadata,
        1, 1, std::nullopt,
        "formula metadata diagnostic should expose category and A1 context");
    const auto formula = metadata_editor.worksheet("Data", lossy_options()).try_cell("A1");
    check(formula.has_value() && formula->text_value() == "SUM(B1:B2)",
        "lossy materialization should explicitly flatten formula metadata");

    const auto cached_source = write_source("fastxlsx-materialization-policy-formula-cache.xlsx",
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f>1+1</f><v>2</v></c></row></sheetData></worksheet>)");
    fastxlsx::WorkbookEditor cached_editor = fastxlsx::WorkbookEditor::open(cached_source);
    const auto cached_diagnostic = capture_materialization_diagnostic(
        [&] { (void)cached_editor.worksheet("Data"); });
    check_materialization_diagnostic(cached_diagnostic,
        fastxlsx::WorksheetMaterializationLossCategory::CachedFormulaResult,
        1, 1, std::nullopt,
        "cached formula diagnostic should expose category and A1 context");
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
    check(!throws_materialization_error(
              [&] { (void)editor.worksheet("Data", lossy_options()); }),
        "session policy mismatch should remain a generic contract error");
}

} // namespace

int main()
{
    try {
        test_strict_rejects_inline_rich_text_and_lossy_opt_in_flattens();
        test_strict_distinguishes_inline_phonetic_and_extension_metadata();
        test_strict_rejects_referenced_shared_rich_text_only();
        test_strict_rejects_formula_metadata_and_cached_results();
        test_materialization_policy_is_part_of_session_identity();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
