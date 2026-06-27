// Structure tests for WorkbookEditor formula reference diagnostics and rename rewrite policy.
// These stay at the public-API level and split the formula-heavy coverage out of
// the larger WorkbookEditor facade test file.

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n",
            static_cast<int>(message.size()), message.data());
    }
}

void check_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    if (haystack.find(needle) == std::string::npos) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n  missing: %.*s\n  in: %s\n",
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(needle.size()), needle.data(), haystack.c_str());
    }
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) == std::string::npos, message);
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

void write_stored_zip_entries(
    const std::filesystem::path& path,
    const std::map<std::string, std::string>& entries)
{
    fastxlsx::test::write_stored_zip_entries(path, entries);
}

void rewrite_package_entry_as_stored(
    const std::filesystem::path& path,
    std::string_view entry_name,
    std::string replacement)
{
    fastxlsx::test::rewrite_package_entry_as_stored(
        path, entry_name, std::move(replacement));
}

void replace_first_or_throw(
    std::string& text, std::string_view needle, std::string_view replacement)
{
    const std::size_t position = text.find(needle);
    if (position == std::string::npos) {
        throw std::runtime_error("test fixture replacement marker was not found");
    }
    text.replace(position, needle.size(), replacement);
}

bool workbook_editor_catalog_entries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].source_name != rhs[index].source_name
            || lhs[index].planned_name != rhs[index].planned_name
            || lhs[index].renamed != rhs[index].renamed) {
            return false;
        }
    }
    return true;
}

bool workbook_editor_edit_summaries_equal(
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& lhs,
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto& left = lhs[index];
        const auto& right = rhs[index];
        if (left.source_name != right.source_name
            || left.planned_name != right.planned_name
            || left.renamed != right.renamed
            || left.sheet_data_replaced != right.sheet_data_replaced
            || left.replacement_cell_count != right.replacement_cell_count
            || left.estimated_replacement_memory_usage
                != right.estimated_replacement_memory_usage
            || left.materialized_dirty != right.materialized_dirty
            || left.materialized_cell_count != right.materialized_cell_count
            || left.estimated_materialized_memory_usage
                != right.estimated_materialized_memory_usage) {
            return false;
        }
    }
    return true;
}

struct WorkbookEditorPublicCatalogSnapshot {
    std::vector<std::string> source_names;
    std::vector<std::string> planned_names;
    std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog;
};

WorkbookEditorPublicCatalogSnapshot workbook_editor_public_catalog_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.source_worksheet_names(),
        editor.worksheet_names(),
        editor.worksheet_catalog(),
    };
}

void check_workbook_editor_public_catalog_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicCatalogSnapshot& before,
    std::string_view scenario)
{
    const std::string prefix(scenario);
    check(editor.source_worksheet_names() == before.source_names,
        prefix + " should preserve source worksheet names");
    check(editor.worksheet_names() == before.planned_names,
        prefix + " should preserve planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), before.catalog),
        prefix + " should preserve worksheet catalog");
}

struct WorkbookEditorPublicSaveStateSnapshot {
    std::size_t pending_change_count{};
    std::size_t pending_replacement_cell_count{};
    std::size_t estimated_pending_replacement_memory_usage{};
    std::vector<std::string> pending_replacement_worksheet_names;
    std::optional<std::string> last_edit_error;
};

WorkbookEditorPublicSaveStateSnapshot workbook_editor_public_save_state_snapshot(
    const fastxlsx::WorkbookEditor& editor)
{
    return {
        editor.pending_change_count(),
        editor.pending_replacement_cell_count(),
        editor.estimated_pending_replacement_memory_usage(),
        editor.pending_replacement_worksheet_names(),
        editor.last_edit_error(),
    };
}

void check_workbook_editor_public_save_state_preserved(
    const fastxlsx::WorkbookEditor& editor,
    const WorkbookEditorPublicSaveStateSnapshot& before,
    std::string_view scenario)
{
    check(editor.pending_change_count() == before.pending_change_count,
        std::string(scenario) + " should preserve public pending change count");
    check(editor.pending_replacement_cell_count()
            == before.pending_replacement_cell_count,
        std::string(scenario) + " should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage()
            == before.estimated_pending_replacement_memory_usage,
        std::string(scenario) + " should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names()
            == before.pending_replacement_worksheet_names,
        std::string(scenario) + " should preserve pending replacement worksheet names");
    check(editor.last_edit_error() == before.last_edit_error,
        std::string(scenario) + " should not replace or clear last_edit_error");
}

void check_workbook_editor_public_no_pending_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(!editor.has_pending_changes(), prefix + " should keep the editor clean");
    check(editor.pending_change_count() == 0,
        prefix + " should keep pending edit count empty");
    check(editor.pending_worksheet_edits().empty(),
        prefix + " should keep pending summaries empty");
}

void check_public_inspection_preserves_last_edit_error(
    fastxlsx::WorkbookEditor& editor, const std::optional<std::string>& expected)
{
    const WorkbookEditorPublicCatalogSnapshot catalog_before =
        workbook_editor_public_catalog_snapshot(editor);
    auto check_inspection_state = [&](std::string_view api_name) {
        const std::string prefix(api_name);
        check(editor.last_edit_error() == expected,
            prefix + " should not update last_edit_error");
        check_workbook_editor_public_catalog_preserved(
            editor, catalog_before, prefix);
    };

    (void)editor.worksheet_names();
    check_inspection_state("worksheet_names");

    (void)editor.has_worksheet("Data");
    check_inspection_state("has_worksheet");

    (void)editor.source_worksheet_names();
    check_inspection_state("source_worksheet_names");

    (void)editor.has_source_worksheet("Data");
    check_inspection_state("has_source_worksheet");

    (void)editor.has_pending_changes();
    check_inspection_state("has_pending_changes");

    (void)editor.pending_change_count();
    check_inspection_state("pending_change_count");

    (void)editor.pending_replacement_cell_count();
    check_inspection_state("pending_replacement_cell_count");

    (void)editor.pending_replacement_worksheet_names();
    check_inspection_state("pending_replacement_worksheet_names");

    (void)editor.pending_materialized_worksheet_names();
    check_inspection_state("pending_materialized_worksheet_names");

    (void)editor.pending_materialized_cell_count();
    check_inspection_state("pending_materialized_cell_count");

    (void)editor.estimated_pending_materialized_memory_usage();
    check_inspection_state("estimated_pending_materialized_memory_usage");

    (void)editor.has_pending_replacement("Data");
    check_inspection_state("has_pending_replacement");

    (void)editor.estimated_pending_replacement_memory_usage();
    check_inspection_state("estimated_pending_replacement_memory_usage");

    (void)editor.pending_worksheet_edits();
    check_inspection_state("pending_worksheet_edits");

    (void)editor.worksheet_catalog();
    check_inspection_state("worksheet_catalog");

    (void)editor.formula_reference_audits();
    check_inspection_state("formula_reference_audits");

    (void)editor.source_formula_reference_audits();
    check_inspection_state("source_formula_reference_audits");

    (void)editor.defined_name_formula_reference_audits();
    check_inspection_state("defined_name_formula_reference_audits");
}

void seed_invalid_rename_last_edit_error(
    fastxlsx::WorkbookEditor& editor,
    std::string_view source_name,
    std::string_view scenario)
{
    const std::string prefix = std::string(scenario);

    check(threw_fastxlsx_error([&] {
            editor.rename_sheet(std::string(source_name), "Bad/Name");
        }),
        prefix + " should reject an invalid sheet name before the successful edit");
    check(editor.last_edit_error().has_value(),
        prefix + " should seed last_edit_error before the successful edit");
}

std::filesystem::path write_formula_reference_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
    }
    {
        fastxlsx::WorksheetWriter other = writer.add_worksheet("Other Sheet");
        other.append_row({fastxlsx::CellView::number(2.0)});
    }
    {
        fastxlsx::WorksheetWriter apostrophe = writer.add_worksheet("O'Brien");
        apostrophe.append_row({fastxlsx::CellView::number(3.0)});
    }
    {
        fastxlsx::WorksheetWriter formulas = writer.add_worksheet("Formula");
        formulas.append_row({fastxlsx::CellView::formula(
            R"(Data!A1+'Other Sheet'!A1+'O''Brien'!A1+[Book.xlsx]Data!A1+Data:Formula!A1+"Data!Z9")")});
    }
    writer.close();

    return path;
}

std::filesystem::path write_shared_formula_reference_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0),
            fastxlsx::CellView::number(2.0)});
        data.append_row({fastxlsx::CellView::number(3.0),
            fastxlsx::CellView::number(4.0)});
    }
    {
        fastxlsx::WorksheetWriter formulas = writer.add_worksheet("Formula");
        formulas.append_row({fastxlsx::CellView::formula("Data!A1")});
    }
    writer.close();

    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);
    std::string& worksheet_xml = entries.at("xl/worksheets/sheet2.xml");
    const std::size_t dimension_begin = worksheet_xml.find(R"(<dimension ref=")");
    if (dimension_begin != std::string::npos) {
        const std::size_t dimension_end = worksheet_xml.find("/>", dimension_begin);
        if (dimension_end == std::string::npos) {
            throw std::runtime_error("shared formula fixture dimension end was not found");
        }
        worksheet_xml.replace(dimension_begin,
            dimension_end + std::string_view("/>").size() - dimension_begin,
            R"(<dimension ref="A1:B2"/>)");
    }

    const std::string shared_sheet_data =
        R"(<sheetData><row r="1"><c r="A1"><f t="shared" ref="A1:B2" si="0">Data!A1</f></c><c r="B1"><f t="shared" si="0"/></c></row><row r="2"><c r="A2"><f t="shared" si="0"/></c><c r="B2"><f t="shared" si="0"/></c></row></sheetData>)";
    const std::size_t sheet_data_begin = worksheet_xml.find("<sheetData>");
    const std::size_t sheet_data_end = worksheet_xml.find("</sheetData>", sheet_data_begin);
    if (sheet_data_begin == std::string::npos || sheet_data_end == std::string::npos) {
        throw std::runtime_error("shared formula fixture source worksheet sheetData was not found");
    }
    worksheet_xml.replace(sheet_data_begin,
        sheet_data_end + std::string_view("</sheetData>").size() - sheet_data_begin,
        shared_sheet_data);
    write_stored_zip_entries(path, entries);

    return path;
}

std::filesystem::path write_multi_formula_reference_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
    }
    {
        fastxlsx::WorksheetWriter formulas = writer.add_worksheet("Formula One");
        formulas.append_row(
            {fastxlsx::CellView::formula("Data!A1+Data!A1")});
    }
    {
        fastxlsx::WorksheetWriter formulas = writer.add_worksheet("Formula Two");
        formulas.append_row({fastxlsx::CellView::formula(
            "Data!A1+'Data'!A1+[Book.xlsx]Data!A1")});
    }
    {
        fastxlsx::WorksheetWriter plain = writer.add_worksheet("Plain");
        plain.append_row({fastxlsx::CellView::text("no formula")});
    }
    writer.close();

    return path;
}

std::filesystem::path write_defined_name_reference_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::number(1.0)});
    }
    {
        fastxlsx::WorksheetWriter other = writer.add_worksheet("Other Sheet");
        other.append_row({fastxlsx::CellView::number(2.0)});
    }
    {
        fastxlsx::WorksheetWriter formulas = writer.add_worksheet("Formula");
        formulas.append_row({fastxlsx::CellView::formula("Data!A1")});
    }
    writer.close();

    std::string workbook_xml =
        fastxlsx::test::read_zip_entries(path).at("xl/workbook.xml");
    replace_first_or_throw(workbook_xml, "</workbook>",
        R"(<definedNames>)"
        R"(<definedName name="ReportRange">Data!$A$1:$B$2</definedName>)"
        R"(<definedName name="ScopedRange" localSheetId="2">'Other Sheet'!$A$1</definedName>)"
        R"(<definedName name="ExternalRef">[Book.xlsx]Data!A1</definedName>)"
        R"(<definedName name="ThreeDRef">Data:Formula!A1</definedName>)"
        R"(</definedNames></workbook>)");
    rewrite_package_entry_as_stored(path, "xl/workbook.xml", std::move(workbook_xml));

    return path;
}

std::filesystem::path write_case_varied_formula_reference_source(std::string_view name)
{
    const std::filesystem::path path = write_defined_name_reference_source(name);

    std::string workbook_xml =
        fastxlsx::test::read_zip_entries(path).at("xl/workbook.xml");
    replace_first_or_throw(workbook_xml, "Data!$A$1:$B$2", "data!$A$1+DATA!$B$2");
    replace_first_or_throw(workbook_xml, "[Book.xlsx]Data!A1", "[Book.xlsx]data!A1");
    replace_first_or_throw(workbook_xml, "Data:Formula!A1", "data:Formula!A1");
    rewrite_package_entry_as_stored(path, "xl/workbook.xml", std::move(workbook_xml));

    std::string formula_sheet_xml =
        fastxlsx::test::read_zip_entries(path).at("xl/worksheets/sheet3.xml");
    replace_first_or_throw(formula_sheet_xml, "Data!A1",
        R"(data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1")");
    rewrite_package_entry_as_stored(path, "xl/worksheets/sheet3.xml",
        std::move(formula_sheet_xml));

    return path;
}

const fastxlsx::WorkbookEditorFormulaReferenceAudit* find_formula_reference_audit(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::string_view referenced_sheet_name)
{
    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : audits) {
        if (audit.referenced_sheet_name == referenced_sheet_name) {
            return &audit;
        }
    }
    return nullptr;
}

const fastxlsx::WorkbookEditorFormulaReferenceAudit* find_formula_reference_audit_at(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::string_view referenced_sheet_name,
    std::uint32_t row,
    std::uint32_t column)
{
    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : audits) {
        if (audit.referenced_sheet_name == referenced_sheet_name
            && audit.formula_cell.row == row
            && audit.formula_cell.column == column) {
            return &audit;
        }
    }
    return nullptr;
}

std::size_t count_formula_reference_audits(
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits,
    std::string_view referenced_sheet_name)
{
    std::size_t count = 0;
    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : audits) {
        if (audit.referenced_sheet_name == referenced_sheet_name) {
            ++count;
        }
    }
    return count;
}

const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit*
find_defined_name_formula_reference_audit(
    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>& audits,
    std::string_view defined_name,
    std::string_view referenced_sheet_name)
{
    for (const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit& audit : audits) {
        if (audit.defined_name == defined_name
            && audit.referenced_sheet_name == referenced_sheet_name) {
            return &audit;
        }
    }
    return nullptr;
}

void test_formula_reference_audits_report_renamed_source_sheet_risk()
{
    const std::filesystem::path source = write_formula_reference_source(
        "fastxlsx-workbook-editor-formula-reference-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-formula-reference-audit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::size_t non_materialized_pending_change_count_before_audit =
        editor.pending_change_count();
    const bool non_materialized_has_pending_changes_before_audit =
        editor.has_pending_changes();
    const std::vector<std::string> non_materialized_pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> non_materialized_pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t non_materialized_pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> non_materialized_last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> non_materialized_audits =
        editor.formula_reference_audits();
    check(non_materialized_audits.empty(),
        "formula reference audit should not scan non-materialized worksheets");
    check(editor.pending_change_count() == non_materialized_pending_change_count_before_audit,
        "non-materialized formula audit should not increment public edit count");
    check(editor.has_pending_changes() == non_materialized_has_pending_changes_before_audit,
        "non-materialized formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() ==
            non_materialized_pending_replacement_names_before_audit,
        "non-materialized formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() ==
            non_materialized_pending_materialized_names_before_audit,
        "non-materialized formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() ==
            non_materialized_pending_summary_count_before_audit,
        "non-materialized formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == non_materialized_last_edit_error_before_audit,
        "non-materialized formula audit should not update last_edit_error");

    (void)editor.worksheet("Formula");
    const std::size_t initial_pending_change_count_before_audit =
        editor.pending_change_count();
    const bool initial_has_pending_changes_before_audit =
        editor.has_pending_changes();
    const std::vector<std::string> initial_pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> initial_pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t initial_pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> initial_last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> initial_audits =
        editor.formula_reference_audits();
    check(editor.pending_change_count() == initial_pending_change_count_before_audit,
        "initial materialized formula audit should not increment public edit count");
    check(editor.has_pending_changes() == initial_has_pending_changes_before_audit,
        "initial materialized formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() ==
            initial_pending_replacement_names_before_audit,
        "initial materialized formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() ==
            initial_pending_materialized_names_before_audit,
        "initial materialized formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == initial_pending_summary_count_before_audit,
        "initial materialized formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == initial_last_edit_error_before_audit,
        "initial materialized formula audit should not update last_edit_error");
    check(initial_audits.size() == 5,
        "materialized formula sheet should expose all sheet-qualified references");
    {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* data =
            find_formula_reference_audit(initial_audits, "Data");
        check(data != nullptr,
            "formula audit should include the unquoted Data sheet reference");
        if (data != nullptr) {
            check(data->formula_sheet_source_name == "Formula" &&
                    data->formula_sheet_planned_name == "Formula",
                "formula audit should report the formula sheet context");
            check(data->formula_cell.row == 1 && data->formula_cell.column == 1,
                "formula audit should report the formula cell coordinate");
            check(data->sheet_qualifier_text == "Data!" && !data->qualifier_quoted,
                "formula audit should keep the raw unquoted qualifier text");
            check(data->reference_text == "A1" &&
                    data->qualified_reference_text == "Data!A1",
                "formula audit should expose the exact unquoted reference token");
            check(data->matched_current_workbook_sheet &&
                    data->matched_source_sheet_name == "Data" &&
                    data->matched_planned_sheet_name == "Data",
                "formula audit should match source and planned names before rename");
            check(!data->references_renamed_source_name &&
                    data->references_planned_sheet_name,
                "formula audit should not flag unchanged sheet references as stale");
        }

        const fastxlsx::WorkbookEditorFormulaReferenceAudit* apostrophe =
            find_formula_reference_audit(initial_audits, "O'Brien");
        check(apostrophe != nullptr,
            "formula audit should decode escaped apostrophes in quoted sheet names");
        if (apostrophe != nullptr) {
            check(apostrophe->sheet_qualifier_text == "'O''Brien'!" &&
                    apostrophe->qualifier_quoted,
                "formula audit should preserve quoted qualifier text");
            check(apostrophe->reference_text == "A1" &&
                    apostrophe->qualified_reference_text == "'O''Brien'!A1",
                "formula audit should expose the exact quoted reference token");
            check(apostrophe->matched_current_workbook_sheet &&
                    apostrophe->matched_source_sheet_name == "O'Brien",
                "formula audit should match decoded quoted sheet names");
        }

        const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
            find_formula_reference_audit(initial_audits, "[Book.xlsx]Data");
        check(external != nullptr,
            "formula audit should include external workbook sheet qualifiers");
        if (external != nullptr) {
            check(external->external_workbook_qualifier &&
                    !external->sheet_range_qualifier,
                "formula audit should classify external workbook qualifiers");
            check(external->reference_text == "A1" &&
                    external->qualified_reference_text == "[Book.xlsx]Data!A1",
                "formula audit should expose exact external reference text");
            check(!external->matched_current_workbook_sheet,
                "formula audit should not match external workbook qualifiers to local sheets");
        }

        const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
            find_formula_reference_audit(initial_audits, "Data:Formula");
        check(sheet_range != nullptr,
            "formula audit should include 3D sheet-range qualifiers");
        if (sheet_range != nullptr) {
            check(!sheet_range->external_workbook_qualifier &&
                    sheet_range->sheet_range_qualifier,
                "formula audit should classify 3D sheet-range qualifiers");
            check(sheet_range->reference_text == "A1" &&
                    sheet_range->qualified_reference_text == "Data:Formula!A1",
                "formula audit should expose exact 3D reference text");
            check(!sheet_range->matched_current_workbook_sheet,
                "formula audit should not match 3D sheet ranges to one local sheet");
        }
    }

    editor.rename_sheet("Data", "RenamedData");
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> renamed_audits =
        editor.formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_audit,
        "renamed materialized formula audit should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        "renamed materialized formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() == pending_replacement_names_before_audit,
        "renamed materialized formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == pending_materialized_names_before_audit,
        "renamed materialized formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == pending_summary_count_before_audit,
        "renamed materialized formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == last_edit_error_before_audit,
        "renamed materialized formula audit should not update last_edit_error");
    check(renamed_audits.size() == 5,
        "rename should not drop materialized formula reference audit entries");
    {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* data =
            find_formula_reference_audit(renamed_audits, "Data");
        check(data != nullptr,
            "renamed audit should still include the formula text's source-name reference");
        if (data != nullptr) {
            check(data->matched_current_workbook_sheet &&
                    data->matched_source_sheet_name == "Data" &&
                    data->matched_planned_sheet_name == "RenamedData",
                "renamed audit should map the source name to the current planned name");
            check(data->references_renamed_source_name,
                "renamed audit should flag formulas that still reference the source sheet name");
            check(!data->references_planned_sheet_name,
                "renamed audit should distinguish source-name references from planned-name references");
        }

        const fastxlsx::WorkbookEditorFormulaReferenceAudit* other =
            find_formula_reference_audit(renamed_audits, "Other Sheet");
        check(other != nullptr,
            "renamed audit should keep unrelated quoted sheet references");
        if (other != nullptr) {
            check(other->matched_current_workbook_sheet &&
                    other->matched_source_sheet_name == "Other Sheet" &&
                    other->matched_planned_sheet_name == "Other Sheet",
                "renamed audit should keep unchanged sheet mappings");
            check(other->reference_text == "A1" &&
                    other->qualified_reference_text == "'Other Sheet'!A1",
                "renamed audit should keep exact unchanged quoted reference text");
            check(!other->references_renamed_source_name &&
                    other->references_planned_sheet_name,
                "renamed audit should not flag unchanged quoted sheet references");
        }
    }

    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "rename should still update only the workbook catalog");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet4.xml");
    check_contains(formula_sheet_xml, "Data!A1",
        "formula reference audit should not rewrite formula text during save");
    check_not_contains(formula_sheet_xml, "RenamedData!A1",
        "formula reference audit should not silently repair renamed sheet formulas");
}

void test_source_formula_reference_audits_report_non_materialized_rename_risk()
{
    const std::filesystem::path source = write_formula_reference_source(
        "fastxlsx-workbook-editor-source-formula-reference-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-formula-reference-audit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.formula_reference_audits().empty(),
        "materialized formula audit should stay empty before worksheet() is called");
    const std::size_t pending_change_count_before_source_audit =
        editor.pending_change_count();
    const bool has_pending_changes_before_source_audit = editor.has_pending_changes();
    const std::vector<std::string> pending_replacement_names_before_source_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> pending_materialized_names_before_source_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_summary_count_before_source_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> last_edit_error_before_source_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> initial_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_source_audit,
        "source formula audit should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_source_audit,
        "source formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() ==
            pending_replacement_names_before_source_audit,
        "source formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() ==
            pending_materialized_names_before_source_audit,
        "source formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == pending_summary_count_before_source_audit,
        "source formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == last_edit_error_before_source_audit,
        "source formula audit should not update last_edit_error");
    check(initial_audits.size() == 5,
        "source formula audit should scan non-materialized source worksheet formulas");
    {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* data =
            find_formula_reference_audit(initial_audits, "Data");
        check(data != nullptr,
            "source formula audit should include the unquoted Data sheet reference");
        if (data != nullptr) {
            check(data->formula_sheet_source_name == "Formula" &&
                    data->formula_sheet_planned_name == "Formula",
                "source formula audit should report the source formula sheet context");
            check(data->formula_cell.row == 1 && data->formula_cell.column == 1,
                "source formula audit should parse the formula cell coordinate");
            check(data->formula_text.find("Data!A1") != std::string::npos,
                "source formula audit should report decoded formula text");
            check(data->matched_current_workbook_sheet &&
                    data->matched_source_sheet_name == "Data" &&
                    data->matched_planned_sheet_name == "Data",
                "source formula audit should match source and planned names before rename");
            check(!data->references_renamed_source_name &&
                    data->references_planned_sheet_name,
                "source formula audit should not flag unchanged source references");
        }

        const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
            find_formula_reference_audit(initial_audits, "[Book.xlsx]Data");
        check(external != nullptr && external->external_workbook_qualifier,
            "source formula audit should classify external workbook qualifiers");
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
            find_formula_reference_audit(initial_audits, "Data:Formula");
        check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
            "source formula audit should classify 3D sheet-range qualifiers");
    }

    editor.rename_sheet("Data", "RenamedData");
    check(editor.formula_reference_audits().empty(),
        "source formula audit should not materialize worksheet formula sessions");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> renamed_audits =
        editor.source_formula_reference_audits();
    check(renamed_audits.size() == 5,
        "rename should not drop source formula audit entries");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* renamed_data =
        find_formula_reference_audit(renamed_audits, "Data");
    check(renamed_data != nullptr,
        "renamed source formula audit should still expose the source-name formula text");
    if (renamed_data != nullptr) {
        check(renamed_data->matched_current_workbook_sheet &&
                renamed_data->matched_source_sheet_name == "Data" &&
                renamed_data->matched_planned_sheet_name == "RenamedData",
            "renamed source formula audit should map the old source name to the planned name");
        check(renamed_data->references_renamed_source_name &&
                !renamed_data->references_planned_sheet_name,
            "renamed source formula audit should flag non-materialized stale formulas");
    }

    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "source formula audit should still let rename persist the workbook catalog");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet4.xml");
    check_contains(formula_sheet_xml, "Data!A1",
        "source formula audit should not rewrite non-materialized source worksheet formulas");
    check_not_contains(formula_sheet_xml, "RenamedData!A1",
        "source formula audit should not silently repair non-materialized formulas");
}

void test_source_formula_reference_audits_report_case_varied_default_rename_risk()
{
    const std::filesystem::path source = write_case_varied_formula_reference_source(
        "fastxlsx-workbook-editor-source-case-varied-formula-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-case-varied-formula-audit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.formula_reference_audits().empty(),
        "case-varied source formula audit should not start with materialized sessions");

    editor.rename_sheet("Data", "Renamed & Data");
    check(editor.formula_reference_audits().empty(),
        "case-varied source formula audit should not materialize worksheet formulas");
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> renamed_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_audit,
        "case-varied source formula audit should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        "case-varied source formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() == pending_replacement_names_before_audit,
        "case-varied source formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == pending_materialized_names_before_audit,
        "case-varied source formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == pending_summary_count_before_audit,
        "case-varied source formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == last_edit_error_before_audit,
        "case-varied source formula audit should not update last_edit_error");
    const auto check_stale_source_formula_ref = [&] (std::string_view spelling) {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
            find_formula_reference_audit(renamed_audits, spelling);
        check(audit != nullptr,
            std::string("case-varied source formula audit should include ") +
                std::string(spelling));
        if (audit != nullptr) {
            check(audit->formula_sheet_source_name == "Formula" &&
                    audit->formula_sheet_planned_name == "Formula",
                std::string("case-varied source formula audit should report Formula context for ") +
                    std::string(spelling));
            check(audit->formula_cell.row == 1 && audit->formula_cell.column == 1,
                std::string("case-varied source formula audit should report A1 for ") +
                    std::string(spelling));
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Renamed & Data",
                std::string("case-varied source formula audit should map ") +
                    std::string(spelling) + " to the renamed catalog entry");
            check(audit->references_renamed_source_name &&
                    !audit->references_planned_sheet_name,
                std::string("case-varied source formula audit should flag ") +
                    std::string(spelling) + " as stale source-name text");
        }
    };
    check_stale_source_formula_ref("data");
    check_stale_source_formula_ref("DATA");
    check(count_formula_reference_audits(renamed_audits, "Renamed & Data") == 0,
        "case-varied source formula audit should not invent planned-name refs");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
        find_formula_reference_audit(renamed_audits, "[Book.xlsx]data");
    check(external != nullptr && external->external_workbook_qualifier,
        "case-varied source formula audit should classify external workbook qualifiers");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
        find_formula_reference_audit(renamed_audits, "data:Formula");
    check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
        "case-varied source formula audit should classify 3D sheet-range qualifiers");

    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed &amp; Data")",
        "case-varied source formula audit should still let rename persist the catalog");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet3.xml");
    check_contains(formula_sheet_xml,
        R"(<f>data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1"</f>)",
        "case-varied source formula audit should preserve non-materialized formula XML");
    check_not_contains(formula_sheet_xml, "Renamed &amp; Data'!A1",
        "case-varied source formula audit should not rewrite non-materialized formulas");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value()
                == R"(data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1")",
        "reopened source-audit output should expose original non-materialized formula text");
}

void test_source_formula_reference_audits_translate_shared_formula_followers()
{
    const std::filesystem::path source = write_shared_formula_reference_source(
        "fastxlsx-workbook-editor-source-shared-formula-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-shared-formula-audit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::size_t initial_pending_change_count_before_audit =
        editor.pending_change_count();
    const bool initial_has_pending_changes_before_audit =
        editor.has_pending_changes();
    const std::vector<std::string> initial_pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> initial_pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t initial_pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> initial_last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> initial_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == initial_pending_change_count_before_audit,
        "source shared formula audit should not increment public edit count");
    check(editor.has_pending_changes() == initial_has_pending_changes_before_audit,
        "source shared formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() ==
            initial_pending_replacement_names_before_audit,
        "source shared formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() ==
            initial_pending_materialized_names_before_audit,
        "source shared formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == initial_pending_summary_count_before_audit,
        "source shared formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == initial_last_edit_error_before_audit,
        "source shared formula audit should not update last_edit_error");
    check(initial_audits.size() == 4,
        "source formula audit should expand source-order shared formula followers");
    check(editor.formula_reference_audits().empty(),
        "source shared formula audit should not materialize worksheet sessions");

    {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* a1 =
            find_formula_reference_audit_at(initial_audits, "Data", 1, 1);
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* b1 =
            find_formula_reference_audit_at(initial_audits, "Data", 1, 2);
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* a2 =
            find_formula_reference_audit_at(initial_audits, "Data", 2, 1);
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* b2 =
            find_formula_reference_audit_at(initial_audits, "Data", 2, 2);

        check(a1 != nullptr && a1->formula_text == "Data!A1" &&
                a1->qualified_reference_text == "Data!A1",
            "source shared formula audit should report the definition formula");
        check(b1 != nullptr && b1->formula_text == "Data!B1" &&
                b1->qualified_reference_text == "Data!B1",
            "source shared formula audit should translate the B1 follower");
        check(a2 != nullptr && a2->formula_text == "Data!A2" &&
                a2->qualified_reference_text == "Data!A2",
            "source shared formula audit should translate the A2 follower");
        check(b2 != nullptr && b2->formula_text == "Data!B2" &&
                b2->qualified_reference_text == "Data!B2",
            "source shared formula audit should translate the B2 follower");
    }

    editor.rename_sheet("Data", "RenamedData");
    const std::size_t renamed_pending_change_count_before_audit =
        editor.pending_change_count();
    const bool renamed_has_pending_changes_before_audit =
        editor.has_pending_changes();
    const std::vector<std::string> renamed_pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> renamed_pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t renamed_pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> renamed_last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> renamed_audits =
        editor.source_formula_reference_audits();
    check(editor.pending_change_count() == renamed_pending_change_count_before_audit,
        "renamed source shared formula audit should not increment public edit count");
    check(editor.has_pending_changes() == renamed_has_pending_changes_before_audit,
        "renamed source shared formula audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() ==
            renamed_pending_replacement_names_before_audit,
        "renamed source shared formula audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() ==
            renamed_pending_materialized_names_before_audit,
        "renamed source shared formula audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == renamed_pending_summary_count_before_audit,
        "renamed source shared formula audit should not create pending edit summaries");
    check(editor.last_edit_error() == renamed_last_edit_error_before_audit,
        "renamed source shared formula audit should not update last_edit_error");
    check(renamed_audits.size() == 4,
        "rename should keep expanded source shared formula audit coverage");
    check(count_formula_reference_audits(renamed_audits, "Data") == 4,
        "renamed source shared formula audit should still expose source-name references");
    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : renamed_audits) {
        check(audit.matched_current_workbook_sheet &&
                audit.matched_source_sheet_name == "Data" &&
                audit.matched_planned_sheet_name == "RenamedData",
            "renamed source shared formula audit should map followers to the planned sheet");
        check(audit.references_renamed_source_name &&
                !audit.references_planned_sheet_name,
            "renamed source shared formula audit should flag each follower as stale");
    }

    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(formula_sheet_xml, R"(<f t="shared" ref="A1:B2" si="0">Data!A1</f>)",
        "source shared formula audit should preserve the source shared formula definition");
    check_contains(formula_sheet_xml, R"(<f t="shared" si="0"/>)",
        "source shared formula audit should preserve metadata-only shared formula followers");
    check_not_contains(formula_sheet_xml, "RenamedData",
        "source shared formula audit should not rewrite non-materialized shared formulas");
}

void test_defined_name_formula_reference_audits_report_renamed_source_sheet_risk()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-defined-name-reference-audit-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-defined-name-reference-audit-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit> initial_audits =
        editor.defined_name_formula_reference_audits();
    check(initial_audits.size() == 4,
        "definedName audit should expose all sheet-qualified definedName references");
    {
        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* report =
            find_defined_name_formula_reference_audit(initial_audits, "ReportRange", "Data");
        check(report != nullptr,
            "definedName audit should include workbook-scope Data reference");
        if (report != nullptr) {
            check(report->defined_name == "ReportRange" &&
                    report->formula_text == "Data!$A$1:$B$2",
                "definedName audit should report name and formula text");
            check(!report->local_sheet_scope &&
                    !report->local_sheet_scope_resolved,
                "workbook-scope definedName should not report local sheet scope");
            check(report->sheet_qualifier_text == "Data!" &&
                    report->reference_text == "$A$1:$B$2" &&
                    report->qualified_reference_text == "Data!$A$1:$B$2",
                "definedName audit should expose exact reference text");
            check(report->matched_current_workbook_sheet &&
                    report->matched_source_sheet_name == "Data" &&
                    report->matched_planned_sheet_name == "Data",
                "definedName audit should match source and planned sheet names before rename");
            check(!report->references_renamed_source_name &&
                    report->references_planned_sheet_name,
                "definedName audit should not flag unchanged workbook-scope references");
        }

        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* scoped =
            find_defined_name_formula_reference_audit(
                initial_audits, "ScopedRange", "Other Sheet");
        check(scoped != nullptr,
            "definedName audit should include locally scoped references");
        if (scoped != nullptr) {
            check(scoped->local_sheet_scope &&
                    scoped->local_sheet_id_text == "2" &&
                    scoped->local_sheet_scope_resolved,
                "definedName audit should expose resolved localSheetId scope");
            check(scoped->scope_sheet_source_name == "Formula" &&
                    scoped->scope_sheet_planned_name == "Formula",
                "definedName audit should map localSheetId through catalog order");
            check(scoped->qualifier_quoted &&
                    scoped->qualified_reference_text == "'Other Sheet'!$A$1",
                "definedName audit should preserve quoted reference text");
        }

        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* external =
            find_defined_name_formula_reference_audit(
                initial_audits, "ExternalRef", "[Book.xlsx]Data");
        check(external != nullptr,
            "definedName audit should include external workbook references");
        if (external != nullptr) {
            check(external->external_workbook_qualifier &&
                    !external->sheet_range_qualifier &&
                    !external->matched_current_workbook_sheet,
                "definedName audit should classify external references without local matching");
        }

        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* three_d =
            find_defined_name_formula_reference_audit(initial_audits, "ThreeDRef", "Data:Formula");
        check(three_d != nullptr,
            "definedName audit should include 3D sheet-range references");
        if (three_d != nullptr) {
            check(!three_d->external_workbook_qualifier &&
                    three_d->sheet_range_qualifier &&
                    !three_d->matched_current_workbook_sheet,
                "definedName audit should classify 3D ranges without local matching");
        }
    }

    editor.rename_sheet("Data", "RenamedData");
    const std::size_t pending_change_count_before_audit = editor.pending_change_count();
    const bool has_pending_changes_before_audit = editor.has_pending_changes();
    const std::vector<std::string> pending_replacement_names_before_audit =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> pending_materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t pending_summary_count_before_audit =
        editor.pending_worksheet_edits().size();
    const std::optional<std::string> last_edit_error_before_audit =
        editor.last_edit_error();

    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit> renamed_audits =
        editor.defined_name_formula_reference_audits();
    check(editor.pending_change_count() == pending_change_count_before_audit,
        "renamed definedName audit should not increment public edit count");
    check(editor.has_pending_changes() == has_pending_changes_before_audit,
        "renamed definedName audit should not change pending-change state");
    check(editor.pending_replacement_worksheet_names() == pending_replacement_names_before_audit,
        "renamed definedName audit should not create replacement diagnostics");
    check(editor.pending_materialized_worksheet_names() == pending_materialized_names_before_audit,
        "renamed definedName audit should not create materialized diagnostics");
    check(editor.pending_worksheet_edits().size() == pending_summary_count_before_audit,
        "renamed definedName audit should not create pending edit summaries");
    check(editor.last_edit_error() == last_edit_error_before_audit,
        "renamed definedName audit should not update last_edit_error");
    check(renamed_audits.size() == 4,
        "rename should not drop definedName formula reference audit entries");
    {
        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* report =
            find_defined_name_formula_reference_audit(renamed_audits, "ReportRange", "Data");
        check(report != nullptr,
            "renamed definedName audit should still expose source-name formula text");
        if (report != nullptr) {
            check(report->matched_current_workbook_sheet &&
                    report->matched_source_sheet_name == "Data" &&
                    report->matched_planned_sheet_name == "RenamedData",
                "renamed definedName audit should map source name to planned sheet name");
            check(report->references_renamed_source_name &&
                    !report->references_planned_sheet_name,
                "renamed definedName audit should flag source-name references as stale");
        }
    }

    check_public_inspection_preserves_last_edit_error(editor, std::nullopt);

    editor.save_as(output);
    const std::string workbook_xml =
        fastxlsx::test::read_zip_entries(output).at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="RenamedData")",
        "rename should still update the workbook catalog");
    check_contains(workbook_xml, "Data!$A$1:$B$2",
        "definedName audit should not rewrite definedName formula text during save");
    check_not_contains(workbook_xml, "RenamedData!$A$1:$B$2",
        "definedName audit should not silently repair workbook definedNames");
}

void test_rename_sheet_can_rewrite_defined_names_opt_in()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-defined-name-rewrite-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-defined-name-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames;
    seed_invalid_rename_last_edit_error(
        editor, "Data", "definedName formula rewrite success");
    editor.rename_sheet("Data", "Renamed & Data", options);
    check(!editor.last_edit_error().has_value(),
        "successful definedName formula rewrite should clear last_edit_error");
    editor.save_as(output);

    const std::string workbook_xml =
        fastxlsx::test::read_zip_entries(output).at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "opt-in definedName rewrite should still update the workbook catalog");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'Renamed &amp; Data'!$A$1:$B$2</definedName>)",
        "opt-in definedName rewrite should update direct workbook definedName formulas");
    check_contains(workbook_xml,
        R"(<definedName name="ScopedRange" localSheetId="2">'Other Sheet'!$A$1</definedName>)",
        "opt-in definedName rewrite should preserve unrelated scoped definedNames");
    check_contains(workbook_xml,
        R"(<definedName name="ExternalRef">[Book.xlsx]Data!A1</definedName>)",
        "opt-in definedName rewrite should preserve external workbook references");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeDRef">Data:Formula!A1</definedName>)",
        "opt-in definedName rewrite should preserve 3D sheet-range references");
}

void test_rename_sheet_defined_name_policy_preserves_materialized_formula_cells()
{
    const std::filesystem::path source = write_formula_reference_source(
        "fastxlsx-workbook-editor-defined-name-policy-formula-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-defined-name-policy-formula-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value().find("Data!A1") != std::string::npos,
        "source formula sheet should materialize the Data reference before rename");
    check(!formula_sheet.has_pending_changes(),
        "read-only materialization should start clean");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames;
    editor.rename_sheet("Data", "RenamedData", options);

    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == before.text_value(),
        "definedName-only policy should not rewrite materialized worksheet formulas");
    check(!formula_sheet.has_pending_changes(),
        "definedName-only policy should keep clean materialized formulas clean");

    editor.save_as(output);
    const std::string formula_sheet_xml =
        fastxlsx::test::read_zip_entries(output).at("xl/worksheets/sheet4.xml");
    check_contains(formula_sheet_xml, "<f>Data!A1+",
        "definedName-only policy should preserve source-name worksheet formula text");
    check_not_contains(formula_sheet_xml, "'RenamedData'!A1",
        "definedName-only policy should not silently rewrite worksheet formula cells");
}

void test_rename_sheet_can_rewrite_materialized_formula_cells_opt_in()
{
    const std::filesystem::path source = write_formula_reference_source(
        "fastxlsx-workbook-editor-materialized-formula-rewrite-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-formula-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula,
        "formula rewrite fixture should materialize a formula cell");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    seed_invalid_rename_last_edit_error(
        editor, "Data", "materialized formula rewrite success");
    editor.rename_sheet("Data", "RenamedData", options);
    check(!editor.last_edit_error().has_value(),
        "successful materialized formula rewrite should clear last_edit_error");

    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    const std::string expected_formula =
        R"('RenamedData'!A1+'Other Sheet'!A1+'O''Brien'!A1+[Book.xlsx]Data!A1+Data:Formula!A1+"Data!Z9")";
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == expected_formula,
        "materialized formula rewrite should update only direct local sheet qualifiers");
    check(formula_sheet.has_pending_changes(),
        "materialized formula rewrite should mark the WorksheetEditor session dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        editor.formula_reference_audits();
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* renamed =
        find_formula_reference_audit(audits, "RenamedData");
    check(renamed != nullptr,
        "formula audit should expose the rewritten planned-name reference");
    if (renamed != nullptr) {
        check(renamed->references_planned_sheet_name &&
                !renamed->references_renamed_source_name,
            "rewritten materialized formula should no longer be reported as stale");
    }
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
        find_formula_reference_audit(audits, "[Book.xlsx]Data");
    check(external != nullptr && external->external_workbook_qualifier,
        "materialized formula rewrite should preserve external workbook references");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
        find_formula_reference_audit(audits, "Data:Formula");
    check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
        "materialized formula rewrite should preserve 3D sheet-range references");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "materialized formula rewrite should still update the workbook catalog");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet4.xml");
    check_contains(formula_sheet_xml, "'RenamedData'!A1+'Other Sheet'!A1",
        "dirty projection should persist the rewritten materialized formula");
    check_contains(formula_sheet_xml, "[Book.xlsx]Data!A1",
        "dirty projection should preserve external workbook references");
    check_contains(formula_sheet_xml, "Data:Formula!A1",
        "dirty projection should preserve 3D sheet-range references");
    check_contains(formula_sheet_xml, R"("Data!Z9")",
        "dirty projection should preserve string literals containing sheet-like text");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == expected_formula,
        "reopened output should expose the rewritten materialized formula text");
}

void test_rename_sheet_combined_policy_rewrites_defined_names_and_materialized_formulas()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-combined-formula-rewrite-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-combined-formula-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value() == "Data!A1",
        "combined rewrite fixture should materialize the source formula before rename");
    check(!formula_sheet.has_pending_changes(),
        "combined rewrite fixture should start with a clean materialized formula session");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    seed_invalid_rename_last_edit_error(
        editor, "Data", "combined formula rewrite success");
    editor.rename_sheet("Data", "RenamedData", options);
    check(!editor.last_edit_error().has_value(),
        "successful combined formula rewrite should clear last_edit_error");

    const std::string expected_formula = "'RenamedData'!A1";
    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == expected_formula,
        "combined rewrite should update the already-materialized worksheet formula");
    check(formula_sheet.has_pending_changes(),
        "combined rewrite should dirty the changed materialized worksheet session");
    check(editor.pending_change_count() == 1,
        "combined rewrite should still count the public rename once before save_as");
    const std::vector<std::string> dirty_names =
        editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Formula",
        "combined rewrite should expose only the rewritten materialized formula sheet");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> formula_audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(formula_audits, "RenamedData") == 1,
        "combined rewrite formula audit should expose the planned-name formula reference");
    check(count_formula_reference_audits(formula_audits, "Data") == 0,
        "combined rewrite formula audit should not leave a stale local Data reference");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="RenamedData")",
        "combined rewrite should persist the renamed workbook catalog entry");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'RenamedData'!$A$1:$B$2</definedName>)",
        "combined rewrite should persist the rewritten workbook definedName");
    check_contains(workbook_xml,
        R"(<definedName name="ExternalRef">[Book.xlsx]Data!A1</definedName>)",
        "combined rewrite should persist external workbook definedName references unchanged");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeDRef">Data:Formula!A1</definedName>)",
        "combined rewrite should persist 3D definedName references unchanged");
    check_contains(output_entries.at("xl/worksheets/sheet3.xml"), expected_formula,
        "combined rewrite should persist the rewritten materialized worksheet formula");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == expected_formula,
        "reopened combined rewrite output should expose the rewritten formula");

    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>
        reopened_defined_name_audits = reopened.defined_name_formula_reference_audits();
    const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* report_range =
        find_defined_name_formula_reference_audit(
            reopened_defined_name_audits, "ReportRange", "RenamedData");
    check(report_range != nullptr,
        "reopened combined output should expose the rewritten direct definedName reference");
    if (report_range != nullptr) {
        check(report_range->formula_text == "'RenamedData'!$A$1:$B$2" &&
                report_range->references_planned_sheet_name &&
                !report_range->references_renamed_source_name,
            "reopened combined definedName audit should report the planned sheet name");
    }
    check(find_defined_name_formula_reference_audit(
              reopened_defined_name_audits, "ReportRange", "Data") == nullptr,
        "reopened combined output should not leave the direct definedName on the old name");
    const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* external =
        find_defined_name_formula_reference_audit(
            reopened_defined_name_audits, "ExternalRef", "[Book.xlsx]Data");
    check(external != nullptr && external->external_workbook_qualifier,
        "reopened combined output should preserve external workbook definedName references");
    const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* sheet_range =
        find_defined_name_formula_reference_audit(
            reopened_defined_name_audits, "ThreeDRef", "Data:Formula");
    check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
        "reopened combined output should preserve 3D definedName references");
}

void test_rename_sheet_formula_policy_rewrites_case_varied_local_refs()
{
    const std::filesystem::path source = write_case_varied_formula_reference_source(
        "fastxlsx-workbook-editor-case-varied-formula-rewrite-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-case-varied-formula-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value()
                == R"(data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1")",
        "case-varied formula fixture should expose mixed-case source references");
    check(!formula_sheet.has_pending_changes(),
        "case-varied formula fixture should start with a clean materialized formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> initial_formula_audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(initial_formula_audits, "data") == 1 &&
            count_formula_reference_audits(initial_formula_audits, "DATA") == 1,
        "case-varied formula audit should preserve source qualifier spelling before rewrite");
    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>
        initial_defined_name_audits = editor.defined_name_formula_reference_audits();
    check(find_defined_name_formula_reference_audit(
              initial_defined_name_audits, "ReportRange", "data") != nullptr,
        "case-varied definedName audit should expose lowercase source references");
    check(find_defined_name_formula_reference_audit(
              initial_defined_name_audits, "ReportRange", "DATA") != nullptr,
        "case-varied definedName audit should expose uppercase source references");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    seed_invalid_rename_last_edit_error(
        editor, "Data", "case-varied formula rewrite success");
    editor.rename_sheet("Data", "Renamed & Data", options);
    check(!editor.last_edit_error().has_value(),
        "successful case-varied formula rewrite should clear last_edit_error");

    const std::string expected_formula =
        R"('Renamed & Data'!A1+'Renamed & Data'!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1")";
    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == expected_formula,
        "opt-in formula policy should rewrite case-varied local worksheet formulas only");
    check(formula_sheet.has_pending_changes(),
        "case-varied formula rewrite should dirty the materialized Formula sheet");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> rewritten_formula_audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(rewritten_formula_audits, "Renamed & Data") == 2,
        "rewritten formula audit should expose both local refs under the planned sheet name");
    check(count_formula_reference_audits(rewritten_formula_audits, "data") == 0 &&
            count_formula_reference_audits(rewritten_formula_audits, "DATA") == 0,
        "rewritten formula audit should not keep stale case-varied local source refs");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
        find_formula_reference_audit(rewritten_formula_audits, "[Book.xlsx]data");
    check(external != nullptr && external->external_workbook_qualifier,
        "case-varied formula rewrite should preserve external workbook references");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
        find_formula_reference_audit(rewritten_formula_audits, "data:Formula");
    check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
        "case-varied formula rewrite should preserve 3D sheet-range references");

    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>
        rewritten_defined_name_audits = editor.defined_name_formula_reference_audits();
    check(find_defined_name_formula_reference_audit(
              rewritten_defined_name_audits, "ReportRange", "Renamed & Data") != nullptr,
        "case-varied definedName rewrite should expose planned-name local references");
    check(find_defined_name_formula_reference_audit(
              rewritten_defined_name_audits, "ReportRange", "data") == nullptr,
        "case-varied definedName rewrite should remove lowercase local source references");
    check(find_defined_name_formula_reference_audit(
              rewritten_defined_name_audits, "ReportRange", "DATA") == nullptr,
        "case-varied definedName rewrite should remove uppercase local source references");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "case-varied formula rewrite should persist XML-escaped renamed sheet catalog");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'Renamed &amp; Data'!$A$1+'Renamed &amp; Data'!$B$2</definedName>)",
        "case-varied formula rewrite should persist XML-escaped rewritten definedNames");
    check_contains(workbook_xml,
        R"(<definedName name="ExternalRef">[Book.xlsx]data!A1</definedName>)",
        "case-varied formula rewrite should preserve external workbook definedNames");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeDRef">data:Formula!A1</definedName>)",
        "case-varied formula rewrite should preserve 3D definedNames");

    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet3.xml");
    check_contains(formula_sheet_xml,
        R"(<f>'Renamed &amp; Data'!A1+'Renamed &amp; Data'!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1"</f>)",
        "case-varied formula rewrite should persist XML-escaped materialized formula text");
    check_not_contains(formula_sheet_xml, "<f>data!A1+DATA!B1",
        "case-varied formula rewrite should not leave stale local worksheet formula refs");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == expected_formula,
        "reopened case-varied output should expose the rewritten materialized formula");
}

void test_rename_sheet_default_preserves_case_varied_formula_refs()
{
    const std::filesystem::path source = write_case_varied_formula_reference_source(
        "fastxlsx-workbook-editor-case-varied-formula-default-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-case-varied-formula-default-output.xlsx");
    const std::string original_formula =
        R"(data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1")";

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value() == original_formula,
        "case-varied default formula fixture should expose mixed-case source references");

    editor.rename_sheet("Data", "Renamed & Data");

    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == original_formula,
        "default rename_sheet should preserve materialized formula text");
    check(!formula_sheet.has_pending_changes(),
        "default rename_sheet should not dirty materialized formula sessions");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> formula_audits =
        editor.formula_reference_audits();
    const auto check_stale_formula_ref = [&] (std::string_view spelling) {
        const fastxlsx::WorkbookEditorFormulaReferenceAudit* audit =
            find_formula_reference_audit(formula_audits, spelling);
        check(audit != nullptr,
            std::string("default rename should audit stale formula ref ") +
                std::string(spelling));
        if (audit != nullptr) {
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Renamed & Data",
                std::string("default rename formula audit should map ") +
                    std::string(spelling) + " to the renamed catalog entry");
            check(audit->references_renamed_source_name &&
                    !audit->references_planned_sheet_name,
                std::string("default rename formula audit should flag ") +
                    std::string(spelling) + " as stale source-name text");
        }
    };
    check_stale_formula_ref("data");
    check_stale_formula_ref("DATA");
    check(count_formula_reference_audits(formula_audits, "Renamed & Data") == 0,
        "default rename should not report rewritten planned-name formula refs");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
        find_formula_reference_audit(formula_audits, "[Book.xlsx]data");
    check(external != nullptr && external->external_workbook_qualifier,
        "default rename should preserve external workbook formula audit classification");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* sheet_range =
        find_formula_reference_audit(formula_audits, "data:Formula");
    check(sheet_range != nullptr && sheet_range->sheet_range_qualifier,
        "default rename should preserve 3D formula audit classification");

    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>
        defined_name_audits = editor.defined_name_formula_reference_audits();
    const auto check_stale_defined_name_ref = [&] (std::string_view spelling) {
        const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* audit =
            find_defined_name_formula_reference_audit(
                defined_name_audits, "ReportRange", spelling);
        check(audit != nullptr,
            std::string("default rename should audit stale definedName ref ") +
                std::string(spelling));
        if (audit != nullptr) {
            check(audit->matched_current_workbook_sheet &&
                    audit->matched_source_sheet_name == "Data" &&
                    audit->matched_planned_sheet_name == "Renamed & Data",
                std::string("default rename definedName audit should map ") +
                    std::string(spelling) + " to the renamed catalog entry");
            check(audit->references_renamed_source_name &&
                    !audit->references_planned_sheet_name,
                std::string("default rename definedName audit should flag ") +
                    std::string(spelling) + " as stale source-name text");
        }
    };
    check_stale_defined_name_ref("data");
    check_stale_defined_name_ref("DATA");
    check(find_defined_name_formula_reference_audit(
              defined_name_audits, "ReportRange", "Renamed & Data") == nullptr,
        "default rename should not rewrite direct definedName formula refs");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "default rename should persist XML-escaped renamed sheet catalog");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">data!$A$1+DATA!$B$2</definedName>)",
        "default rename should preserve mixed-case direct definedName text");
    check_contains(workbook_xml,
        R"(<definedName name="ExternalRef">[Book.xlsx]data!A1</definedName>)",
        "default rename should preserve external workbook definedNames");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeDRef">data:Formula!A1</definedName>)",
        "default rename should preserve 3D definedNames");
    check_not_contains(workbook_xml,
        R"(<definedName name="ReportRange">'Renamed &amp; Data'!$A$1)",
        "default rename should not persist rewritten definedName formula text");

    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet3.xml");
    check_contains(formula_sheet_xml,
        R"(<f>data!A1+DATA!B1+[Book.xlsx]data!C1+data:Formula!D1+"data!E1"</f>)",
        "default rename should preserve original materialized formula XML");
    check_not_contains(formula_sheet_xml, "Renamed &amp; Data'!A1",
        "default rename should not persist rewritten worksheet formula refs");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == original_formula,
        "reopened default rename output should expose original formula text");
}

void test_rename_sheet_chain_formula_policy_rewrites_source_aliases()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-chain-formula-rewrite-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-chain-formula-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value() == "Data!A1",
        "chain formula rewrite fixture should start with the source sheet name");
    check(!formula_sheet.has_pending_changes(),
        "chain formula rewrite fixture should start with a clean materialized formula");

    editor.rename_sheet("Data", "TemporaryData");
    check(formula_sheet.get_cell("A1").text_value() == "Data!A1",
        "audit-only first rename should preserve source-name formula text");
    check(!formula_sheet.has_pending_changes(),
        "audit-only first rename should not dirty materialized formula sessions");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    seed_invalid_rename_last_edit_error(
        editor, "TemporaryData", "chain formula rewrite success");
    editor.rename_sheet("TemporaryData", "FinalData", options);
    check(!editor.last_edit_error().has_value(),
        "successful chain formula rewrite should clear last_edit_error");

    const std::string expected_formula = "'FinalData'!A1";
    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == fastxlsx::CellValueKind::Formula &&
            after.text_value() == expected_formula,
        "chain formula rewrite should update source-name formulas through the planned alias");
    check(formula_sheet.has_pending_changes(),
        "chain formula rewrite should dirty the materialized formula sheet");
    check(editor.pending_change_count() == 2,
        "chain formula rewrite should count the two successful public renames");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> planned_formula_audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(planned_formula_audits, "FinalData") == 1,
        "chain formula audit should expose the rewritten materialized formula under the final planned name");
    check(count_formula_reference_audits(planned_formula_audits, "Data") == 0,
        "chain formula audit should not keep stale source-name materialized formula references after rewrite");
    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>
        planned_defined_name_audits = editor.defined_name_formula_reference_audits();
    const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit* planned_report_range =
        find_defined_name_formula_reference_audit(
            planned_defined_name_audits, "ReportRange", "FinalData");
    check(planned_report_range != nullptr,
        "chain definedName audit should inspect the queued planned workbook XML after opt-in rewrite");
    if (planned_report_range != nullptr) {
        check(planned_report_range->matched_current_workbook_sheet &&
                planned_report_range->matched_source_sheet_name == "Data" &&
                planned_report_range->matched_planned_sheet_name == "FinalData",
            "chain definedName audit should map final planned references back to the source sheet");
        check(!planned_report_range->references_renamed_source_name &&
                planned_report_range->references_planned_sheet_name,
            "chain definedName audit should not flag rewritten planned references as stale");
    }
    check(find_defined_name_formula_reference_audit(
              planned_defined_name_audits, "ReportRange", "Data") == nullptr,
        "chain definedName audit should not keep source-name direct references after planned rewrite");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="FinalData")",
        "chain formula rewrite should persist the final sheet catalog name");
    check_not_contains(workbook_xml, "TemporaryData",
        "chain formula rewrite output should not leak the transient planned name");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'FinalData'!$A$1:$B$2</definedName>)",
        "chain formula rewrite should update source-name direct definedName references");
    check_not_contains(workbook_xml,
        R"(<definedName name="ReportRange">Data!$A$1:$B$2</definedName>)",
        "chain formula rewrite should not leave the direct definedName on the source name");
    check_contains(workbook_xml,
        R"(<definedName name="ExternalRef">[Book.xlsx]Data!A1</definedName>)",
        "chain formula rewrite should still preserve external workbook definedNames");
    check_contains(workbook_xml,
        R"(<definedName name="ThreeDRef">Data:Formula!A1</definedName>)",
        "chain formula rewrite should still preserve 3D definedName references");
    check_contains(output_entries.at("xl/worksheets/sheet3.xml"), expected_formula,
        "chain formula rewrite should persist the rewritten materialized formula");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == expected_formula,
        "reopened chain formula rewrite output should expose the final sheet formula");
}

void test_rename_sheet_rewrites_multiple_materialized_formula_sheets_opt_in()
{
    const std::filesystem::path source = write_multi_formula_reference_source(
        "fastxlsx-workbook-editor-multi-materialized-formula-rewrite-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-multi-materialized-formula-rewrite-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_one = editor.worksheet("Formula One");
    fastxlsx::WorksheetEditor formula_two = editor.worksheet("Formula Two");
    fastxlsx::WorksheetEditor plain = editor.worksheet("Plain");
    check(!formula_one.has_pending_changes() && !formula_two.has_pending_changes()
            && !plain.has_pending_changes(),
        "multi-formula rewrite fixture should materialize all sessions cleanly");
    check(editor.formula_reference_audits().size() == 5,
        "multi-formula rewrite fixture should expose all pre-rename formula references");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    seed_invalid_rename_last_edit_error(
        editor, "Data", "multi-session formula rewrite success");
    editor.rename_sheet("Data", "RenamedData", options);
    check(!editor.last_edit_error().has_value(),
        "successful multi-session formula rewrite should clear last_edit_error");

    const std::string expected_one = "'RenamedData'!A1+'RenamedData'!A1";
    const std::string expected_two =
        "'RenamedData'!A1+'RenamedData'!A1+[Book.xlsx]Data!A1";
    check(formula_one.get_cell("A1").text_value() == expected_one,
        "opt-in rename should rewrite repeated direct references in first materialized sheet");
    check(formula_two.get_cell("A1").text_value() == expected_two,
        "opt-in rename should rewrite quoted and unquoted references in second materialized sheet");
    check(formula_one.has_pending_changes() && formula_two.has_pending_changes(),
        "formula rewrite should dirty every changed materialized formula session");
    check(!plain.has_pending_changes(),
        "formula rewrite should not dirty unrelated materialized sessions");

    const std::vector<std::string> dirty_names =
        editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 2 && dirty_names[0] == "Formula One"
            && dirty_names[1] == "Formula Two",
        "formula rewrite should expose only changed materialized sessions as dirty");
    check(editor.pending_materialized_cell_count()
            == formula_one.cell_count() + formula_two.cell_count(),
        "formula rewrite dirty cell count should aggregate only changed formula sessions");
    check(editor.pending_change_count() == 1,
        "formula rewrite should count the public rename once before save_as flush");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(audits, "RenamedData") == 4,
        "formula audit should expose all rewritten local references under the planned name");
    check(count_formula_reference_audits(audits, "Data") == 0,
        "formula audit should not report stale local source-name references after rewrite");
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* external =
        find_formula_reference_audit(audits, "[Book.xlsx]Data");
    check(external != nullptr && external->external_workbook_qualifier,
        "formula rewrite should preserve external workbook references in multi-session rewrite");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "multi-session formula rewrite should still update workbook catalog");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), expected_one,
        "first materialized formula sheet should persist rewritten formula text");
    check_contains(output_entries.at("xl/worksheets/sheet3.xml"), expected_two,
        "second materialized formula sheet should persist rewritten formula text");
    check_contains(output_entries.at("xl/worksheets/sheet4.xml"), "no formula",
        "unrelated materialized plain sheet should remain preserved");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.worksheet("Formula One").get_cell("A1").text_value() == expected_one,
        "reopened output should expose first rewritten materialized formula");
    check(reopened.worksheet("Formula Two").get_cell("A1").text_value() == expected_two,
        "reopened output should expose second rewritten materialized formula");
}

void test_rename_sheet_formula_rewrite_failed_save_as_preserves_state()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-formula-rewrite-failed-save-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-formula-rewrite-failed-save-missing-parent")
        / "output.xlsx";
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-formula-rewrite-failed-save-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    check(formula_sheet.get_cell("A1").text_value() == "Data!A1",
        "formula rewrite failed-save fixture should start with the source formula");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    editor.rename_sheet("Data", "RenamedData", options);

    const std::string expected_formula = "'RenamedData'!A1";
    check(!editor.last_edit_error().has_value(),
        "successful formula rewrite before failed save_as should leave last_edit_error empty");
    check(formula_sheet.has_pending_changes() &&
            formula_sheet.get_cell("A1").text_value() == expected_formula,
        "successful formula rewrite before failed save_as should dirty the materialized formula");
    check(editor.pending_change_count() == 1,
        "formula rewrite failed-save fixture should queue one public rename before save");

    const WorkbookEditorPublicSaveStateSnapshot save_state_before_save =
        workbook_editor_public_save_state_snapshot(editor);
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();
    const std::vector<std::string> dirty_names_before_save =
        editor.pending_materialized_worksheet_names();
    const std::size_t dirty_cell_count_before_save =
        editor.pending_materialized_cell_count();
    const std::size_t dirty_memory_before_save =
        editor.estimated_pending_materialized_memory_usage();

    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "formula rewrite save_as to a missing parent should fail without committing output");
    check(!std::filesystem::exists(missing_parent_output),
        "formula rewrite failed save_as should not create the missing-parent output");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_save, "failed formula rewrite save_as");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_save),
        "failed formula rewrite save_as should preserve the planned catalog");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_save),
        "failed formula rewrite save_as should preserve public edit summaries");
    check(editor.pending_materialized_worksheet_names() == dirty_names_before_save,
        "failed formula rewrite save_as should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count_before_save,
        "failed formula rewrite save_as should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage()
            == dirty_memory_before_save,
        "failed formula rewrite save_as should preserve dirty materialized memory");
    check(formula_sheet.has_pending_changes() &&
            formula_sheet.get_cell("A1").text_value() == expected_formula,
        "failed formula rewrite save_as should preserve the rewritten formula session");
    check(!editor.last_edit_error().has_value(),
        "failed formula rewrite save_as should not create last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="RenamedData")",
        "safe retry after formula rewrite failed save should persist the catalog rename");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'RenamedData'!$A$1:$B$2</definedName>)",
        "safe retry after formula rewrite failed save should persist rewritten definedNames");
    check_contains(output_entries.at("xl/worksheets/sheet3.xml"), expected_formula,
        "safe retry after formula rewrite failed save should persist rewritten worksheet formulas");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::CellValue reopened_formula =
        reopened.worksheet("Formula").get_cell("A1");
    check(reopened_formula.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_formula.text_value() == expected_formula,
        "reopened retry output should expose the rewritten formula after failed save_as");
}

void test_rename_sheet_formula_rewrite_dirty_session_accepts_later_mutations()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-formula-rewrite-later-mutation-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-formula-rewrite-later-mutation-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    check(formula_sheet.get_cell("A1").text_value() == "Data!A1",
        "formula rewrite later-mutation fixture should start with the source formula");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    editor.rename_sheet("Data", "RenamedData", options);

    const std::string expected_formula = "'RenamedData'!A1";
    check(formula_sheet.has_pending_changes() &&
            formula_sheet.get_cell("A1").text_value() == expected_formula,
        "formula rewrite should dirty the materialized session before later mutations");
    const std::size_t rewritten_cell_count = formula_sheet.cell_count();
    const std::size_t rewritten_memory = formula_sheet.estimated_memory_usage();

    check(threw_fastxlsx_error([&] {
        formula_sheet.set_cell("a1",
            fastxlsx::CellValue::text("rejected-after-formula-rewrite"));
    }), "invalid mutation after formula rewrite should throw");
    check(editor.last_edit_error().has_value(),
        "invalid mutation after formula rewrite should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(),
            "WorksheetEditor cell reference is invalid",
            "invalid mutation after formula rewrite should expose the cell-reference diagnostic");
    }
    check(formula_sheet.get_cell("A1").text_value() == expected_formula,
        "invalid mutation after formula rewrite should preserve rewritten formula text");
    check(formula_sheet.cell_count() == rewritten_cell_count,
        "invalid mutation after formula rewrite should preserve sparse cell count");
    check(formula_sheet.estimated_memory_usage() == rewritten_memory,
        "invalid mutation after formula rewrite should preserve sparse memory estimate");
    check(editor.pending_change_count() == 1,
        "invalid mutation after formula rewrite should preserve the queued public rename");

    formula_sheet.set_cell(2, 1,
        fastxlsx::CellValue::text("post-rewrite materialized edit"));
    check(!editor.last_edit_error().has_value(),
        "valid mutation after formula rewrite should clear the failed mutation diagnostic");
    check(formula_sheet.get_cell("A1").text_value() == expected_formula,
        "valid mutation after formula rewrite should keep the rewritten formula");
    check(formula_sheet.get_cell("A2").text_value() == "post-rewrite materialized edit",
        "valid mutation after formula rewrite should be readable from the same session");
    check(formula_sheet.cell_count() == rewritten_cell_count + 1,
        "valid mutation after formula rewrite should add one materialized cell");
    check(editor.pending_materialized_cell_count() == formula_sheet.cell_count(),
        "valid mutation after formula rewrite should update aggregate dirty cell count");
    const std::vector<std::string> dirty_names =
        editor.pending_materialized_worksheet_names();
    check(dirty_names.size() == 1 && dirty_names[0] == "Formula",
        "valid mutation after formula rewrite should keep only Formula dirty");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        editor.formula_reference_audits();
    check(count_formula_reference_audits(audits, "RenamedData") == 1,
        "later materialized mutation should preserve the rewritten formula audit");
    check(count_formula_reference_audits(audits, "Data") == 0,
        "later materialized mutation should not restore stale source-name formula refs");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet3.xml");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "later mutation after formula rewrite should persist the renamed catalog");
    check_contains(output_entries.at("xl/workbook.xml"),
        R"(<definedName name="ReportRange">'RenamedData'!$A$1:$B$2</definedName>)",
        "later mutation after formula rewrite should persist rewritten definedNames");
    check_contains(formula_sheet_xml, expected_formula,
        "later mutation after formula rewrite should persist rewritten formula text");
    check_contains(formula_sheet_xml, "post-rewrite materialized edit",
        "later mutation after formula rewrite should persist later materialized edits");
    check_not_contains(formula_sheet_xml, "rejected-after-formula-rewrite",
        "later mutation after formula rewrite should not leak rejected payloads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_formula = reopened.worksheet("Formula");
    check(reopened_formula.get_cell("A1").text_value() == expected_formula,
        "reopened later-mutation output should expose the rewritten formula");
    check(reopened_formula.get_cell("A2").text_value()
            == "post-rewrite materialized edit",
        "reopened later-mutation output should expose the later edit");
}

void test_rename_sheet_formula_rewrite_blocks_same_sheet_replacement()
{
    const std::filesystem::path source = write_defined_name_reference_source(
        "fastxlsx-workbook-editor-formula-rewrite-same-sheet-replacement-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-formula-rewrite-same-sheet-replacement-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor formula_sheet = editor.worksheet("Formula");
    check(formula_sheet.get_cell("A1").text_value() == "Data!A1",
        "formula rewrite replacement fixture should start with the source formula");

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    editor.rename_sheet("Data", "RenamedData", options);

    const std::string expected_formula = "'RenamedData'!A1";
    check(formula_sheet.has_pending_changes() &&
            formula_sheet.get_cell("A1").text_value() == expected_formula,
        "formula rewrite should dirty Formula before same-sheet replacement preflight");
    const std::size_t rewritten_cell_count = formula_sheet.cell_count();
    const std::size_t rewritten_memory = formula_sheet.estimated_memory_usage();
    check(editor.pending_change_count() == 1,
        "formula rewrite replacement fixture should queue the public rename first");

    bool replacement_failed = false;
    try {
        editor.replace_sheet_data("Formula",
            {{fastxlsx::CellValue::text("blocked-formula-replacement")}});
    } catch (const fastxlsx::FastXlsxError& error) {
        replacement_failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "same-sheet replacement after formula rewrite should populate last_edit_error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "same-sheet replacement diagnostic should match the thrown error");
            check_contains(*last_error,
                "cannot replace sheet data after materializing planned worksheet session",
                "same-sheet replacement should report the materialized-session guard");
        }
    }
    check(replacement_failed,
        "same-sheet replacement after formula rewrite should be rejected");
    const std::optional<std::string> replacement_error = editor.last_edit_error();
    check_public_inspection_preserves_last_edit_error(editor, replacement_error);
    check(formula_sheet.has_pending_changes() &&
            formula_sheet.get_cell("A1").text_value() == expected_formula,
        "rejected same-sheet replacement should preserve the rewritten formula");
    check(formula_sheet.cell_count() == rewritten_cell_count,
        "rejected same-sheet replacement should preserve sparse formula cell count");
    check(formula_sheet.estimated_memory_usage() == rewritten_memory,
        "rejected same-sheet replacement should preserve sparse formula memory");
    check(!editor.has_pending_replacement("Formula"),
        "rejected same-sheet replacement should not queue a Formula replacement");
    check(editor.pending_replacement_cell_count() == 0,
        "rejected same-sheet replacement should not add replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rejected same-sheet replacement should not add replacement sheet names");
    check(editor.pending_change_count() == 1,
        "rejected same-sheet replacement should preserve the queued public rename only");

    editor.replace_sheet_data("Other Sheet",
        {{fastxlsx::CellValue::text("allowed-other-sheet-after-formula-rewrite")}});
    check(!editor.last_edit_error().has_value(),
        "cross-sheet replacement after formula rewrite should clear last_edit_error");
    check(editor.has_pending_replacement("Other Sheet"),
        "cross-sheet replacement after formula rewrite should queue replacement state");
    check(editor.pending_replacement_cell_count() == 1,
        "cross-sheet replacement after formula rewrite should expose one replacement cell");
    check(editor.pending_change_count() == 2,
        "cross-sheet replacement should add to the queued formula rewrite rename");
    check(formula_sheet.get_cell("A1").text_value() == expected_formula,
        "cross-sheet replacement should not disturb the rewritten Formula session");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet3.xml");
    const std::string other_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(workbook_xml, R"(name="RenamedData")",
        "same-sheet replacement boundary should persist the renamed catalog");
    check_contains(workbook_xml,
        R"(<definedName name="ReportRange">'RenamedData'!$A$1:$B$2</definedName>)",
        "same-sheet replacement boundary should persist rewritten definedNames");
    check_contains(formula_sheet_xml, expected_formula,
        "same-sheet replacement boundary should persist rewritten formula text");
    check_not_contains(formula_sheet_xml, "blocked-formula-replacement",
        "rejected same-sheet replacement should not leak into Formula output");
    check_contains(other_sheet_xml, "allowed-other-sheet-after-formula-rewrite",
        "cross-sheet replacement should save beside the rewritten Formula session");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.worksheet("Formula").get_cell("A1").text_value() == expected_formula,
        "reopened same-sheet replacement boundary output should expose rewritten formula");
    check(reopened.worksheet("Other Sheet").get_cell("A1").text_value()
            == "allowed-other-sheet-after-formula-rewrite",
        "reopened same-sheet replacement boundary output should expose cross-sheet replacement");
}

void test_rename_sheet_materialized_formula_rewrite_guard_failure_preserves_state()
{
    const std::filesystem::path source = write_formula_reference_source(
        "fastxlsx-workbook-editor-materialized-formula-rewrite-guard-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-materialized-formula-rewrite-guard-output.xlsx");
    const std::filesystem::path recovered_output = artifact(
        "fastxlsx-workbook-editor-materialized-formula-rewrite-guard-recovered-output.xlsx");
    const std::string target_name = "RenamedDataLongerSheetName01";

    fastxlsx::WorkbookEditor sizing_editor = fastxlsx::WorkbookEditor::open(source);
    const fastxlsx::WorksheetEditor sizing_formula_sheet =
        sizing_editor.worksheet("Formula");
    const std::size_t exact_memory_budget =
        sizing_formula_sheet.estimated_memory_usage();

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions worksheet_options;
    worksheet_options.memory_budget_bytes = exact_memory_budget;
    fastxlsx::WorksheetEditor formula_sheet =
        editor.worksheet("Formula", worksheet_options);
    const fastxlsx::CellValue before = formula_sheet.get_cell("A1");
    check(before.kind() == fastxlsx::CellValueKind::Formula &&
            before.text_value().find("Data!A1") != std::string::npos,
        "formula rewrite guard fixture should start with a Data reference");
    check(!formula_sheet.has_pending_changes(),
        "guarded materialized formula sheet should start clean");
    check_workbook_editor_public_no_pending_state(
        editor, "guarded materialized formula rewrite initial state");
    const std::vector<std::string> names_before = editor.worksheet_names();

    fastxlsx::WorkbookEditorRenameOptions options;
    options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;

    bool failed = false;
    std::string failure_message;
    try {
        editor.rename_sheet("Data", std::string(target_name), options);
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        failure_message = error.what();
        check_contains(failure_message, "WorkbookEditor::rename_sheet() failed",
            "materialized formula rewrite guard should report public rename context");
        check_contains(failure_message, "Data",
            "materialized formula rewrite guard diagnostic should include the source sheet");
        check_contains(failure_message, target_name,
            "materialized formula rewrite guard diagnostic should include the target sheet");
        check_contains(failure_message, "CellStore memory_budget_bytes guardrail exceeded",
            "materialized formula rewrite guard should preserve the CellStore root cause");
    }
    check(failed,
        "materialized formula rewrite should preflight CellStore memory guardrails");
    check(editor.last_edit_error().has_value() &&
            *editor.last_edit_error() == failure_message,
        "materialized formula rewrite guard failure should record the public diagnostic");

    const fastxlsx::CellValue after = formula_sheet.get_cell("A1");
    check(after.kind() == before.kind() && after.text_value() == before.text_value(),
        "failed materialized formula rewrite should not mutate the clean session formula");
    check(!formula_sheet.has_pending_changes(),
        "failed materialized formula rewrite should not dirty the materialized session");
    check_workbook_editor_public_no_pending_state(
        editor, "failed materialized formula rewrite");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed materialized formula rewrite should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "failed materialized formula rewrite should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "failed materialized formula rewrite should not expose dirty materialized memory");
    check(editor.worksheet_names() == names_before,
        "failed materialized formula rewrite should preserve the planned catalog");
    check(editor.has_worksheet("Data"),
        "failed materialized formula rewrite should keep the source sheet planned name");
    check(!editor.has_worksheet(target_name),
        "failed materialized formula rewrite should not expose the rejected target name");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "no-op save after materialized formula rewrite guard failure should keep source catalog");
    check_not_contains(output_entries.at("xl/workbook.xml"), target_name,
        "no-op save after materialized formula rewrite guard failure should not leak target name");
    const std::string formula_sheet_xml = output_entries.at("xl/worksheets/sheet4.xml");
    check_contains(formula_sheet_xml, "<f>Data!A1+",
        "no-op save after materialized formula rewrite guard failure should keep source formula");
    check_not_contains(formula_sheet_xml, target_name,
        "no-op save after materialized formula rewrite guard failure should not leak rewritten formula");
    check(editor.last_edit_error().has_value() &&
            *editor.last_edit_error() == failure_message,
        "no-op save after materialized formula rewrite guard failure should preserve the diagnostic");

    editor.rename_sheet("Data", "R", options);
    check(!editor.last_edit_error().has_value(),
        "successful retry after formula rewrite guard failure should clear last_edit_error");
    check(formula_sheet.has_pending_changes(),
        "successful retry after formula rewrite guard failure should dirty the formula session");
    check(editor.has_worksheet("R") && !editor.has_worksheet("Data"),
        "successful retry after formula rewrite guard failure should update the planned catalog");
    const fastxlsx::CellValue recovered_formula = formula_sheet.get_cell("A1");
    check(recovered_formula.kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula.text_value().find("'R'!A1") != std::string::npos,
        "successful retry after formula rewrite guard failure should rewrite the materialized formula");
    check(recovered_formula.text_value().find(target_name) == std::string::npos,
        "successful retry after formula rewrite guard failure should not retain rejected target text");

    editor.save_as(recovered_output);
    const auto recovered_entries = fastxlsx::test::read_zip_entries(recovered_output);
    check_contains(recovered_entries.at("xl/workbook.xml"), R"(name="R")",
        "successful retry output should persist the recovered short rename");
    check_not_contains(recovered_entries.at("xl/workbook.xml"), target_name,
        "successful retry output should not leak the rejected long rename");
    const std::string recovered_formula_sheet_xml =
        recovered_entries.at("xl/worksheets/sheet4.xml");
    check_contains(recovered_formula_sheet_xml, "'R'!A1",
        "successful retry output should persist the recovered rewritten formula");
    check_not_contains(recovered_formula_sheet_xml, target_name,
        "successful retry formula output should not leak the rejected long rename");
}


} // namespace

int main()
{
    try {
        test_formula_reference_audits_report_renamed_source_sheet_risk();
        test_source_formula_reference_audits_report_non_materialized_rename_risk();
        test_source_formula_reference_audits_report_case_varied_default_rename_risk();
        test_source_formula_reference_audits_translate_shared_formula_followers();
        test_defined_name_formula_reference_audits_report_renamed_source_sheet_risk();
        test_rename_sheet_can_rewrite_defined_names_opt_in();
        test_rename_sheet_defined_name_policy_preserves_materialized_formula_cells();
        test_rename_sheet_can_rewrite_materialized_formula_cells_opt_in();
        test_rename_sheet_combined_policy_rewrites_defined_names_and_materialized_formulas();
        test_rename_sheet_formula_policy_rewrites_case_varied_local_refs();
        test_rename_sheet_default_preserves_case_varied_formula_refs();
        test_rename_sheet_chain_formula_policy_rewrites_source_aliases();
        test_rename_sheet_rewrites_multiple_materialized_formula_sheets_opt_in();
        test_rename_sheet_formula_rewrite_failed_save_as_preserves_state();
        test_rename_sheet_formula_rewrite_dirty_session_accepts_later_mutations();
        test_rename_sheet_formula_rewrite_blocks_same_sheet_replacement();
        test_rename_sheet_materialized_formula_rewrite_guard_failure_preserves_state();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor formula rewrite check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor formula rewrite tests passed\n");
    return 0;
}
