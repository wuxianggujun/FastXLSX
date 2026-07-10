#include "../src/workbook_editor_materialized_edits.hpp"
#include "../src/package_editor.hpp"
#include "zip_test_utils.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

void check_names(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    const char* message)
{
    if (actual != expected) {
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

std::string read_all_chunks(auto& read_next_chunk)
{
    std::string xml;
    std::string chunk;
    while (read_next_chunk(chunk)) {
        xml += chunk;
    }
    return xml;
}

fastxlsx::detail::MaterializedWorksheetSession& materialize_session(
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    std::string planned_name)
{
    fastxlsx::detail::CellStore store;
    return registry.materialize(std::move(planned_name), std::move(store));
}

struct MaterializedFlushSourcePackage {
    std::filesystem::path path;
    std::string worksheet;
    std::string shared_strings;
};

struct MaterializedFlushTwoSheetSourcePackage {
    std::filesystem::path path;
    std::string data_worksheet;
    std::string other_worksheet;
    std::string shared_strings;
};

MaterializedFlushSourcePackage write_materialized_flush_source_package(
    std::string_view name,
    bool with_shared_strings = false)
{
    MaterializedFlushSourcePackage source;
    source.path = fastxlsx::test::artifact_path(name);
    if (with_shared_strings) {
        source.worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)";
        source.shared_strings = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
            R"(<si><t>existing</t></si></sst>)";
    } else {
        source.worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    }

    std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (with_shared_strings) {
        content_types +=
            R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)";
    }
    content_types += R"(</Types>)";

    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (with_shared_strings) {
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
    entries.emplace("xl/worksheets/sheet1.xml", source.worksheet);
    if (with_shared_strings) {
        entries.emplace("xl/sharedStrings.xml", source.shared_strings);
    }
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    return source;
}

MaterializedFlushTwoSheetSourcePackage write_two_sheet_materialized_flush_source_package(
    std::string_view name,
    bool with_shared_strings = false)
{
    MaterializedFlushTwoSheetSourcePackage source;
    source.path = fastxlsx::test::artifact_path(name);
    if (with_shared_strings) {
        source.data_worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c></row></sheetData></worksheet>)";
        source.shared_strings = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
            R"(<si><t>existing</t></si></sst>)";
    } else {
        source.data_worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
            R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>1</v></c></row></sheetData></worksheet>)";
    }
    source.other_worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1"/><sheetData><row r="1"><c r="A1"><v>2</v></c></row></sheetData></worksheet>)";

    std::map<std::string, std::string> entries;
    std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (with_shared_strings) {
        content_types +=
            R"(<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>)";
    }
    content_types += R"(</Types>)";

    entries.emplace("[Content_Types].xml", std::move(content_types));
    entries.emplace("_rels/.rels",
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)");
    entries.emplace("xl/workbook.xml",
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )"
        R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/><sheet name="Other" sheetId="2" r:id="rId2"/></sheets></workbook>)");
    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>)";
    if (with_shared_strings) {
        workbook_relationships +=
            R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)";
    }
    workbook_relationships += R"(</Relationships>)";

    entries.emplace("xl/_rels/workbook.xml.rels", std::move(workbook_relationships));
    entries.emplace("xl/worksheets/sheet1.xml", source.data_worksheet);
    entries.emplace("xl/worksheets/sheet2.xml", source.other_worksheet);
    if (with_shared_strings) {
        entries.emplace("xl/sharedStrings.xml", source.shared_strings);
    }
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    return source;
}

std::string read_stored_package_entry(
    const std::filesystem::path& path,
    std::string_view entry_name)
{
    const std::map<std::string, std::string> entries =
        fastxlsx::test::read_stored_zip_entries(path);
    const auto found = entries.find(std::string(entry_name));
    if (found == entries.end()) {
        throw TestFailure("stored package entry not found");
    }
    return found->second;
}

bool stored_package_has_entry(
    const std::filesystem::path& path,
    std::string_view entry_name)
{
    const std::map<std::string, std::string> entries =
        fastxlsx::test::read_stored_zip_entries(path);
    return entries.find(std::string(entry_name)) != entries.end();
}

std::map<std::string, std::string> read_stored_package_entries(
    const std::filesystem::path& path)
{
    return fastxlsx::test::read_stored_zip_entries(path);
}

void check_materialized_flush_noop_save_is_stable(
    fastxlsx::detail::PackageEditor& editor,
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    const std::filesystem::path& output,
    const std::filesystem::path& noop_output,
    const char* byte_stable_message,
    const char* clean_registry_message)
{
    const std::map<std::string, std::string> output_entries =
        read_stored_package_entries(output);

    editor.save_as(noop_output);
    check(read_stored_package_entries(noop_output) == output_entries,
        byte_stable_message);
    check(registry.dirty_session_count() == 0,
        clean_registry_message);
}

void check_text_cell(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view cell_reference,
    std::string_view expected,
    const char* message)
{
    const auto cell = sheet.try_cell(cell_reference);
    check(cell.has_value() && cell->kind() == fastxlsx::CellValueKind::Text &&
            cell->text_value() == std::string(expected),
        message);
}

void check_number_cell(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view cell_reference,
    double expected,
    const char* message)
{
    const auto cell = sheet.try_cell(cell_reference);
    check(cell.has_value() && cell->kind() == fastxlsx::CellValueKind::Number &&
            cell->number_value() == expected,
        message);
}

void check_boolean_cell(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view cell_reference,
    bool expected,
    const char* message)
{
    const auto cell = sheet.try_cell(cell_reference);
    check(cell.has_value() && cell->kind() == fastxlsx::CellValueKind::Boolean &&
            cell->boolean_value() == expected,
        message);
}

void check_formula_cell(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view cell_reference,
    std::string_view expected,
    const char* message)
{
    const auto cell = sheet.try_cell(cell_reference);
    check(cell.has_value() && cell->kind() == fastxlsx::CellValueKind::Formula &&
            cell->text_value() == std::string(expected),
        message);
}

void check_blank_cell(
    fastxlsx::WorksheetEditor& sheet,
    std::string_view cell_reference,
    const char* message)
{
    const auto cell = sheet.try_cell(cell_reference);
    check(cell.has_value() && cell->kind() == fastxlsx::CellValueKind::Blank,
        message);
}

void check_reopened_editor_clean(
    fastxlsx::WorkbookEditor& editor,
    fastxlsx::WorksheetEditor& data,
    fastxlsx::WorksheetEditor& other,
    const char* message)
{
    check(!editor.has_pending_changes() && !data.has_pending_changes() &&
            !other.has_pending_changes(),
        message);
}

void check_reopened_multi_session_appended_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor other = editor.worksheet("Other");

    check_reopened_editor_clean(
        editor, data, other, "reopened appended sharedStrings output should be clean");
    check(data.cell_count() == 2,
        "reopened appended sharedStrings Data sheet should expose two cells");
    check(other.cell_count() == 3,
        "reopened appended sharedStrings Other sheet should expose three cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened appended sharedStrings Data A1 should expose the source text");
    check_text_cell(
        data, "B1", "cross-sheet-new",
        "reopened appended sharedStrings Data B1 should expose the appended text");
    check_text_cell(
        other, "A1", "cross-sheet-new",
        "reopened appended sharedStrings Other A1 should reuse the appended text");
    check_text_cell(
        other, "B2", "other-only",
        "reopened appended sharedStrings Other B2 should expose sheet-local text");
    check_number_cell(
        other, "C3", 33.0,
        "reopened appended sharedStrings Other C3 should expose the number");
}

void check_reopened_mixed_shared_strings_non_text_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor other = editor.worksheet("Other");

    check_reopened_editor_clean(
        editor, data, other, "reopened mixed sharedStrings/non-text output should be clean");
    check(data.cell_count() == 2,
        "reopened mixed sharedStrings/non-text Data sheet should expose two cells");
    check(other.cell_count() == 4,
        "reopened mixed sharedStrings/non-text Other sheet should expose four cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened mixed sharedStrings/non-text Data A1 should expose source text");
    check_text_cell(
        data, "B1", "text-only-sheet",
        "reopened mixed sharedStrings/non-text Data B1 should expose appended text");
    check_blank_cell(
        other, "B2",
        "reopened mixed sharedStrings/non-text Other B2 should expose blank cell");
    check_number_cell(
        other, "C3", 44.0,
        "reopened mixed sharedStrings/non-text Other C3 should expose number");
    check_boolean_cell(
        other, "A4", false,
        "reopened mixed sharedStrings/non-text Other A4 should expose boolean");
    check_formula_cell(
        other, "D5", "A1&B2<C3",
        "reopened mixed sharedStrings/non-text Other D5 should expose formula text");
}

void check_reopened_multi_session_existing_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor other = editor.worksheet("Other");

    check_reopened_editor_clean(
        editor, data, other, "reopened existing-only sharedStrings output should be clean");
    check(data.cell_count() == 2,
        "reopened existing-only sharedStrings Data sheet should expose two cells");
    check(other.cell_count() == 2,
        "reopened existing-only sharedStrings Other sheet should expose two cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened existing-only sharedStrings Data A1 should expose source text");
    check_number_cell(
        data, "B1", 7.0,
        "reopened existing-only sharedStrings Data B1 should expose the number");
    check_text_cell(
        other, "A1", "existing",
        "reopened existing-only sharedStrings Other A1 should expose source text");
    check_boolean_cell(
        other, "C2", true,
        "reopened existing-only sharedStrings Other C2 should expose the boolean");
}

void check_reopened_multi_session_unsupported_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor other = editor.worksheet("Other");

    check_reopened_editor_clean(
        editor, data, other, "reopened unsupported sharedStrings output should be clean");
    check(data.cell_count() == 3,
        "reopened unsupported sharedStrings Data sheet should expose three cells");
    check(other.cell_count() == 3,
        "reopened unsupported sharedStrings Other sheet should expose three cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened unsupported sharedStrings Data A1 should expose existing text");
    check_text_cell(
        data, "B1", "data <&> text",
        "reopened unsupported sharedStrings Data B1 should expose escaped text");
    check_text_cell(
        data, "C1", "  data spaced  ",
        "reopened unsupported sharedStrings Data C1 should preserve whitespace");
    check_text_cell(
        other, "A1", "existing",
        "reopened unsupported sharedStrings Other A1 should expose existing text");
    check_text_cell(
        other, "B2", "other <&> text",
        "reopened unsupported sharedStrings Other B2 should expose escaped text");
    check_text_cell(
        other, "C3", "  other spaced  ",
        "reopened unsupported sharedStrings Other C3 should preserve whitespace");
}

void check_reopened_single_session_inline_flush_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");

    check(!editor.has_pending_changes() && !data.has_pending_changes(),
        "reopened inline materialized flush output should be clean");
    check(data.cell_count() == 6,
        "reopened inline materialized flush output should expose six cells");
    check_text_cell(
        data, "A1", "inline-a1",
        "reopened inline materialized flush A1 should expose text");
    check_number_cell(
        data, "B2", 22.0,
        "reopened inline materialized flush B2 should expose the number");
    check_boolean_cell(
        data, "C3", true,
        "reopened inline materialized flush C3 should expose the boolean");

    const auto formula = data.try_cell("D4");
    check(formula.has_value() &&
            formula->kind() == fastxlsx::CellValueKind::Formula &&
            formula->text_value() == "A1&B2<C3",
        "reopened inline materialized flush D4 should expose formula text");
    const auto error = data.try_cell("E5");
    check(error.has_value() && error->kind() == fastxlsx::CellValueKind::Error &&
            error->text_value() == "#VALUE<&",
        "reopened inline materialized flush E5 should expose the error token");
    const auto blank = data.try_cell("F6");
    check(blank.has_value() && blank->kind() == fastxlsx::CellValueKind::Blank,
        "reopened inline materialized flush F6 should expose the blank cell");
}

void check_reopened_single_session_appended_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");

    check(!editor.has_pending_changes() && !data.has_pending_changes(),
        "reopened appended sharedStrings single-session output should be clean");
    check(data.cell_count() == 5,
        "reopened appended sharedStrings single-session output should expose five cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened appended sharedStrings single-session A1 should expose source text");
    check_text_cell(
        data, "B1", "new-shared",
        "reopened appended sharedStrings single-session B1 should expose appended text");
    check_text_cell(
        data, "C1", "escaped <&> shared",
        "reopened appended sharedStrings single-session C1 should expose escaped text");
    check_text_cell(
        data, "D1", "  spaced shared  ",
        "reopened appended sharedStrings single-session D1 should preserve whitespace");
    check_number_cell(
        data, "A2", 9.0,
        "reopened appended sharedStrings single-session A2 should expose the number");
}

void check_reopened_single_session_existing_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");

    check(!editor.has_pending_changes() && !data.has_pending_changes(),
        "reopened existing-only sharedStrings single-session output should be clean");
    check(data.cell_count() == 1,
        "reopened existing-only sharedStrings single-session output should expose one cell");
    check_text_cell(
        data, "A1", "existing",
        "reopened existing-only sharedStrings single-session A1 should expose source text");
}

void check_reopened_single_session_unsupported_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");

    check(!editor.has_pending_changes() && !data.has_pending_changes(),
        "reopened unsupported sharedStrings single-session output should be clean");
    check(data.cell_count() == 3,
        "reopened unsupported sharedStrings single-session output should expose three cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened unsupported sharedStrings single-session A1 should expose existing text");
    check_text_cell(
        data, "B1", "fallback <&> text",
        "reopened unsupported sharedStrings single-session B1 should expose fallback text");
    check_text_cell(
        data, "C1", "  fallback spaced  ",
        "reopened unsupported sharedStrings single-session C1 should preserve whitespace");
}

void check_reopened_single_session_duplicate_shared_strings_output(
    const std::filesystem::path& output)
{
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor data = editor.worksheet("Data");

    check(!editor.has_pending_changes() && !data.has_pending_changes(),
        "reopened duplicate sharedStrings single-session output should be clean");
    check(data.cell_count() == 3,
        "reopened duplicate sharedStrings single-session output should expose three cells");
    check_text_cell(
        data, "A1", "existing",
        "reopened duplicate sharedStrings single-session A1 should expose source text");
    check_text_cell(
        data, "B1", "repeat-new",
        "reopened duplicate sharedStrings single-session B1 should expose appended text");
    check_text_cell(
        data, "B2", "repeat-new",
        "reopened duplicate sharedStrings single-session B2 should expose reused appended text");
}

void test_pending_materialized_names_follow_current_catalog_order()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other", "Clean"});
    catalog.record_rename("Data", "Renamed");

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Clean");
    materialize_session(registry, "Other")
        .set_cell(1, 1, fastxlsx::CellValue::number(2.0));
    materialize_session(registry, "Renamed")
        .set_cell(1, 1, fastxlsx::CellValue::text("dirty"));

    check_names(
        fastxlsx::detail::workbook_editor_pending_materialized_worksheet_names(
            catalog, registry),
        {"Renamed", "Other"},
        "pending materialized names should follow current planned catalog order");
}

void test_pending_materialized_names_ignore_stale_source_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    catalog.record_rename("Data", "Renamed");

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("stale-source-dirty"));
    materialize_session(registry, "Renamed")
        .set_cell(1, 1, fastxlsx::CellValue::text("current-dirty"));
    materialize_session(registry, "Other")
        .set_cell(1, 1, fastxlsx::CellValue::number(3.0));

    check_names(
        fastxlsx::detail::workbook_editor_pending_materialized_worksheet_names(
            catalog, registry),
        {"Renamed", "Other"},
        "pending materialized names should ignore dirty sessions outside current catalog");
}

void test_dirty_sheet_data_projections_include_only_dirty_sessions_and_dimensions()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Clean");
    materialize_session(registry, "Data")
        .set_cell(2, 2, fastxlsx::CellValue::text("dirty-b2"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(4, 4, fastxlsx::CellValue::number(4.0));
    materialize_session(registry, "Alpha")
        .set_cell(1, 1, fastxlsx::CellValue::boolean(true));

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections = registry.dirty_sheet_data_chunk_sources();

    check(projections.size() == 2,
        "dirty sheetData projections should include only dirty materialized sessions");
    check(projections[0].planned_name == "Alpha" &&
            projections[1].planned_name == "Data",
        "dirty sheetData projections should follow registry planned-name order");
    check(projections[0].dimension_reference == "A1",
        "dirty sheetData projection should expose single-cell dimensions");
    check(projections[1].dimension_reference == "B2:D4",
        "dirty sheetData projection should expose sparse multi-cell dimensions");

    auto alpha_read_next_chunk = projections[0].read_next_chunk;
    const std::string alpha_xml = read_all_chunks(alpha_read_next_chunk);
    check(alpha_xml.find("<worksheet") == std::string::npos &&
            alpha_xml.find("<dimension") == std::string::npos,
        "dirty sheetData projection should emit sheetData only");
    check(alpha_xml.find(R"(<c r="A1" t="b"><v>1</v></c>)") != std::string::npos,
        "dirty sheetData projection should include boolean cells");

    auto data_read_next_chunk = projections[1].read_next_chunk;
    const std::string data_xml = read_all_chunks(data_read_next_chunk);
    check(data_xml.find(R"(<c r="B2" t="inlineStr"><is><t>dirty-b2</t></is></c>)")
            != std::string::npos,
        "dirty sheetData projection should include text cells");
    check(data_xml.find(R"(<c r="D4"><v>4</v></c>)") != std::string::npos,
        "dirty sheetData projection should include numeric cells");
    check(data_xml.find("Clean") == std::string::npos,
        "dirty sheetData projection should not include clean session data");
}

void test_dirty_sheet_data_projection_uses_shared_string_index_provider()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(1, 2, fastxlsx::CellValue::text("appended"));
    data.set_cell(2, 1, fastxlsx::CellValue::number(3.0));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "existing") {
                    return 0;
                }
                if (text == "appended") {
                    return 5;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in materialized test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "shared-string sheetData projection should include dirty session");
    check(projections[0].planned_name == "Data",
        "shared-string sheetData projection should preserve planned name");
    check(projections[0].dimension_reference == "A1:B2",
        "shared-string sheetData projection should expose sparse dimensions");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "shared-string sheetData projection should reuse source string indexes");
    check(xml.find(R"(<c r="B1" t="s"><v>5</v></c>)") != std::string::npos,
        "shared-string sheetData projection should emit appended string indexes");
    check(xml.find(R"(<c r="A2"><v>3</v></c>)") != std::string::npos,
        "shared-string sheetData projection should leave numeric cells value-only");
    check(xml.find("inlineStr") == std::string::npos,
        "shared-string sheetData projection should not emit inline strings");
}

void test_materialized_session_insert_rows_translates_formula_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId stationary_formula_style =
        fastxlsx::detail::make_source_style_id(7);
    const fastxlsx::StyleId moved_formula_style =
        fastxlsx::detail::make_source_style_id(9);

    data.set_cell(1, 1,
        fastxlsx::CellValue::formula("A2+B2").with_style(stationary_formula_style));
    data.set_cell(2, 3,
        fastxlsx::CellValue::formula("A1+B2").with_style(moved_formula_style));
    data.set_cell(4, 4, fastxlsx::CellValue::number(44.0));
    data.clear_dirty();

    data.insert_rows(2, 1);

    check(data.dirty(),
        "insert_rows should dirty the materialized session after shifting records");
    check(data.cell_count() == 3,
        "insert_rows should preserve sparse record count when it only shifts records");
    check(data.try_cell(2, 3) == nullptr,
        "insert_rows should remove the original formula coordinate");
    check(data.try_cell(4, 4) == nullptr,
        "insert_rows should remove the original tail coordinate");

    const fastxlsx::detail::CellRecord* stationary_formula = data.try_cell(1, 1);
    check(stationary_formula != nullptr &&
            stationary_formula->kind == fastxlsx::CellValueKind::Formula &&
            stationary_formula->text_value == "A3+B3" &&
            stationary_formula->style_id.has_value() &&
            stationary_formula->style_id->value() == stationary_formula_style.value(),
        "insert_rows should rewrite stationary formula references and preserve style");

    const fastxlsx::detail::CellRecord* moved_formula = data.try_cell(3, 3);
    check(moved_formula != nullptr &&
            moved_formula->kind == fastxlsx::CellValueKind::Formula &&
            moved_formula->text_value == "A2+B3" &&
            moved_formula->style_id.has_value() &&
            moved_formula->style_id->value() == moved_formula_style.value(),
        "insert_rows should translate moved formula text and preserve style");

    const fastxlsx::detail::CellRecord* shifted_tail = data.try_cell(5, 4);
    check(shifted_tail != nullptr &&
            shifted_tail->kind == fastxlsx::CellValueKind::Number &&
            shifted_tail->number_value == 44.0,
        "insert_rows should shift later sparse records");

    const std::vector<fastxlsx::detail::MaterializedCellSnapshot> snapshots =
        data.sparse_cell_snapshots();
    check(snapshots.size() == 3 &&
            snapshots[0].position.row == 1 && snapshots[0].position.column == 1 &&
            snapshots[1].position.row == 3 && snapshots[1].position.column == 3 &&
            snapshots[2].position.row == 5 && snapshots[2].position.column == 4,
        "insert_rows should keep snapshots ordered by shifted sparse coordinates");
}

void test_materialized_session_delete_rows_rewrites_formula_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId stationary_formula_style =
        fastxlsx::detail::make_source_style_id(21);
    const fastxlsx::StyleId moved_formula_style =
        fastxlsx::detail::make_source_style_id(23);

    data.set_cell(1, 1,
        fastxlsx::CellValue::formula("A2+A4").with_style(stationary_formula_style));
    data.set_cell(2, 2, fastxlsx::CellValue::text("deleted-b2"));
    data.set_cell(4, 3,
        fastxlsx::CellValue::formula("A3+C4").with_style(moved_formula_style));
    data.set_cell(5, 4, fastxlsx::CellValue::number(55.0));
    data.clear_dirty();

    data.delete_rows(2, 1);

    check(data.dirty(),
        "delete_rows should dirty the materialized session after shifting records");
    check(data.cell_count() == 3,
        "delete_rows should drop deleted-row records and keep shifted survivors");
    check(data.try_cell(2, 2) == nullptr,
        "delete_rows should erase records inside the deleted rows");
    check(data.try_cell(4, 3) == nullptr,
        "delete_rows should remove the original moved formula coordinate");
    check(data.try_cell(5, 4) == nullptr,
        "delete_rows should remove the original tail coordinate");

    const fastxlsx::detail::CellRecord* stationary_formula = data.try_cell(1, 1);
    check(stationary_formula != nullptr &&
            stationary_formula->kind == fastxlsx::CellValueKind::Formula &&
            stationary_formula->text_value == "#REF!+A3" &&
            stationary_formula->style_id.has_value() &&
            stationary_formula->style_id->value() == stationary_formula_style.value(),
        "delete_rows should rewrite stationary formula references and preserve style");

    const fastxlsx::detail::CellRecord* moved_formula = data.try_cell(3, 3);
    check(moved_formula != nullptr &&
            moved_formula->kind == fastxlsx::CellValueKind::Formula &&
            moved_formula->text_value == "A2+C3" &&
            moved_formula->style_id.has_value() &&
            moved_formula->style_id->value() == moved_formula_style.value(),
        "delete_rows should translate moved formula text and preserve style");

    const fastxlsx::detail::CellRecord* shifted_tail = data.try_cell(4, 4);
    check(shifted_tail != nullptr &&
            shifted_tail->kind == fastxlsx::CellValueKind::Number &&
            shifted_tail->number_value == 55.0,
        "delete_rows should shift later sparse records");

    const std::vector<fastxlsx::detail::MaterializedCellSnapshot> snapshots =
        data.sparse_cell_snapshots();
    check(snapshots.size() == 3 &&
            snapshots[0].position.row == 1 && snapshots[0].position.column == 1 &&
            snapshots[1].position.row == 3 && snapshots[1].position.column == 3 &&
            snapshots[2].position.row == 4 && snapshots[2].position.column == 4,
        "delete_rows should keep snapshots ordered by shifted sparse coordinates");
}

void test_materialized_session_delete_columns_rewrites_formula_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId stationary_formula_style =
        fastxlsx::detail::make_source_style_id(11);
    const fastxlsx::StyleId moved_formula_style =
        fastxlsx::detail::make_source_style_id(13);

    data.set_cell(1, 1,
        fastxlsx::CellValue::formula("B1+D1").with_style(stationary_formula_style));
    data.set_cell(2, 4,
        fastxlsx::CellValue::formula("A1+D1").with_style(moved_formula_style));
    data.set_cell(3, 5, fastxlsx::CellValue::text("tail-e3"));
    data.set_cell(4, 2, fastxlsx::CellValue::number(24.0));
    data.clear_dirty();

    data.delete_columns(2, 2);

    check(data.dirty(),
        "delete_columns should dirty the materialized session after shifting records");
    check(data.cell_count() == 3,
        "delete_columns should drop deleted-column records and keep shifted survivors");
    check(data.try_cell(2, 4) == nullptr,
        "delete_columns should remove the original moved formula coordinate");
    check(data.try_cell(3, 5) == nullptr,
        "delete_columns should remove the original tail coordinate");
    check(data.try_cell(4, 2) == nullptr,
        "delete_columns should erase records inside the deleted columns");

    const fastxlsx::detail::CellRecord* stationary_formula = data.try_cell(1, 1);
    check(stationary_formula != nullptr &&
            stationary_formula->kind == fastxlsx::CellValueKind::Formula &&
            stationary_formula->text_value == "#REF!+B1" &&
            stationary_formula->style_id.has_value() &&
            stationary_formula->style_id->value() == stationary_formula_style.value(),
        "delete_columns should rewrite stationary formula references and preserve style");

    const fastxlsx::detail::CellRecord* moved_formula = data.try_cell(2, 2);
    check(moved_formula != nullptr &&
            moved_formula->kind == fastxlsx::CellValueKind::Formula &&
            moved_formula->text_value == "#REF!+B1" &&
            moved_formula->style_id.has_value() &&
            moved_formula->style_id->value() == moved_formula_style.value(),
        "delete_columns should translate moved formula text and preserve style");

    const fastxlsx::detail::CellRecord* shifted_tail = data.try_cell(3, 3);
    check(shifted_tail != nullptr &&
            shifted_tail->kind == fastxlsx::CellValueKind::Text &&
            shifted_tail->text_value == "tail-e3",
        "delete_columns should shift later sparse records");

    const std::vector<fastxlsx::detail::MaterializedCellSnapshot> snapshots =
        data.sparse_cell_snapshots();
    check(snapshots.size() == 3 &&
            snapshots[0].position.row == 1 && snapshots[0].position.column == 1 &&
            snapshots[1].position.row == 2 && snapshots[1].position.column == 2 &&
            snapshots[2].position.row == 3 && snapshots[2].position.column == 3,
        "delete_columns should keep snapshots ordered by shifted sparse coordinates");
}

void test_materialized_session_insert_columns_translates_formula_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId stationary_formula_style =
        fastxlsx::detail::make_source_style_id(25);
    const fastxlsx::StyleId moved_formula_style =
        fastxlsx::detail::make_source_style_id(27);

    data.set_cell(1, 1,
        fastxlsx::CellValue::formula("B1+D1").with_style(stationary_formula_style));
    data.set_cell(2, 3,
        fastxlsx::CellValue::formula("A1+C2").with_style(moved_formula_style));
    data.set_cell(3, 5, fastxlsx::CellValue::text("tail-e3"));
    data.clear_dirty();

    data.insert_columns(2, 2);

    check(data.dirty(),
        "insert_columns should dirty the materialized session after shifting records");
    check(data.cell_count() == 3,
        "insert_columns should preserve sparse record count when it only shifts records");
    check(data.try_cell(2, 3) == nullptr,
        "insert_columns should remove the original formula coordinate");
    check(data.try_cell(3, 5) == nullptr,
        "insert_columns should remove the original tail coordinate");

    const fastxlsx::detail::CellRecord* stationary_formula = data.try_cell(1, 1);
    check(stationary_formula != nullptr &&
            stationary_formula->kind == fastxlsx::CellValueKind::Formula &&
            stationary_formula->text_value == "D1+F1" &&
            stationary_formula->style_id.has_value() &&
            stationary_formula->style_id->value() == stationary_formula_style.value(),
        "insert_columns should rewrite stationary formula references and preserve style");

    const fastxlsx::detail::CellRecord* moved_formula = data.try_cell(2, 5);
    check(moved_formula != nullptr &&
            moved_formula->kind == fastxlsx::CellValueKind::Formula &&
            moved_formula->text_value == "C1+E2" &&
            moved_formula->style_id.has_value() &&
            moved_formula->style_id->value() == moved_formula_style.value(),
        "insert_columns should translate moved formula text and preserve style");

    const fastxlsx::detail::CellRecord* shifted_tail = data.try_cell(3, 7);
    check(shifted_tail != nullptr &&
            shifted_tail->kind == fastxlsx::CellValueKind::Text &&
            shifted_tail->text_value == "tail-e3",
        "insert_columns should shift later sparse records");

    const std::vector<fastxlsx::detail::MaterializedCellSnapshot> snapshots =
        data.sparse_cell_snapshots();
    check(snapshots.size() == 3 &&
            snapshots[0].position.row == 1 && snapshots[0].position.column == 1 &&
            snapshots[1].position.row == 2 && snapshots[1].position.column == 5 &&
            snapshots[2].position.row == 3 && snapshots[2].position.column == 7,
        "insert_columns should keep snapshots ordered by shifted sparse coordinates");
}

void test_materialized_session_insert_row_overflow_preserves_state()
{
    constexpr std::uint32_t max_excel_rows = 1048576U;

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(17);

    data.set_cell(1, 1, fastxlsx::CellValue::text("a1"));
    data.set_cell(max_excel_rows, 2,
        fastxlsx::CellValue::formula("A1+B1").with_style(formula_style));
    data.clear_dirty();
    const std::size_t initial_memory = data.estimated_memory_usage();

    check(throws_fastxlsx_error([&] { data.insert_rows(1, 1); }),
        "insert_rows overflow should throw before replacing sparse records");

    check(!data.dirty(),
        "insert_rows overflow should keep the materialized session clean");
    check(data.cell_count() == 2,
        "insert_rows overflow should preserve sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "insert_rows overflow should preserve estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Text &&
            first_cell->text_value == "a1",
        "insert_rows overflow should preserve the first source-backed cell");
    const fastxlsx::detail::CellRecord* formula_cell =
        data.try_cell(max_excel_rows, 2);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B1" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "insert_rows overflow should preserve formula text and style");
    check(data.try_cell(2, 1) == nullptr,
        "insert_rows overflow should not leak a partially shifted first cell");
}

void test_materialized_session_insert_column_overflow_preserves_state()
{
    constexpr std::uint32_t max_excel_columns = 16384U;

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(19);

    data.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    data.set_cell(2, max_excel_columns,
        fastxlsx::CellValue::formula("A1+B1").with_style(formula_style));
    data.clear_dirty();
    const std::size_t initial_memory = data.estimated_memory_usage();

    check(throws_fastxlsx_error([&] { data.insert_columns(1, 1); }),
        "insert_columns overflow should throw before replacing sparse records");

    check(!data.dirty(),
        "insert_columns overflow should keep the materialized session clean");
    check(data.cell_count() == 2,
        "insert_columns overflow should preserve sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "insert_columns overflow should preserve estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Number &&
            first_cell->number_value == 1.0,
        "insert_columns overflow should preserve the first source-backed cell");
    const fastxlsx::detail::CellRecord* formula_cell =
        data.try_cell(2, max_excel_columns);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B1" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "insert_columns overflow should preserve formula text and style");
    check(data.try_cell(1, 2) == nullptr,
        "insert_columns overflow should not leak a partially shifted first cell");
}

void test_materialized_session_invalid_row_spans_preserve_state()
{
    constexpr std::uint32_t max_excel_rows = 1048576U;

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(29);

    data.set_cell(1, 1, fastxlsx::CellValue::text("a1"));
    data.set_cell(2, 2,
        fastxlsx::CellValue::formula("A1+B2").with_style(formula_style));
    data.clear_dirty();
    const std::size_t initial_memory = data.estimated_memory_usage();

    check(throws_fastxlsx_error([&] { data.insert_rows(0, 1); }),
        "insert_rows should reject row zero before mutating state");
    check(throws_fastxlsx_error([&] { data.delete_rows(0, 1); }),
        "delete_rows should reject row zero before mutating state");
    check(throws_fastxlsx_error([&] { data.insert_rows(2, max_excel_rows); }),
        "insert_rows should reject row spans past the worksheet limit");
    check(throws_fastxlsx_error([&] { data.delete_rows(2, max_excel_rows); }),
        "delete_rows should reject row spans past the worksheet limit");

    check(!data.dirty(),
        "invalid row spans should keep the materialized session clean");
    check(data.cell_count() == 2,
        "invalid row spans should preserve sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "invalid row spans should preserve estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Text &&
            first_cell->text_value == "a1",
        "invalid row spans should preserve the first source-backed cell");
    const fastxlsx::detail::CellRecord* formula_cell = data.try_cell(2, 2);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B2" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "invalid row spans should preserve formula text and style");
    check(data.try_cell(3, 2) == nullptr,
        "invalid row spans should not leak partially shifted row records");
}

void test_materialized_session_invalid_column_spans_preserve_state()
{
    constexpr std::uint32_t max_excel_columns = 16384U;

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(31);

    data.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    data.set_cell(2, 2,
        fastxlsx::CellValue::formula("A1+B2").with_style(formula_style));
    data.clear_dirty();
    const std::size_t initial_memory = data.estimated_memory_usage();

    check(throws_fastxlsx_error([&] { data.insert_columns(0, 1); }),
        "insert_columns should reject column zero before mutating state");
    check(throws_fastxlsx_error([&] { data.delete_columns(0, 1); }),
        "delete_columns should reject column zero before mutating state");
    check(throws_fastxlsx_error([&] { data.insert_columns(2, max_excel_columns); }),
        "insert_columns should reject column spans past the worksheet limit");
    check(throws_fastxlsx_error([&] { data.delete_columns(2, max_excel_columns); }),
        "delete_columns should reject column spans past the worksheet limit");

    check(!data.dirty(),
        "invalid column spans should keep the materialized session clean");
    check(data.cell_count() == 2,
        "invalid column spans should preserve sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "invalid column spans should preserve estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Number &&
            first_cell->number_value == 1.0,
        "invalid column spans should preserve the first source-backed cell");
    const fastxlsx::detail::CellRecord* formula_cell = data.try_cell(2, 2);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B2" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "invalid column spans should preserve formula text and style");
    check(data.try_cell(2, 3) == nullptr,
        "invalid column spans should not leak partially shifted column records");
}

void test_materialized_session_zero_count_structural_edits_preserve_clean_state()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(33);

    data.set_cell(1, 1, fastxlsx::CellValue::text("a1"));
    data.set_cell(2, 2,
        fastxlsx::CellValue::formula("A1+B2").with_style(formula_style));
    data.clear_dirty();
    const std::size_t initial_memory = data.estimated_memory_usage();

    data.insert_rows(2, 0);
    data.delete_rows(2, 0);
    data.insert_columns(2, 0);
    data.delete_columns(2, 0);

    check(!data.dirty(),
        "zero-count structural edits should keep a clean session clean");
    check(data.cell_count() == 2,
        "zero-count structural edits should preserve clean sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "zero-count structural edits should preserve clean estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Text &&
            first_cell->text_value == "a1",
        "zero-count structural edits should preserve clean text cells");
    const fastxlsx::detail::CellRecord* formula_cell = data.try_cell(2, 2);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B2" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "zero-count structural edits should preserve clean formula text and style");
}

void test_materialized_session_zero_count_structural_edits_preserve_dirty_state()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    const fastxlsx::StyleId formula_style =
        fastxlsx::detail::make_source_style_id(35);

    data.set_cell(1, 1, fastxlsx::CellValue::number(1.0));
    data.set_cell(2, 2,
        fastxlsx::CellValue::formula("A1+B2").with_style(formula_style));
    const std::size_t initial_memory = data.estimated_memory_usage();

    data.insert_rows(2, 0);
    data.delete_rows(2, 0);
    data.insert_columns(2, 0);
    data.delete_columns(2, 0);

    check(data.dirty(),
        "zero-count structural edits should keep a dirty session dirty");
    check(data.cell_count() == 2,
        "zero-count structural edits should preserve dirty sparse cell count");
    check(data.estimated_memory_usage() == initial_memory,
        "zero-count structural edits should preserve dirty estimated memory usage");

    const fastxlsx::detail::CellRecord* first_cell = data.try_cell(1, 1);
    check(first_cell != nullptr &&
            first_cell->kind == fastxlsx::CellValueKind::Number &&
            first_cell->number_value == 1.0,
        "zero-count structural edits should preserve dirty numeric cells");
    const fastxlsx::detail::CellRecord* formula_cell = data.try_cell(2, 2);
    check(formula_cell != nullptr &&
            formula_cell->kind == fastxlsx::CellValueKind::Formula &&
            formula_cell->text_value == "A1+B2" &&
            formula_cell->style_id.has_value() &&
            formula_cell->style_id->value() == formula_style.value(),
        "zero-count structural edits should preserve dirty formula text and style");
}

void test_dirty_worksheet_projection_uses_shared_string_index_provider()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(3, 2, fastxlsx::CellValue::text("appended"));
    data.set_cell(3, 3, fastxlsx::CellValue::number(9.0));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "existing") {
                    return 0;
                }
                if (text == "appended") {
                    return 7;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in worksheet projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "shared-string worksheet projection should include dirty session");
    check(projections[0].planned_name == "Data",
        "shared-string worksheet projection should preserve planned name");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(xml.find(R"(<?xml version="1.0" encoding="UTF-8"?>)")
            != std::string::npos,
        "shared-string worksheet projection should emit XML declaration");
    check(xml.find(R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)")
            != std::string::npos,
        "shared-string worksheet projection should emit worksheet root");
    check(xml.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "shared-string worksheet projection should emit sparse dimensions");
    check(xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "shared-string worksheet projection should reuse source string indexes");
    check(xml.find(R"(<c r="B3" t="s"><v>7</v></c>)") != std::string::npos,
        "shared-string worksheet projection should emit appended string indexes");
    check(xml.find(R"(<c r="C3"><v>9</v></c>)") != std::string::npos,
        "shared-string worksheet projection should leave numeric cells value-only");
    check(xml.find("inlineStr") == std::string::npos,
        "shared-string worksheet projection should not emit inline strings");
}

void test_dirty_sheet_data_projection_provider_failure_keeps_session_dirty()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("known"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(1, 2, fastxlsx::CellValue::text("missing"));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                throw fastxlsx::FastXlsxError(
                    "missing shared string index in sheetData projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "failing shared-string sheetData projection should still be created");
    check(data.dirty(),
        "failing shared-string sheetData projection should start dirty");

    auto read_next_chunk = projections[0].read_next_chunk;
    check(throws_fastxlsx_error([&] {
        const std::string ignored = read_all_chunks(read_next_chunk);
        (void)ignored;
    }), "failing shared-string sheetData projection should propagate provider errors");
    check(data.dirty(),
        "failing shared-string sheetData projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "failing shared-string sheetData projection should keep dirty diagnostics");

    auto recovery_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                if (text == "missing") {
                    return 3;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in sheetData projection retry");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        retry_projections = registry.dirty_sheet_data_chunk_sources(
            recovery_shared_string_index_provider);
    check(retry_projections.size() == 1,
        "retry shared-string sheetData projection should still include dirty session");
    auto retry_read_next_chunk = retry_projections[0].read_next_chunk;
    const std::string retry_xml = read_all_chunks(retry_read_next_chunk);
    check(retry_xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "retry shared-string sheetData projection should reuse known string index");
    check(retry_xml.find(R"(<c r="B1" t="s"><v>3</v></c>)") != std::string::npos,
        "retry shared-string sheetData projection should use recovered string index");
    check(data.dirty(),
        "retry shared-string sheetData projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "retry shared-string sheetData projection should keep dirty diagnostics");
}

void test_dirty_worksheet_projection_provider_failure_keeps_session_dirty()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::text("known"));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(2, 1, fastxlsx::CellValue::text("missing"));

    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                throw fastxlsx::FastXlsxError(
                    "missing shared string index in worksheet projection test");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "failing shared-string worksheet projection should still be created");
    check(data.dirty(),
        "failing shared-string worksheet projection should start dirty");

    auto read_next_chunk = projections[0].read_next_chunk;
    check(throws_fastxlsx_error([&] {
        const std::string ignored = read_all_chunks(read_next_chunk);
        (void)ignored;
    }), "failing shared-string worksheet projection should propagate provider errors");
    check(data.dirty(),
        "failing shared-string worksheet projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "failing shared-string worksheet projection should keep dirty diagnostics");

    auto recovery_shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [](std::string_view text) -> std::uint32_t {
                if (text == "known") {
                    return 0;
                }
                if (text == "missing") {
                    return 4;
                }
                throw fastxlsx::FastXlsxError(
                    "unexpected shared string text in worksheet projection retry");
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        retry_projections = registry.dirty_worksheet_chunk_sources(
            recovery_shared_string_index_provider);
    check(retry_projections.size() == 1,
        "retry shared-string worksheet projection should still include dirty session");
    auto retry_read_next_chunk = retry_projections[0].read_next_chunk;
    const std::string retry_xml = read_all_chunks(retry_read_next_chunk);
    check(retry_xml.find(R"(<dimension ref="A1:A2"/>)") != std::string::npos,
        "retry shared-string worksheet projection should keep sparse dimensions");
    check(retry_xml.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "retry shared-string worksheet projection should reuse known string index");
    check(retry_xml.find(R"(<c r="A2" t="s"><v>4</v></c>)") != std::string::npos,
        "retry shared-string worksheet projection should use recovered string index");
    check(data.dirty(),
        "retry shared-string worksheet projection should keep session dirty");
    check(registry.dirty_session_count() == 1,
        "retry shared-string worksheet projection should keep dirty diagnostics");
}

void test_dirty_sheet_data_projection_provider_skips_non_text_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::number(11.0));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(2, 2, fastxlsx::CellValue::boolean(false));
    data.set_cell(3, 3, fastxlsx::CellValue::formula("A1&B2<C3"));
    data.set_cell(4, 4, fastxlsx::CellValue::error("#VALUE<&"));
    data.set_cell(5, 5, fastxlsx::CellValue::blank());

    bool provider_called = false;
    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [&provider_called](std::string_view) -> std::uint32_t {
                provider_called = true;
                return 99;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        projections =
            registry.dirty_sheet_data_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "non-text sheetData projection should include dirty session");
    check(projections[0].dimension_reference == "A1:E5",
        "non-text sheetData projection should expose sparse dimensions");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(!provider_called,
        "non-text sheetData projection should not call shared string provider");
    check(xml.find(R"(<c r="A1"><v>11</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit numeric cells");
    check(xml.find(R"(<c r="B2" t="b"><v>0</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit boolean cells");
    check(xml.find(R"(<c r="C3"><f>A1&amp;B2&lt;C3</f></c>)") != std::string::npos,
        "non-text sheetData projection should emit escaped formula cells");
    check(xml.find(R"(<c r="D4" t="e"><v>#VALUE&lt;&amp;</v></c>)") != std::string::npos,
        "non-text sheetData projection should emit escaped error cells");
    check(xml.find(R"(<c r="E5"/>)") != std::string::npos,
        "non-text sheetData projection should emit blank cells");
    check(xml.find(R"(t="s")") == std::string::npos &&
            xml.find("inlineStr") == std::string::npos,
        "non-text sheetData projection should not emit text cell encodings");
    check(data.dirty(),
        "non-text sheetData projection should not clear dirty state");
}

void test_dirty_worksheet_projection_provider_skips_non_text_records()
{
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data")
        .set_cell(1, 1, fastxlsx::CellValue::number(12.0));
    fastxlsx::detail::MaterializedWorksheetSession& data =
        *registry.try_session("Data");
    data.set_cell(3, 3, fastxlsx::CellValue::boolean(true));
    data.set_cell(4, 4, fastxlsx::CellValue::formula("A1&C3<D4"));
    data.set_cell(5, 5, fastxlsx::CellValue::error("#N/A<&"));
    data.set_cell(2, 2, fastxlsx::CellValue::blank());

    bool provider_called = false;
    auto shared_string_index_provider =
        std::make_shared<fastxlsx::detail::CellStoreSharedStringIndexProvider>(
            [&provider_called](std::string_view) -> std::uint32_t {
                provider_called = true;
                return 99;
            });

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        projections =
            registry.dirty_worksheet_chunk_sources(shared_string_index_provider);

    check(projections.size() == 1,
        "non-text worksheet projection should include dirty session");

    auto read_next_chunk = projections[0].read_next_chunk;
    const std::string xml = read_all_chunks(read_next_chunk);
    check(!provider_called,
        "non-text worksheet projection should not call shared string provider");
    check(xml.find(R"(<dimension ref="A1:E5"/>)") != std::string::npos,
        "non-text worksheet projection should emit sparse dimensions");
    check(xml.find(R"(<c r="A1"><v>12</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit numeric cells");
    check(xml.find(R"(<c r="B2"/>)") != std::string::npos,
        "non-text worksheet projection should emit blank cells");
    check(xml.find(R"(<c r="C3" t="b"><v>1</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit boolean cells");
    check(xml.find(R"(<c r="D4"><f>A1&amp;C3&lt;D4</f></c>)") != std::string::npos,
        "non-text worksheet projection should emit escaped formula cells");
    check(xml.find(R"(<c r="E5" t="e"><v>#N/A&lt;&amp;</v></c>)") != std::string::npos,
        "non-text worksheet projection should emit escaped error cells");
    check(xml.find(R"(t="s")") == std::string::npos &&
            xml.find("inlineStr") == std::string::npos,
        "non-text worksheet projection should not emit text cell encodings");
    check(data.dirty(),
        "non-text worksheet projection should not clear dirty state");
}

void test_dirty_empty_projection_after_erasing_all_source_cells()
{
    fastxlsx::detail::CellStore source_store;
    source_store.set_cell(1, 1, fastxlsx::CellValue::text("source-a1"));
    source_store.set_cell(3, 3, fastxlsx::CellValue::number(3.0));

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        registry.materialize("Data", std::move(source_store));
    check(!data.dirty(),
        "source-backed materialized session should start clean before erase");

    data.erase_cells();
    check(data.dirty(),
        "erasing all source-backed materialized cells should dirty the session");
    check(data.cell_count() == 0,
        "erasing all source-backed materialized cells should empty the sparse store");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        sheet_data_projections = registry.dirty_sheet_data_chunk_sources();
    check(sheet_data_projections.size() == 1,
        "empty dirty sheetData projection should include dirty session");
    check(sheet_data_projections[0].planned_name == "Data",
        "empty dirty sheetData projection should preserve planned name");
    check(sheet_data_projections[0].dimension_reference == "A1",
        "empty dirty sheetData projection should expose A1 dimension");

    auto sheet_data_read_next_chunk = sheet_data_projections[0].read_next_chunk;
    const std::string sheet_data_xml =
        read_all_chunks(sheet_data_read_next_chunk);
    check(sheet_data_xml == "<sheetData></sheetData>",
        "empty dirty sheetData projection should emit empty sheetData");
    check(data.dirty(),
        "consuming empty sheetData projection should not clear dirty state");

    const std::vector<fastxlsx::detail::MaterializedWorksheetProjection>
        worksheet_projections = registry.dirty_worksheet_chunk_sources();
    check(worksheet_projections.size() == 1,
        "empty dirty worksheet projection should include dirty session");
    check(worksheet_projections[0].planned_name == "Data",
        "empty dirty worksheet projection should preserve planned name");

    auto worksheet_read_next_chunk = worksheet_projections[0].read_next_chunk;
    const std::string worksheet_xml = read_all_chunks(worksheet_read_next_chunk);
    check(worksheet_xml
            == R"(<?xml version="1.0" encoding="UTF-8"?>)"
               R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
               R"(<dimension ref="A1"/><sheetData></sheetData></worksheet>)",
        "empty dirty worksheet projection should emit minimal worksheet XML");
    check(data.dirty(),
        "consuming empty worksheet projection should not clear dirty state");
}

void test_materialized_flush_target_validation_accepts_current_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection> projections {
        fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
            "Data",
            [](std::string&) { return false; },
            "A1",
        },
    };

    fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
        catalog, projections);
}

void test_materialized_flush_target_validation_uses_current_catalog_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    catalog.record_rename("Data", "Renamed");

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        renamed_projection {
            fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
                "Renamed",
                [](std::string&) { return false; },
                "A1",
            },
        };
    fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
        catalog, renamed_projection);

    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection>
        stale_source_projection {
            fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
                "Data",
                [](std::string&) { return false; },
                "A1",
            },
        };
    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
            catalog, stale_source_projection);
    }), "flush target validation should reject stale source names after rename");
}

void test_materialized_flush_target_validation_rejects_missing_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const std::vector<fastxlsx::detail::MaterializedWorksheetSheetDataProjection> projections {
        fastxlsx::detail::MaterializedWorksheetSheetDataProjection {
            "Missing",
            [](std::string&) { return false; },
            "A1",
        },
    };

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_materialized_flush_targets(
            catalog, projections);
    }), "flush target validation should reject sheets outside current catalog");
}

void test_materialized_flush_to_patch_plan_clears_dirty_sessions()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-source.xlsx");
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("inline-a1"));
    data.set_cell(2, 2, fastxlsx::CellValue::number(22.0));
    data.set_cell(3, 3, fastxlsx::CellValue::boolean(true));
    data.set_cell(4, 4, fastxlsx::CellValue::formula("A1&B2<C3"));
    data.set_cell(5, 5, fastxlsx::CellValue::error("#VALUE<&"));
    data.set_cell(6, 6, fastxlsx::CellValue::blank());
    check(data.dirty(), "materialized flush test should start with a dirty session");

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "materialized flush should report one flushed worksheet");
    check(!data.dirty(),
        "materialized flush should clear the flushed session dirty flag");
    check(registry.dirty_session_count() == 0,
        "materialized flush should clear registry dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet.find(R"(<dimension ref="A1:F6"/>)") != std::string::npos,
        "materialized flush should update worksheet dimensions from sparse cells");
    check(worksheet.find(R"(<c r="A1" t="inlineStr"><is><t>inline-a1</t></is></c>)")
            != std::string::npos,
        "materialized flush should project text as inline strings without sharedStrings");
    check(worksheet.find(R"(<c r="B2"><v>22</v></c>)") != std::string::npos,
        "materialized flush should project numeric sparse cells");
    check(worksheet.find(R"(<c r="C3" t="b"><v>1</v></c>)") != std::string::npos,
        "materialized flush should project boolean sparse cells");
    check(worksheet.find(R"(<c r="D4"><f>A1&amp;B2&lt;C3</f></c>)") != std::string::npos,
        "materialized flush should project escaped formula sparse cells");
    check(worksheet.find(R"(<c r="E5" t="e"><v>#VALUE&lt;&amp;</v></c>)") != std::string::npos,
        "materialized flush should project escaped error sparse cells");
    check(worksheet.find(R"(<c r="F6"/>)") != std::string::npos,
        "materialized flush should project blank sparse cells");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "materialized flush without source sharedStrings should not create sharedStrings");
    check_reopened_single_session_inline_flush_output(output);
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "materialized flush no-op save should keep output byte-stable",
        "materialized flush no-op save should keep registry clean");
    check_reopened_single_session_inline_flush_output(noop_output);
    check(!data.dirty(),
        "materialized flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "materialized flush no-op save should not mutate the source package");
}

void test_materialized_flush_rejects_stale_targets_without_clearing_dirty()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-stale-source.xlsx");
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& missing =
        materialize_session(registry, "Missing");
    missing.set_cell(1, 1, fastxlsx::CellValue::text("should-not-flush"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    check(throws_fastxlsx_error([&] {
        const fastxlsx::detail::WorkbookEditorMaterializedFlushResult ignored =
            fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
                editor, registry, catalog);
        (void)ignored;
    }), "materialized flush should reject dirty sessions outside the current catalog");
    check(missing.dirty(),
        "rejected materialized flush should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush should preserve dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-stale-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-stale-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    check(worksheet == source.worksheet,
        "rejected materialized flush should leave PackageEditor output unmodified");
    check(read_stored_package_entries(output) == source_entries,
        "rejected materialized flush should keep the whole output package source-copy");
    check(missing.dirty(),
        "rejected materialized flush save should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush save should preserve dirty diagnostics");

    editor.save_as(noop_output);
    check(read_stored_package_entries(noop_output) == source_entries,
        "rejected materialized flush no-op save should stay source-copy");
    check(read_stored_package_entries(source.path) == source_entries,
        "rejected materialized flush no-op save should not mutate the source package");
    check(missing.dirty(),
        "rejected materialized flush no-op save should keep the stale session dirty");
    check(registry.dirty_session_count() == 1,
        "rejected materialized flush no-op save should preserve dirty diagnostics");
}

void test_materialized_flush_handles_multiple_dirty_sessions()
{
    const MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-source.xlsx");
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(2, 2, fastxlsx::CellValue::text("data-b2"));
    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(3, 3, fastxlsx::CellValue::number(33.0));
    other.set_cell(4, 1, fastxlsx::CellValue::boolean(false));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session materialized flush should report both dirty worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session materialized flush should clear both dirty sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session materialized flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-noop-output.xlsx");
    editor.save_as(output);
    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");

    check(data_worksheet.find(R"(<dimension ref="B2"/>)") != std::string::npos,
        "multi-session materialized flush should update Data dimensions");
    check(data_worksheet.find(R"(<c r="B2" t="inlineStr"><is><t>data-b2</t></is></c>)")
            != std::string::npos,
        "multi-session materialized flush should write Data sparse cells");
    check(other_worksheet.find(R"(<dimension ref="A3:C4"/>)") != std::string::npos,
        "multi-session materialized flush should update Other dimensions");
    check(other_worksheet.find(R"(<c r="C3"><v>33</v></c>)") != std::string::npos,
        "multi-session materialized flush should write Other numeric cells");
    check(other_worksheet.find(R"(<c r="A4" t="b"><v>0</v></c>)") != std::string::npos,
        "multi-session materialized flush should write Other boolean cells");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "multi-session materialized flush without source sharedStrings should not create sharedStrings");

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session materialized flush no-op save should keep output byte-stable",
        "multi-session materialized flush no-op save should keep registry clean");
    check(!data.dirty() && !other.dirty(),
        "multi-session materialized flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session materialized flush no-op save should not mutate the source package");
}

void test_materialized_flush_appends_shared_strings_projection()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("new-shared"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("escaped <&> shared"));
    data.set_cell(1, 4, fastxlsx::CellValue::text("  spaced shared  "));
    data.set_cell(2, 1, fastxlsx::CellValue::number(9.0));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "sharedStrings materialized flush should report one worksheet");
    check(!data.dirty(),
        "sharedStrings materialized flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:D2"/>)") != std::string::npos,
        "sharedStrings materialized flush should update sparse dimensions");
    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reuse source string index");
    check(worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference appended string index");
    check(worksheet.find(R"(<c r="C1" t="s"><v>2</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference escaped appended string index");
    check(worksheet.find(R"(<c r="D1" t="s"><v>3</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should reference whitespace appended string index");
    check(worksheet.find(R"(<c r="A2"><v>9</v></c>)") != std::string::npos,
        "sharedStrings materialized flush should keep numeric cells value-only");
    check(worksheet.find("inlineStr") == std::string::npos,
        "sharedStrings materialized flush should not emit inline text fallback");
    check(shared_strings.find(R"(count="4")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="4")") != std::string::npos,
        "sharedStrings materialized flush should update source table counts");
    const std::string expected_appended_shared_strings =
        R"(<si><t>existing</t></si>)"
        R"(<si><t>new-shared</t></si>)"
        R"(<si><t>escaped &lt;&amp;&gt; shared</t></si>)"
        R"(<si><t xml:space="preserve">  spaced shared  </t></si>)";
    check(shared_strings.find(expected_appended_shared_strings) != std::string::npos,
        "sharedStrings materialized flush should append missing text with XML escaping and whitespace preservation");
    check_reopened_single_session_appended_shared_strings_output(output);
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "sharedStrings materialized flush no-op save should keep output byte-stable",
        "sharedStrings materialized flush no-op save should keep registry clean");
    check_reopened_single_session_appended_shared_strings_output(noop_output);
    check(!data.dirty(),
        "sharedStrings materialized flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "sharedStrings materialized flush no-op save should not mutate the source package");
}

void test_materialized_flush_reuses_existing_shared_strings_without_rewrite()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-existing-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "existing-only sharedStrings flush should report one worksheet");
    check(!data.dirty(),
        "existing-only sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-existing-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-existing-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "existing-only sharedStrings flush should reuse the existing string index");
    check(worksheet.find("inlineStr") == std::string::npos,
        "existing-only sharedStrings flush should not fall back to inline text");
    check(shared_strings == source.shared_strings,
        "existing-only sharedStrings flush should not rewrite the sharedStrings part");
    check_reopened_single_session_existing_shared_strings_output(output);
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "existing-only sharedStrings flush no-op save should keep output byte-stable",
        "existing-only sharedStrings flush no-op save should keep registry clean");
    check_reopened_single_session_existing_shared_strings_output(noop_output);
    check(!data.dirty(),
        "existing-only sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "existing-only sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_falls_back_to_inline_when_shared_strings_append_is_unsupported()
{
    MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-source.xlsx",
            true);
    const std::string unsupported_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1" unsupported="preserve-me">)"
        R"(<si><t>existing</t></si></sst>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        source.path, "xl/sharedStrings.xml", unsupported_shared_strings);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("fallback <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  fallback spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "unsupported sharedStrings flush should still flush one worksheet");
    check(!data.dirty(),
        "unsupported sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-unsupported-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "unsupported sharedStrings flush should update sparse dimensions");
    check(worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should write existing text inline");
    check(worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>fallback &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should write escaped dirty text inline");
    check(worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  fallback spaced  </t></is></c>)")
            != std::string::npos,
        "unsupported sharedStrings flush should preserve dirty text whitespace inline");
    check(worksheet.find(R"(t="s")") == std::string::npos,
        "unsupported sharedStrings flush should not write shared string indexes");
    check(shared_strings == unsupported_shared_strings,
        "unsupported sharedStrings flush should preserve source sharedStrings bytes");
    check_reopened_single_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "unsupported sharedStrings flush no-op save should keep output byte-stable",
        "unsupported sharedStrings flush no-op save should keep registry clean");
    check_reopened_single_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty(),
        "unsupported sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "unsupported sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_falls_back_to_inline_when_shared_strings_load_fails()
{
    MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-malformed-source.xlsx",
            true);
    const std::string malformed_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
        R"(<si><t>existing</t></si>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        source.path, "xl/sharedStrings.xml", malformed_shared_strings);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("fallback <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  fallback spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "malformed sharedStrings flush should still flush one worksheet");
    check(!data.dirty(),
        "malformed sharedStrings flush should clear the dirty session");
    check(registry.dirty_session_count() == 0,
        "malformed sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-malformed-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-malformed-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "malformed sharedStrings flush should update sparse dimensions");
    check(worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "malformed sharedStrings flush should write existing text inline");
    check(worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>fallback &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "malformed sharedStrings flush should write escaped dirty text inline");
    check(worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  fallback spaced  </t></is></c>)")
            != std::string::npos,
        "malformed sharedStrings flush should preserve dirty text whitespace inline");
    check(worksheet.find(R"(t="s")") == std::string::npos,
        "malformed sharedStrings flush should not write shared string indexes");
    check(shared_strings == malformed_shared_strings,
        "malformed sharedStrings flush should preserve source sharedStrings bytes");
    check_reopened_single_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "malformed sharedStrings flush no-op save should keep output byte-stable",
        "malformed sharedStrings flush no-op save should keep registry clean");
    check_reopened_single_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty(),
        "malformed sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "malformed sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_falls_back_to_inline_when_shared_strings_part_is_missing()
{
    MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-missing-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    entries.erase("xl/sharedStrings.xml");
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("fallback <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  fallback spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "missing sharedStrings flush should still flush one worksheet");
    check(!data.dirty(),
        "missing sharedStrings flush should clear the dirty session");
    check(registry.dirty_session_count() == 0,
        "missing sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-missing-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-missing-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");

    check(worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "missing sharedStrings flush should update sparse dimensions");
    check(worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "missing sharedStrings flush should write existing text inline");
    check(worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>fallback &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "missing sharedStrings flush should write escaped dirty text inline");
    check(worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  fallback spaced  </t></is></c>)")
            != std::string::npos,
        "missing sharedStrings flush should preserve dirty text whitespace inline");
    check(worksheet.find(R"(t="s")") == std::string::npos,
        "missing sharedStrings flush should not write shared string indexes");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "missing sharedStrings flush should not recreate the missing sharedStrings part");
    check_reopened_single_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "missing sharedStrings flush no-op save should keep output byte-stable",
        "missing sharedStrings flush no-op save should keep registry clean");
    check_reopened_single_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty(),
        "missing sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "missing sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_deduplicates_appended_shared_strings()
{
    const MaterializedFlushSourcePackage source =
        write_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("repeat-new"));
    data.set_cell(2, 2, fastxlsx::CellValue::text("repeat-new"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 1,
        "duplicate sharedStrings flush should report one worksheet");
    check(!data.dirty(),
        "duplicate sharedStrings flush should clear the dirty session");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-duplicate-noop-output.xlsx");
    editor.save_as(output);
    const std::string worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)") != std::string::npos,
        "duplicate sharedStrings flush should preserve the existing string index");
    check(worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)") != std::string::npos &&
            worksheet.find(R"(<c r="B2" t="s"><v>1</v></c>)") != std::string::npos,
        "duplicate sharedStrings flush should reuse one appended string index");
    check(worksheet.find("inlineStr") == std::string::npos,
        "duplicate sharedStrings flush should not emit inline text fallback");
    check(shared_strings.find(R"(count="3")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="2")") != std::string::npos,
        "duplicate sharedStrings flush should count references and unique strings separately");
    check(shared_strings.find(R"(<si><t>existing</t></si><si><t>repeat-new</t></si>)")
            != std::string::npos,
        "duplicate sharedStrings flush should append each new text only once");
    check_reopened_single_session_duplicate_shared_strings_output(output);
    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "duplicate sharedStrings flush no-op save should keep output byte-stable",
        "duplicate sharedStrings flush no-op save should keep registry clean");
    check_reopened_single_session_duplicate_shared_strings_output(noop_output);
    check(!data.dirty(),
        "duplicate sharedStrings flush no-op save should keep the flushed session clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "duplicate sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_reuses_shared_strings_across_multiple_dirty_sessions()
{
    const MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("cross-sheet-new"));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("cross-sheet-new"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other-only"));
    other.set_cell(3, 3, fastxlsx::CellValue::number(33.0));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session sharedStrings materialized flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session sharedStrings materialized flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session sharedStrings materialized flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:B1"/>)") != std::string::npos,
        "multi-session sharedStrings flush should update Data dimensions");
    check(data_worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)")
            != std::string::npos,
        "multi-session sharedStrings flush should reuse the source string index");
    check(data_worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)")
            != std::string::npos,
        "multi-session sharedStrings flush should use the first appended string index");
    check(data_worksheet.find("inlineStr") == std::string::npos,
        "multi-session sharedStrings flush should not inline Data text");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session sharedStrings flush should update Other dimensions");
    check(other_worksheet.find(R"(<c r="A1" t="s"><v>1</v></c>)")
            != std::string::npos,
        "multi-session sharedStrings flush should reuse appended indexes across sheets");
    check(other_worksheet.find(R"(<c r="B2" t="s"><v>2</v></c>)")
            != std::string::npos,
        "multi-session sharedStrings flush should append later sheet-only text");
    check(other_worksheet.find(R"(<c r="C3"><v>33</v></c>)") != std::string::npos,
        "multi-session sharedStrings flush should keep non-text cells value-only");
    check(other_worksheet.find("inlineStr") == std::string::npos,
        "multi-session sharedStrings flush should not inline Other text");

    check(shared_strings.find(R"(count="4")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="3")") != std::string::npos,
        "multi-session sharedStrings flush should count cross-sheet references and unique strings");
    check(shared_strings.find(
              R"(<si><t>existing</t></si><si><t>cross-sheet-new</t></si><si><t>other-only</t></si>)")
            != std::string::npos,
        "multi-session sharedStrings flush should append each cross-sheet text once");
    check_reopened_multi_session_appended_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session sharedStrings flush no-op save should keep output byte-stable",
        "multi-session sharedStrings flush no-op save should keep registry clean");
    check_reopened_multi_session_appended_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session sharedStrings flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_shared_strings_skips_non_text_dirty_sessions()
{
    const MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-shared-non-text-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("text-only-sheet"));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(2, 2, fastxlsx::CellValue::blank());
    other.set_cell(3, 3, fastxlsx::CellValue::number(44.0));
    other.set_cell(4, 1, fastxlsx::CellValue::boolean(false));
    other.set_cell(5, 4, fastxlsx::CellValue::formula("A1&B2<C3"));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "mixed sharedStrings/non-text materialized flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "mixed sharedStrings/non-text materialized flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "mixed sharedStrings/non-text materialized flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-non-text-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-shared-non-text-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:B1"/>)") != std::string::npos,
        "mixed sharedStrings/non-text flush should update Data dimensions");
    check(data_worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)")
            != std::string::npos,
        "mixed sharedStrings/non-text flush should reuse the source string index");
    check(data_worksheet.find(R"(<c r="B1" t="s"><v>1</v></c>)")
            != std::string::npos,
        "mixed sharedStrings/non-text flush should append the text-only sheet string");
    check(data_worksheet.find("inlineStr") == std::string::npos,
        "mixed sharedStrings/non-text flush should not inline Data text");

    check(other_worksheet.find(R"(<dimension ref="A2:D5"/>)") != std::string::npos,
        "mixed sharedStrings/non-text flush should update Other dimensions");
    check(other_worksheet.find(R"(<c r="B2"/>)") != std::string::npos,
        "mixed sharedStrings/non-text flush should keep blank cells value-only");
    check(other_worksheet.find(R"(<c r="C3"><v>44</v></c>)") != std::string::npos,
        "mixed sharedStrings/non-text flush should keep numeric cells value-only");
    check(other_worksheet.find(R"(<c r="A4" t="b"><v>0</v></c>)")
            != std::string::npos,
        "mixed sharedStrings/non-text flush should keep boolean cells value-only");
    check(other_worksheet.find(R"(<c r="D5"><f>A1&amp;B2&lt;C3</f></c>)")
            != std::string::npos,
        "mixed sharedStrings/non-text flush should keep formula cells value-only");
    check(other_worksheet.find(R"(t="s")") == std::string::npos &&
            other_worksheet.find("inlineStr") == std::string::npos,
        "mixed sharedStrings/non-text flush should not encode Other cells as text");

    check(shared_strings.find(R"(count="2")") != std::string::npos &&
            shared_strings.find(R"(uniqueCount="2")") != std::string::npos,
        "mixed sharedStrings/non-text flush should count only text references");
    check(shared_strings.find(
              R"(<si><t>existing</t></si><si><t>text-only-sheet</t></si>)")
            != std::string::npos,
        "mixed sharedStrings/non-text flush should append only the text session value");
    check_reopened_mixed_shared_strings_non_text_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "mixed sharedStrings/non-text flush no-op save should keep output byte-stable",
        "mixed sharedStrings/non-text flush no-op save should keep registry clean");
    check_reopened_mixed_shared_strings_non_text_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "mixed sharedStrings/non-text flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "mixed sharedStrings/non-text flush no-op save should not mutate the source package");
}

void test_materialized_flush_reuses_existing_shared_strings_across_multiple_dirty_sessions()
{
    const MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-existing-source.xlsx",
            true);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);
    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::number(7.0));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 3, fastxlsx::CellValue::boolean(true));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session existing-only sharedStrings flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session existing-only sharedStrings flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session existing-only sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-existing-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-existing-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:B1"/>)") != std::string::npos,
        "multi-session existing-only sharedStrings flush should update Data dimensions");
    check(data_worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)")
            != std::string::npos,
        "multi-session existing-only sharedStrings flush should reuse the source index on Data");
    check(data_worksheet.find(R"(<c r="B1"><v>7</v></c>)") != std::string::npos,
        "multi-session existing-only sharedStrings flush should keep Data numbers value-only");
    check(data_worksheet.find("inlineStr") == std::string::npos,
        "multi-session existing-only sharedStrings flush should not inline Data text");

    check(other_worksheet.find(R"(<dimension ref="A1:C2"/>)") != std::string::npos,
        "multi-session existing-only sharedStrings flush should update Other dimensions");
    check(other_worksheet.find(R"(<c r="A1" t="s"><v>0</v></c>)")
            != std::string::npos,
        "multi-session existing-only sharedStrings flush should reuse the source index on Other");
    check(other_worksheet.find(R"(<c r="C2" t="b"><v>1</v></c>)")
            != std::string::npos,
        "multi-session existing-only sharedStrings flush should keep Other booleans value-only");
    check(other_worksheet.find("inlineStr") == std::string::npos,
        "multi-session existing-only sharedStrings flush should not inline Other text");
    check(shared_strings == source.shared_strings,
        "multi-session existing-only sharedStrings flush should not rewrite sharedStrings");
    check_reopened_multi_session_existing_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session existing-only sharedStrings flush no-op save should keep output byte-stable",
        "multi-session existing-only sharedStrings flush no-op save should keep registry clean");
    check_reopened_multi_session_existing_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session existing-only sharedStrings flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session existing-only sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_append_is_unsupported()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-unsupported-source.xlsx",
            true);
    const std::string unsupported_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1" unsupported="preserve-me">)"
        R"(<si><t>existing</t></si></sst>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        source.path, "xl/sharedStrings.xml", unsupported_shared_strings);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session unsupported sharedStrings flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session unsupported sharedStrings flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session unsupported sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-unsupported-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-unsupported-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session unsupported sharedStrings flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session unsupported sharedStrings flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session unsupported sharedStrings flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session unsupported sharedStrings flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session unsupported sharedStrings flush should not write Other shared string indexes");
    check(shared_strings == unsupported_shared_strings,
        "multi-session unsupported sharedStrings flush should preserve source sharedStrings bytes");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session unsupported sharedStrings flush no-op save should keep output byte-stable",
        "multi-session unsupported sharedStrings flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session unsupported sharedStrings flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session unsupported sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_load_fails()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-malformed-source.xlsx",
            true);
    const std::string malformed_shared_strings =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="1" uniqueCount="1">)"
        R"(<si><t>existing</t></si>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        source.path, "xl/sharedStrings.xml", malformed_shared_strings);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session malformed sharedStrings flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session malformed sharedStrings flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session malformed sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-malformed-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-malformed-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session malformed sharedStrings flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session malformed sharedStrings flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session malformed sharedStrings flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session malformed sharedStrings flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session malformed sharedStrings flush should not write Other shared string indexes");
    check(shared_strings == malformed_shared_strings,
        "multi-session malformed sharedStrings flush should preserve source sharedStrings bytes");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session malformed sharedStrings flush no-op save should keep output byte-stable",
        "multi-session malformed sharedStrings flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session malformed sharedStrings flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session malformed sharedStrings flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationship_is_stale()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-stale-relationship-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "stale relationship source should contain workbook relationships");
    const std::string original_target = R"(Target="sharedStrings.xml")";
    const std::string stale_target = R"(Target="missingSharedStrings.xml")";
    const std::size_t target_position =
        workbook_relationships->second.find(original_target);
    check(target_position != std::string::npos,
        "stale relationship source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, original_target.size(), stale_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session stale sharedStrings relationship flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session stale sharedStrings relationship flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session stale sharedStrings relationship flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-stale-relationship-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-stale-relationship-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session stale sharedStrings relationship flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session stale sharedStrings relationship flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session stale sharedStrings relationship flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session stale sharedStrings relationship flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session stale sharedStrings relationship flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session stale sharedStrings relationship flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session stale sharedStrings relationship flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session stale sharedStrings relationship flush no-op save should keep output byte-stable",
        "multi-session stale sharedStrings relationship flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session stale sharedStrings relationship flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session stale sharedStrings relationship flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationship_is_external()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-external-relationship-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "external relationship source should contain workbook relationships");
    const std::string internal_target = R"(Target="sharedStrings.xml")";
    const std::string external_target =
        R"(Target="https://example.invalid/sharedStrings.xml" TargetMode="External")";
    const std::size_t target_position =
        workbook_relationships->second.find(internal_target);
    check(target_position != std::string::npos,
        "external relationship source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, internal_target.size(), external_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session external sharedStrings relationship flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session external sharedStrings relationship flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session external sharedStrings relationship flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-external-relationship-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-external-relationship-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session external sharedStrings relationship flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session external sharedStrings relationship flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session external sharedStrings relationship flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session external sharedStrings relationship flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session external sharedStrings relationship flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session external sharedStrings relationship flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session external sharedStrings relationship flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session external sharedStrings relationship flush no-op save should keep output byte-stable",
        "multi-session external sharedStrings relationship flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session external sharedStrings relationship flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session external sharedStrings relationship flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_has_fragment()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-fragment-target-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "fragment target source should contain workbook relationships");
    const std::string package_target = R"(Target="sharedStrings.xml")";
    const std::string fragment_target = R"(Target="sharedStrings.xml#fragment")";
    const std::size_t target_position =
        workbook_relationships->second.find(package_target);
    check(target_position != std::string::npos,
        "fragment target source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, package_target.size(), fragment_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session fragment sharedStrings target flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session fragment sharedStrings target flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session fragment sharedStrings target flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-fragment-target-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-fragment-target-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session fragment sharedStrings target flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session fragment sharedStrings target flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session fragment sharedStrings target flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session fragment sharedStrings target flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session fragment sharedStrings target flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session fragment sharedStrings target flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session fragment sharedStrings target flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session fragment sharedStrings target flush no-op save should keep output byte-stable",
        "multi-session fragment sharedStrings target flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session fragment sharedStrings target flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session fragment sharedStrings target flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_percent_escape_is_invalid()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-invalid-percent-target-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "invalid percent target source should contain workbook relationships");
    const std::string package_target = R"(Target="sharedStrings.xml")";
    const std::string invalid_percent_target = R"(Target="sharedStrings%ZZ.xml")";
    const std::size_t target_position =
        workbook_relationships->second.find(package_target);
    check(target_position != std::string::npos,
        "invalid percent target source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, package_target.size(), invalid_percent_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session invalid percent sharedStrings target flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session invalid percent sharedStrings target flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session invalid percent sharedStrings target flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-invalid-percent-target-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-invalid-percent-target-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session invalid percent sharedStrings target flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session invalid percent sharedStrings target flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session invalid percent sharedStrings target flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session invalid percent sharedStrings target flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session invalid percent sharedStrings target flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session invalid percent sharedStrings target flush no-op save should keep output byte-stable",
        "multi-session invalid percent sharedStrings target flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session invalid percent sharedStrings target flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session invalid percent sharedStrings target flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_percent_escape_is_incomplete()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-incomplete-percent-target-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "incomplete percent target source should contain workbook relationships");
    const std::string package_target = R"(Target="sharedStrings.xml")";
    const std::string incomplete_percent_target = R"(Target="sharedStrings%")";
    const std::size_t target_position =
        workbook_relationships->second.find(package_target);
    check(target_position != std::string::npos,
        "incomplete percent target source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, package_target.size(), incomplete_percent_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session incomplete percent sharedStrings target flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session incomplete percent sharedStrings target flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session incomplete percent sharedStrings target flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-incomplete-percent-target-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-incomplete-percent-target-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session incomplete percent sharedStrings target flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session incomplete percent sharedStrings target flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session incomplete percent sharedStrings target flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session incomplete percent sharedStrings target flush no-op save should keep output byte-stable",
        "multi-session incomplete percent sharedStrings target flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session incomplete percent sharedStrings target flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session incomplete percent sharedStrings target flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_decodes_to_null_byte()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-null-byte-target-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "null byte target source should contain workbook relationships");
    const std::string package_target = R"(Target="sharedStrings.xml")";
    const std::string null_byte_target = R"(Target="sharedStrings%00.xml")";
    const std::size_t target_position =
        workbook_relationships->second.find(package_target);
    check(target_position != std::string::npos,
        "null byte target source should contain a sharedStrings relationship target");
    workbook_relationships->second.replace(
        target_position, package_target.size(), null_byte_target);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session null byte sharedStrings target flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session null byte sharedStrings target flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session null byte sharedStrings target flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-null-byte-target-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-null-byte-target-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session null byte sharedStrings target flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session null byte sharedStrings target flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session null byte sharedStrings target flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session null byte sharedStrings target flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session null byte sharedStrings target flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session null byte sharedStrings target flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session null byte sharedStrings target flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session null byte sharedStrings target flush no-op save should keep output byte-stable",
        "multi-session null byte sharedStrings target flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session null byte sharedStrings target flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session null byte sharedStrings target flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_content_type_is_wrong()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-wrong-content-type-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto content_types = entries.find("[Content_Types].xml");
    check(content_types != entries.end(),
        "wrong content-type source should contain content types");
    const std::string shared_strings_content_type =
        R"(ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml")";
    const std::string wrong_content_type =
        R"(ContentType="application/xml")";
    const std::size_t content_type_position =
        content_types->second.find(shared_strings_content_type);
    check(content_type_position != std::string::npos,
        "wrong content-type source should contain a sharedStrings content type");
    content_types->second.replace(
        content_type_position, shared_strings_content_type.size(), wrong_content_type);
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session wrong sharedStrings content type flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session wrong sharedStrings content type flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session wrong sharedStrings content type flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-wrong-content-type-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-wrong-content-type-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_content_types =
        read_stored_package_entry(output, "[Content_Types].xml");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session wrong sharedStrings content type flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session wrong sharedStrings content type flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session wrong sharedStrings content type flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session wrong sharedStrings content type flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session wrong sharedStrings content type flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session wrong sharedStrings content type flush should preserve source sharedStrings bytes");
    check(output_content_types == source_entries.at("[Content_Types].xml"),
        "multi-session wrong sharedStrings content type flush should not repair content types");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session wrong sharedStrings content type flush no-op save should keep output byte-stable",
        "multi-session wrong sharedStrings content type flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session wrong sharedStrings content type flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session wrong sharedStrings content type flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationships_are_duplicate()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-duplicate-relationship-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    auto workbook_relationships =
        entries.find("xl/_rels/workbook.xml.rels");
    check(workbook_relationships != entries.end(),
        "duplicate relationship source should contain workbook relationships");
    const std::string relationship_end = R"(</Relationships>)";
    const std::size_t insert_position =
        workbook_relationships->second.find(relationship_end);
    check(insert_position != std::string::npos,
        "duplicate relationship source should contain a relationships root");
    workbook_relationships->second.insert(insert_position,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)");
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session duplicate sharedStrings relationships flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session duplicate sharedStrings relationships flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session duplicate sharedStrings relationships flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-duplicate-relationship-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-duplicate-relationship-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");
    const std::string shared_strings =
        read_stored_package_entry(output, "xl/sharedStrings.xml");
    const std::string output_workbook_relationships =
        read_stored_package_entry(output, "xl/_rels/workbook.xml.rels");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session duplicate sharedStrings relationships flush should not write Other shared string indexes");
    check(shared_strings == source.shared_strings,
        "multi-session duplicate sharedStrings relationships flush should preserve source sharedStrings bytes");
    check(output_workbook_relationships == source_entries.at("xl/_rels/workbook.xml.rels"),
        "multi-session duplicate sharedStrings relationships flush should not repair workbook relationships");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session duplicate sharedStrings relationships flush no-op save should keep output byte-stable",
        "multi-session duplicate sharedStrings relationships flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session duplicate sharedStrings relationships flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session duplicate sharedStrings relationships flush no-op save should not mutate the source package");
}

void test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_part_is_missing()
{
    MaterializedFlushTwoSheetSourcePackage source =
        write_two_sheet_materialized_flush_source_package(
            "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-missing-source.xlsx",
            true);
    std::map<std::string, std::string> entries =
        read_stored_package_entries(source.path);
    entries.erase("xl/sharedStrings.xml");
    fastxlsx::test::write_stored_zip_entries(source.path, entries);
    const std::map<std::string, std::string> source_entries =
        read_stored_package_entries(source.path);

    fastxlsx::detail::PackageEditor editor =
        fastxlsx::detail::PackageEditor::open(source.path);
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    fastxlsx::detail::MaterializedWorksheetSession& data =
        materialize_session(registry, "Data");
    data.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    data.set_cell(1, 2, fastxlsx::CellValue::text("data <&> text"));
    data.set_cell(1, 3, fastxlsx::CellValue::text("  data spaced  "));

    fastxlsx::detail::MaterializedWorksheetSession& other =
        materialize_session(registry, "Other");
    other.set_cell(1, 1, fastxlsx::CellValue::text("existing"));
    other.set_cell(2, 2, fastxlsx::CellValue::text("other <&> text"));
    other.set_cell(3, 3, fastxlsx::CellValue::text("  other spaced  "));

    const fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    const fastxlsx::detail::WorkbookEditorMaterializedFlushResult result =
        fastxlsx::detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
            editor, registry, catalog);

    check(result.flushed_worksheet_count == 2,
        "multi-session missing sharedStrings flush should report both worksheets");
    check(!data.dirty() && !other.dirty(),
        "multi-session missing sharedStrings flush should clear both sessions");
    check(registry.dirty_session_count() == 0,
        "multi-session missing sharedStrings flush should clear dirty diagnostics");

    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-missing-output.xlsx");
    const std::filesystem::path noop_output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-materialized-flush-two-sheet-shared-missing-noop-output.xlsx");
    editor.save_as(output);

    const std::string data_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet1.xml");
    const std::string other_worksheet =
        read_stored_package_entry(output, "xl/worksheets/sheet2.xml");

    check(data_worksheet.find(R"(<dimension ref="A1:C1"/>)") != std::string::npos,
        "multi-session missing sharedStrings flush should update Data dimensions");
    check(data_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should inline existing Data text");
    check(data_worksheet.find(
              R"(<c r="B1" t="inlineStr"><is><t>data &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should inline escaped Data text");
    check(data_worksheet.find(
              R"(<c r="C1" t="inlineStr"><is><t xml:space="preserve">  data spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should preserve Data whitespace inline");
    check(data_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session missing sharedStrings flush should not write Data shared string indexes");

    check(other_worksheet.find(R"(<dimension ref="A1:C3"/>)") != std::string::npos,
        "multi-session missing sharedStrings flush should update Other dimensions");
    check(other_worksheet.find(
              R"(<c r="A1" t="inlineStr"><is><t>existing</t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should inline existing Other text");
    check(other_worksheet.find(
              R"(<c r="B2" t="inlineStr"><is><t>other &lt;&amp;&gt; text</t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should inline escaped Other text");
    check(other_worksheet.find(
              R"(<c r="C3" t="inlineStr"><is><t xml:space="preserve">  other spaced  </t></is></c>)")
            != std::string::npos,
        "multi-session missing sharedStrings flush should preserve Other whitespace inline");
    check(other_worksheet.find(R"(t="s")") == std::string::npos,
        "multi-session missing sharedStrings flush should not write Other shared string indexes");
    check(!stored_package_has_entry(output, "xl/sharedStrings.xml"),
        "multi-session missing sharedStrings flush should not recreate the missing sharedStrings part");
    check_reopened_multi_session_unsupported_shared_strings_output(output);

    check_materialized_flush_noop_save_is_stable(
        editor,
        registry,
        output,
        noop_output,
        "multi-session missing sharedStrings flush no-op save should keep output byte-stable",
        "multi-session missing sharedStrings flush no-op save should keep registry clean");
    check_reopened_multi_session_unsupported_shared_strings_output(noop_output);
    check(!data.dirty() && !other.dirty(),
        "multi-session missing sharedStrings flush no-op save should keep both sessions clean");
    check(read_stored_package_entries(source.path) == source_entries,
        "multi-session missing sharedStrings flush no-op save should not mutate the source package");
}

} // namespace

int main()
{
    try {
        test_pending_materialized_names_follow_current_catalog_order();
        test_pending_materialized_names_ignore_stale_source_names();
        test_dirty_sheet_data_projections_include_only_dirty_sessions_and_dimensions();
        test_dirty_sheet_data_projection_uses_shared_string_index_provider();
        test_materialized_session_insert_rows_translates_formula_records();
        test_materialized_session_delete_rows_rewrites_formula_records();
        test_materialized_session_delete_columns_rewrites_formula_records();
        test_materialized_session_insert_columns_translates_formula_records();
        test_materialized_session_insert_row_overflow_preserves_state();
        test_materialized_session_insert_column_overflow_preserves_state();
        test_materialized_session_invalid_row_spans_preserve_state();
        test_materialized_session_invalid_column_spans_preserve_state();
        test_materialized_session_zero_count_structural_edits_preserve_clean_state();
        test_materialized_session_zero_count_structural_edits_preserve_dirty_state();
        test_dirty_worksheet_projection_uses_shared_string_index_provider();
        test_dirty_sheet_data_projection_provider_failure_keeps_session_dirty();
        test_dirty_worksheet_projection_provider_failure_keeps_session_dirty();
        test_dirty_sheet_data_projection_provider_skips_non_text_records();
        test_dirty_worksheet_projection_provider_skips_non_text_records();
        test_dirty_empty_projection_after_erasing_all_source_cells();
        test_materialized_flush_target_validation_accepts_current_names();
        test_materialized_flush_target_validation_uses_current_catalog_names();
        test_materialized_flush_target_validation_rejects_missing_names();
        test_materialized_flush_to_patch_plan_clears_dirty_sessions();
        test_materialized_flush_rejects_stale_targets_without_clearing_dirty();
        test_materialized_flush_handles_multiple_dirty_sessions();
        test_materialized_flush_appends_shared_strings_projection();
        test_materialized_flush_reuses_existing_shared_strings_without_rewrite();
        test_materialized_flush_falls_back_to_inline_when_shared_strings_append_is_unsupported();
        test_materialized_flush_falls_back_to_inline_when_shared_strings_load_fails();
        test_materialized_flush_falls_back_to_inline_when_shared_strings_part_is_missing();
        test_materialized_flush_deduplicates_appended_shared_strings();
        test_materialized_flush_reuses_shared_strings_across_multiple_dirty_sessions();
        test_materialized_flush_shared_strings_skips_non_text_dirty_sessions();
        test_materialized_flush_reuses_existing_shared_strings_across_multiple_dirty_sessions();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_append_is_unsupported();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_load_fails();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationship_is_stale();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationship_is_external();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_has_fragment();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_percent_escape_is_invalid();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_percent_escape_is_incomplete();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_target_decodes_to_null_byte();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_content_type_is_wrong();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_relationships_are_duplicate();
        test_materialized_flush_multi_session_falls_back_to_inline_when_shared_strings_part_is_missing();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
