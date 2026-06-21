#include <fastxlsx/detail/formula.hpp>
#include <fastxlsx/detail/formula_reference_audit.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
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

void check_equal(std::string_view actual, std::string_view expected, const char* message)
{
    if (actual != expected) {
        std::string detail(message);
        detail += " actual=[";
        detail += actual;
        detail += "] expected=[";
        detail += expected;
        detail += "]";
        throw TestFailure(detail);
    }
}

void check_reference_text(
    std::string_view formula,
    const fastxlsx::detail::FormulaReference& reference,
    std::string_view expected,
    const char* message)
{
    check(reference.offset + reference.length <= formula.size(), message);
    check_equal(formula.substr(reference.offset, reference.length), expected, message);
}

void check_no_sheet_qualifier(
    const fastxlsx::detail::FormulaReference& reference, const char* message)
{
    check(!reference.sheet.present, message);
}

void check_sheet_qualifier_text(
    std::string_view formula,
    const fastxlsx::detail::FormulaReference& reference,
    std::string_view expected_qualifier,
    std::string_view expected_name,
    bool quoted,
    const char* message)
{
    check(reference.sheet.present, message);
    check(reference.sheet.offset + reference.sheet.length <= formula.size(), message);
    check(reference.sheet.name_offset + reference.sheet.name_length <= formula.size(), message);
    check_equal(
        formula.substr(reference.sheet.offset, reference.sheet.length), expected_qualifier,
        message);
    check_equal(
        formula.substr(reference.sheet.name_offset, reference.sheet.name_length),
        expected_name, message);
    check(reference.sheet.quoted == quoted, message);
}

void test_scan_formula_references()
{
    const std::string formula =
        R"(SUM(A1,$B$2,A1:B2,Sheet1!C3,'Other Sheet'!D:D,[Book.xlsx]Sheet1!1:1,"A1",Table1[A1],LOG10(E5),A1foo,_A1,A1_,R1C1))";
    const std::vector<fastxlsx::detail::FormulaReference> references =
        fastxlsx::detail::scan_formula_references(formula);

    check(references.size() == 7, "formula scanner should report only supported references");
    check(references[0].kind == fastxlsx::detail::FormulaReferenceKind::Cell
            && references[0].first_cell.row == 1 && references[0].first_cell.column == 1
            && !references[0].first_cell.row_absolute
            && !references[0].first_cell.column_absolute,
        "formula scanner should parse relative cell references");
    check_reference_text(formula, references[0], "A1",
        "formula scanner first reference text mismatch");
    check_no_sheet_qualifier(references[0],
        "formula scanner should leave unqualified references unqualified");
    check(references[1].kind == fastxlsx::detail::FormulaReferenceKind::Cell
            && references[1].first_cell.row == 2 && references[1].first_cell.column == 2
            && references[1].first_cell.row_absolute
            && references[1].first_cell.column_absolute,
        "formula scanner should parse absolute cell references");
    check_reference_text(formula, references[1], "$B$2",
        "formula scanner absolute reference text mismatch");
    check_no_sheet_qualifier(references[1],
        "formula scanner should leave absolute references unqualified");
    check(references[2].kind == fastxlsx::detail::FormulaReferenceKind::CellRange
            && references[2].first_cell.row == 1 && references[2].first_cell.column == 1
            && references[2].last_cell.row == 2 && references[2].last_cell.column == 2,
        "formula scanner should parse A1-style cell ranges");
    check_reference_text(formula, references[2], "A1:B2",
        "formula scanner cell range text mismatch");
    check_no_sheet_qualifier(references[2],
        "formula scanner should leave unqualified ranges unqualified");
    check_reference_text(formula, references[3], "C3",
        "formula scanner should report references after sheet qualifiers");
    check_sheet_qualifier_text(formula, references[3], "Sheet1!", "Sheet1", false,
        "formula scanner should expose unquoted sheet qualifiers");
    check(references[4].kind == fastxlsx::detail::FormulaReferenceKind::WholeColumnRange
            && references[4].first_axis.value == 4 && references[4].last_axis.value == 4,
        "formula scanner should parse whole-column ranges");
    check_reference_text(formula, references[4], "D:D",
        "formula scanner whole-column range text mismatch");
    check_sheet_qualifier_text(formula, references[4], "'Other Sheet'!", "Other Sheet",
        true, "formula scanner should expose quoted sheet qualifiers");
    check(references[5].kind == fastxlsx::detail::FormulaReferenceKind::WholeRowRange
            && references[5].first_axis.value == 1 && references[5].last_axis.value == 1,
        "formula scanner should parse whole-row ranges");
    check_reference_text(formula, references[5], "1:1",
        "formula scanner whole-row range text mismatch");
    check_sheet_qualifier_text(formula, references[5], "[Book.xlsx]Sheet1!",
        "[Book.xlsx]Sheet1", false,
        "formula scanner should expose external workbook sheet qualifiers");
    check_reference_text(formula, references[6], "E5",
        "formula scanner should skip function names but keep arguments");
    check_no_sheet_qualifier(references[6],
        "formula scanner should not infer qualifiers for function arguments");
}

void test_scan_formula_quoted_sheet_qualifier_with_escaped_quote()
{
    const std::string formula = "SUM('O''Brien'!A1,Sheet1:Sheet2!B2)";
    const std::vector<fastxlsx::detail::FormulaReference> references =
        fastxlsx::detail::scan_formula_references(formula);

    check(references.size() == 2,
        "formula scanner should keep escaped quotes and 3D qualifiers outside ref text");
    check_reference_text(formula, references[0], "A1",
        "formula scanner escaped quoted qualifier reference text mismatch");
    check_sheet_qualifier_text(formula, references[0], "'O''Brien'!", "O''Brien", true,
        "formula scanner should expose raw escaped quoted sheet qualifier");
    check_reference_text(formula, references[1], "B2",
        "formula scanner 3D qualifier reference text mismatch");
    check_sheet_qualifier_text(formula, references[1], "Sheet1:Sheet2!",
        "Sheet1:Sheet2", false,
        "formula scanner should expose raw 3D sheet qualifier");
}

void test_translate_formula_references()
{
    const std::string formula =
        R"(SUM(A1,$B$2,A1:B2,Sheet1!C3,'Other Sheet'!D:D,[Book.xlsx]Sheet1!1:1,"A1",Table1[A1],LOG10(E5),A1foo,_A1,A1_,R1C1))";
    const std::string translated = fastxlsx::detail::translate_formula_references(
        formula, fastxlsx::detail::FormulaTranslationDelta {1, 2});
    check_equal(translated,
        R"(SUM(C2,$B$2,C2:D3,Sheet1!E4,'Other Sheet'!F:F,[Book.xlsx]Sheet1!2:2,"A1",Table1[A1],LOG10(G6),A1foo,_A1,A1_,R1C1))",
        "formula translator should shift supported relative references only");

    check_equal(
        fastxlsx::detail::translate_formula_references(
            "SUM($A:B,$1:2)", fastxlsx::detail::FormulaTranslationDelta {1, 2}),
        "SUM($A:D,$1:3)",
        "formula translator should preserve whole-axis absolute endpoints");

    check_equal(
        fastxlsx::detail::translate_formula_references(
            "A1+Sheet1!A1+'O''Brien'!A1+[Book.xlsx]Sheet1!A1+Table1[A1]",
            fastxlsx::detail::FormulaTranslationDelta {2, 3}),
        "D3+Sheet1!D3+'O''Brien'!D3+[Book.xlsx]Sheet1!D3+Table1[A1]",
        "formula translator should handle sheet-qualified references and skip structured refs");
}

void test_translate_formula_out_of_bounds()
{
    check_equal(
        fastxlsx::detail::translate_formula_references(
            "XFD1048576+$XFD$1048576+XFD:XFD+1048576:1048576+$XFD:XFD+$1048576:1048576",
            fastxlsx::detail::FormulaTranslationDelta {1, 1}),
        "#REF!+$XFD$1048576+#REF!+#REF!+#REF!+#REF!",
        "formula translator should convert out-of-bounds translated refs to #REF!");

    check_equal(
        fastxlsx::detail::translate_formula_references(
            "A1+A:A+1:1", fastxlsx::detail::FormulaTranslationDelta {-1, -1}),
        "#REF!+#REF!+#REF!",
        "formula translator should reject translated refs below Excel lower bounds");
}

void test_zero_delta_preserves_formula()
{
    const std::string formula =
        R"(A1+SUM(A:A)+"A1"+'Other Sheet'!1:1+Table1[A1])";
    check_equal(
        fastxlsx::detail::translate_formula_references(
            formula, fastxlsx::detail::FormulaTranslationDelta {}),
        formula,
        "formula translator should preserve text for zero delta");
}

void test_formula_reference_audit_fields()
{
    const std::vector<fastxlsx::detail::FormulaAuditSheetCatalogEntry> catalog {
        {"Old Sheet", "New Sheet"},
        {"Data", "Data"},
    };

    {
        const std::string formula = "'Old Sheet'!B2";
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references(formula, catalog);
        check(audits.size() == 1,
            "formula audit should report quoted sheet references through semantic API");

        const fastxlsx::detail::FormulaReferenceAuditFields& fields = audits.front();
        check_equal(fields.formula_text, formula,
            "formula audit should preserve full formula text");
        check_equal(fields.sheet_qualifier_text, "'Old Sheet'!",
            "formula audit should preserve exact sheet qualifier text");
        check_equal(fields.reference_text, "B2",
            "formula audit should preserve exact reference text");
        check_equal(fields.qualified_reference_text, "'Old Sheet'!B2",
            "formula audit should preserve exact qualified reference text");
        check_equal(fields.referenced_sheet_name, "Old Sheet",
            "formula audit should decode quoted sheet names");
        check(fields.qualifier_quoted, "formula audit should mark quoted qualifiers");
        check(fields.matched_current_workbook_sheet,
            "formula audit should match ordinary local sheet qualifiers");
        check_equal(fields.matched_source_sheet_name, "Old Sheet",
            "formula audit should report matched source sheet");
        check_equal(fields.matched_planned_sheet_name, "New Sheet",
            "formula audit should report matched planned sheet");
        check(fields.references_renamed_source_name,
            "formula audit should flag stale source-name references");
        check(!fields.references_planned_sheet_name,
            "formula audit should not treat source-name references as planned-name refs");
    }

    {
        const std::string formula = "[Book.xlsx]Data!A1";
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references(formula, catalog);
        check(audits.size() == 1,
            "formula audit should report external workbook refs through semantic API");
        const fastxlsx::detail::FormulaReferenceAuditFields& fields = audits.front();
        check(fields.external_workbook_qualifier,
            "formula audit should classify external workbook qualifiers");
        check(!fields.matched_current_workbook_sheet,
            "formula audit should not match external workbook qualifiers locally");
    }

    {
        const std::string formula = "Data:Other!C3";
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references(formula, catalog);
        check(audits.size() == 1,
            "formula audit should report 3D sheet-range refs through semantic API");
        const fastxlsx::detail::FormulaReferenceAuditFields& fields = audits.front();
        check(fields.sheet_range_qualifier,
            "formula audit should classify 3D sheet-range qualifiers");
        check(!fields.matched_current_workbook_sheet,
            "formula audit should not match 3D sheet-range qualifiers locally");
    }
}

void test_scan_workbook_defined_name_formulas()
{
    const std::vector<fastxlsx::detail::FormulaAuditSheetCatalogEntry> catalog {
        {"Old Sheet", "New Sheet"},
        {"Data", "Data"},
    };
    const std::string workbook_xml =
        R"xml(<?xml version="1.0"?>)xml"
        R"xml(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)xml"
        R"xml(<sheets><sheet name="Old Sheet" sheetId="1" r:id="rId1"/></sheets>)xml"
        R"xml(<definedNames>)xml"
        R"xml(<definedName name="Global">Data!A1</definedName>)xml"
        R"xml(<definedName name="Scoped" localSheetId="0">'Old Sheet'!B2</definedName>)xml"
        R"xml(<definedName name="Unresolved" localSheetId="bad">[Book.xlsx]Data!A1</definedName>)xml"
        R"xml(</definedNames></workbook>)xml";

    const std::vector<fastxlsx::detail::SourceDefinedNameFormula> defined_names =
        fastxlsx::detail::scan_workbook_defined_name_formulas(workbook_xml, catalog);

    check(defined_names.size() == 3,
        "definedName scanner should report direct definedName formula entries");
    check_equal(defined_names[0].name, "Global",
        "definedName scanner should preserve workbook-scope name");
    check(!defined_names[0].local_sheet_scope,
        "definedName scanner should leave workbook-scope names unscoped");
    check_equal(defined_names[0].formula_text, "Data!A1",
        "definedName scanner should preserve formula text");

    check_equal(defined_names[1].name, "Scoped",
        "definedName scanner should preserve local scoped name");
    check(defined_names[1].local_sheet_scope,
        "definedName scanner should expose localSheetId scope");
    check_equal(defined_names[1].local_sheet_id_text, "0",
        "definedName scanner should preserve localSheetId text");
    check(defined_names[1].local_sheet_scope_resolved,
        "definedName scanner should resolve valid localSheetId against catalog order");
    check_equal(defined_names[1].scope_sheet_source_name, "Old Sheet",
        "definedName scanner should report scoped source sheet");
    check_equal(defined_names[1].scope_sheet_planned_name, "New Sheet",
        "definedName scanner should report scoped planned sheet");

    check(defined_names[2].local_sheet_scope,
        "definedName scanner should expose unresolved localSheetId scopes");
    check(!defined_names[2].local_sheet_scope_resolved,
        "definedName scanner should leave malformed localSheetId unresolved");

    const std::vector<fastxlsx::detail::DefinedNameFormulaReferenceAudit> reference_audits =
        fastxlsx::detail::audit_workbook_defined_name_formula_references(workbook_xml, catalog);
    check(reference_audits.size() == 3,
        "definedName audit should combine name extraction and formula reference audit");
    check_equal(reference_audits[0].defined_name.name, "Global",
        "definedName audit should preserve workbook-scope name context");
    check_equal(reference_audits[0].reference.qualified_reference_text, "Data!A1",
        "definedName audit should preserve workbook-scope qualified reference text");
    check(reference_audits[0].reference.matched_current_workbook_sheet,
        "definedName audit should match local sheet references");
    check_equal(reference_audits[1].defined_name.scope_sheet_planned_name, "New Sheet",
        "definedName audit should preserve resolved localSheetId context");
    check(reference_audits[1].reference.references_renamed_source_name,
        "definedName audit should flag stale source-name references");
    check(reference_audits[2].reference.external_workbook_qualifier,
        "definedName audit should classify external workbook references");
}

} // namespace

int main()
{
    try {
        test_scan_formula_references();
        test_scan_formula_quoted_sheet_qualifier_with_escaped_quote();
        test_translate_formula_references();
        test_translate_formula_out_of_bounds();
        test_zero_delta_preserves_formula();
        test_formula_reference_audit_fields();
        test_scan_workbook_defined_name_formulas();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
