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

bool throws_exception(auto&& callable)
{
    try {
        callable();
    } catch (const std::exception&) {
        return true;
    }
    return false;
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

std::string_view token_text(
    std::string_view formula, const fastxlsx::detail::FormulaToken& token)
{
    return formula.substr(token.offset, token.length);
}

void check_token_exists(
    std::string_view formula,
    const std::vector<fastxlsx::detail::FormulaToken>& tokens,
    fastxlsx::detail::FormulaTokenKind kind,
    std::string_view expected_text,
    const char* message)
{
    for (const fastxlsx::detail::FormulaToken& token : tokens) {
        if (token.kind == kind && token_text(formula, token) == expected_text) {
            return;
        }
    }
    throw TestFailure(message);
}

void test_tokenize_formula_foundation()
{
    const std::string formula =
        R"(SUM(Sheet1!A1,"A1",'Other Sheet'!B:B,Table1[A1],1.25,A1foo,_A1,LOG10(C3)))";
    const std::vector<fastxlsx::detail::FormulaToken> tokens =
        fastxlsx::detail::tokenize_formula(formula);

    bool saw_sum_function = false;
    bool saw_string_literal = false;
    bool saw_quoted_sheet_name = false;
    bool saw_bracketed_token = false;
    bool saw_decimal_number = false;
    bool saw_a1foo_identifier = false;
    bool saw_name_like_identifier = false;
    bool saw_log_function = false;
    std::vector<fastxlsx::detail::FormulaToken> reference_tokens;

    for (const fastxlsx::detail::FormulaToken& token : tokens) {
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Function
            && token_text(formula, token) == "SUM") {
            saw_sum_function = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::StringLiteral
            && token_text(formula, token) == R"("A1")") {
            saw_string_literal = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::QuotedSheetName
            && token_text(formula, token) == "'Other Sheet'") {
            saw_quoted_sheet_name = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::BracketedToken
            && token_text(formula, token) == "[A1]") {
            saw_bracketed_token = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Number
            && token_text(formula, token) == "1.25") {
            saw_decimal_number = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Identifier
            && token_text(formula, token) == "A1foo") {
            saw_a1foo_identifier = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Identifier
            && token_text(formula, token) == "_A1") {
            saw_name_like_identifier = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Function
            && token_text(formula, token) == "LOG10") {
            saw_log_function = true;
        }
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Reference) {
            reference_tokens.push_back(token);
        }
    }

    check(saw_sum_function, "formula tokenizer should classify function identifiers");
    check(saw_string_literal, "formula tokenizer should preserve string literal spans");
    check(saw_quoted_sheet_name, "formula tokenizer should preserve quoted sheet-name tokens");
    check(saw_bracketed_token, "formula tokenizer should preserve bracketed tokens");
    check(saw_decimal_number, "formula tokenizer should classify decimal number tokens");
    check(saw_a1foo_identifier, "formula tokenizer should not split name-like A1 prefixes");
    check(saw_name_like_identifier, "formula tokenizer should keep underscore names as identifiers");
    check(saw_log_function, "formula tokenizer should classify function names containing digits");
    check(reference_tokens.size() == 3,
        "formula tokenizer should report references outside strings and structured refs only");
    check_reference_text(formula, reference_tokens[0].reference, "A1",
        "formula tokenizer first reference mismatch");
    check_sheet_qualifier_text(formula, reference_tokens[0].reference, "Sheet1!", "Sheet1",
        false, "formula tokenizer should carry unquoted sheet qualifier metadata");
    check(reference_tokens[1].reference.kind
            == fastxlsx::detail::FormulaReferenceKind::WholeColumnRange,
        "formula tokenizer should preserve whole-column reference kind");
    check_reference_text(formula, reference_tokens[1].reference, "B:B",
        "formula tokenizer whole-column reference mismatch");
    check_sheet_qualifier_text(formula, reference_tokens[1].reference, "'Other Sheet'!",
        "Other Sheet", true,
        "formula tokenizer should carry quoted sheet qualifier metadata");
    check_reference_text(formula, reference_tokens[2].reference, "C3",
        "formula tokenizer function argument reference mismatch");
    check_no_sheet_qualifier(reference_tokens[2].reference,
        "formula tokenizer should leave function argument reference unqualified");
}

void test_tokenize_formula_operator_number_and_recovery_boundaries()
{
    const std::string formula = R"(IF(A1<=10, B2>=.5, C3<>1.E2, {1,2;3,4}))";
    const std::vector<fastxlsx::detail::FormulaToken> tokens =
        fastxlsx::detail::tokenize_formula(formula);

    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Operator,
        "<=", "formula tokenizer should keep <= as one comparison operator token");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Operator,
        ">=", "formula tokenizer should keep >= as one comparison operator token");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Operator,
        "<>", "formula tokenizer should keep <> as one comparison operator token");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Number,
        ".5", "formula tokenizer should classify leading-decimal numbers");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Number,
        "1.E2", "formula tokenizer should classify exponent numbers with trailing dot");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Punctuation,
        "{", "formula tokenizer should preserve array-constant open brace");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Punctuation,
        ";", "formula tokenizer should preserve array-constant row separator");
    check_token_exists(formula, tokens, fastxlsx::detail::FormulaTokenKind::Punctuation,
        "}", "formula tokenizer should preserve array-constant close brace");

    std::vector<fastxlsx::detail::FormulaToken> reference_tokens;
    for (const fastxlsx::detail::FormulaToken& token : tokens) {
        if (token.kind == fastxlsx::detail::FormulaTokenKind::Reference) {
            reference_tokens.push_back(token);
        }
    }
    check(reference_tokens.size() == 3,
        "formula tokenizer should report comparison operand references");
    check_reference_text(formula, reference_tokens[0].reference, "A1",
        "formula tokenizer comparison left reference mismatch");
    check_reference_text(formula, reference_tokens[1].reference, "B2",
        "formula tokenizer comparison middle reference mismatch");
    check_reference_text(formula, reference_tokens[2].reference, "C3",
        "formula tokenizer comparison right reference mismatch");

    const std::string incomplete_string = R"("unterminated A1)";
    const std::vector<fastxlsx::detail::FormulaToken> string_tokens =
        fastxlsx::detail::tokenize_formula(incomplete_string);
    check(string_tokens.size() == 1,
        "formula tokenizer should preserve incomplete strings as one token");
    check(string_tokens[0].kind == fastxlsx::detail::FormulaTokenKind::StringLiteral,
        "formula tokenizer incomplete string token kind mismatch");
    check_equal(token_text(incomplete_string, string_tokens[0]), incomplete_string,
        "formula tokenizer incomplete string token span mismatch");

    const std::string incomplete_bracket = "[Book.xlsx";
    const std::vector<fastxlsx::detail::FormulaToken> bracket_tokens =
        fastxlsx::detail::tokenize_formula(incomplete_bracket);
    check(bracket_tokens.size() == 1,
        "formula tokenizer should preserve incomplete brackets as one token");
    check(bracket_tokens[0].kind == fastxlsx::detail::FormulaTokenKind::BracketedToken,
        "formula tokenizer incomplete bracket token kind mismatch");
    check_equal(token_text(incomplete_bracket, bracket_tokens[0]), incomplete_bracket,
        "formula tokenizer incomplete bracket token span mismatch");
}

void test_formula_reference_qualifier_classifier()
{
    const std::string formula =
        "A1+Sheet1!B2+'O''Brien'!C3+[Book.xlsx]Sheet1!D4+"
        "Sheet1:Sheet2!E5+[Book.xlsx]Sheet1:Sheet2!F6+Table1[A1]";
    const std::vector<fastxlsx::detail::FormulaReference> references =
        fastxlsx::detail::scan_formula_references(formula);

    check(references.size() == 6,
        "formula qualifier classifier should ignore structured reference contents");

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[0]);
        check(qualifier.kind == fastxlsx::detail::FormulaReferenceQualifierKind::None,
            "formula qualifier classifier should mark unqualified refs as none");
        check_equal(qualifier.decoded_sheet_name, "",
            "formula qualifier classifier should leave unqualified decoded name empty");
    }

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[1]);
        check(qualifier.kind == fastxlsx::detail::FormulaReferenceQualifierKind::LocalSheet,
            "formula qualifier classifier should classify ordinary local sheets");
        check_equal(qualifier.decoded_sheet_name, "Sheet1",
            "formula qualifier classifier local sheet name mismatch");
    }

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[2]);
        check(qualifier.kind == fastxlsx::detail::FormulaReferenceQualifierKind::LocalSheet,
            "formula qualifier classifier should classify quoted local sheets");
        check_equal(qualifier.decoded_sheet_name, "O'Brien",
            "formula qualifier classifier should decode escaped quote sheet names");
    }

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[3]);
        check(qualifier.kind == fastxlsx::detail::FormulaReferenceQualifierKind::ExternalWorkbook,
            "formula qualifier classifier should classify external workbook refs");
        check_equal(qualifier.decoded_sheet_name, "[Book.xlsx]Sheet1",
            "formula qualifier classifier external workbook name mismatch");
    }

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[4]);
        check(qualifier.kind == fastxlsx::detail::FormulaReferenceQualifierKind::SheetRange,
            "formula qualifier classifier should classify 3D sheet ranges");
        check_equal(qualifier.decoded_sheet_name, "Sheet1:Sheet2",
            "formula qualifier classifier sheet range name mismatch");
    }

    {
        const fastxlsx::detail::FormulaReferenceQualifierClassification qualifier =
            fastxlsx::detail::classify_formula_reference_qualifier(formula, references[5]);
        check(qualifier.kind
                == fastxlsx::detail::FormulaReferenceQualifierKind::ExternalWorkbookSheetRange,
            "formula qualifier classifier should classify external workbook 3D refs");
        check_equal(qualifier.decoded_sheet_name, "[Book.xlsx]Sheet1:Sheet2",
            "formula qualifier classifier external workbook sheet range name mismatch");
    }
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

void test_rewrite_formula_references_for_structural_edit()
{
    using fastxlsx::detail::FormulaStructuralEdit;
    using fastxlsx::detail::FormulaStructuralEditKind;

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(A1,A2,$A$2,A2:B3,Sheet1!2:2,"A2",Table1[A2]))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 2, 2}),
        R"(SUM(A1,A4,$A$4,A4:B5,Sheet1!4:4,"A2",Table1[A2]))",
        "formula structural row insert should move affected row references");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(A1+A2+A4+$A$3+Sheet1!4:4+"A2"+Table1[A2])",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        R"(A1+#REF!+A2+#REF!+Sheet1!2:2+"A2"+Table1[A2])",
        "formula structural row delete should ref deleted rows and shift later rows");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1+B1+$B$2+B:C+Sheet1!B2",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 2}),
        "A1+D1+$D$2+D:E+Sheet1!D2",
        "formula structural column insert should move affected column references");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1+B1+D1+$C$1+D:E",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteColumns, 2, 2}),
        "A1+#REF!+B1+#REF!+B:C",
        "formula structural column delete should ref deleted columns and shift later columns");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1:B4+C2:D5",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        "A1:B2+#REF!:D3",
        "formula structural row delete should rewrite cell-range endpoints independently");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1:D2+B3:E4",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteColumns, 2, 2}),
        "A1:B2+#REF!:C4",
        "formula structural column delete should rewrite cell-range endpoints independently");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A2:B3+B4:A1",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        "#REF!:#REF!+B2:A1",
        "formula structural row delete should ref fully deleted range endpoints and preserve surviving reversed ranges");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "B1:C2+D2:B1",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteColumns, 2, 2}),
        "#REF!:#REF!+B2:#REF!",
        "formula structural column delete should ref fully deleted range endpoints and preserve reversed endpoint order");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "B4:A2",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 2, 1}),
        "B5:A3",
        "formula structural row insert should preserve reversed range endpoint order");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "D2:B1",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        "E2:C1",
        "formula structural column insert should preserve reversed range endpoint order");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM('O''Brien'!A2,[Book.xlsx]Sheet1!A4,Table1[A2],"A2"))",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        R"(SUM('O''Brien'!#REF!,[Book.xlsx]Sheet1!A2,Table1[A2],"A2"))",
        "formula structural row delete should preserve qualifiers and skip non-references");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM('Other Sheet'!B:B,[Book.xlsx]Sheet1!B2,Table1[B2],"B2"))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"(SUM('Other Sheet'!C:C,[Book.xlsx]Sheet1!C2,Table1[B2],"B2"))",
        "formula structural column insert should preserve qualified whole-axis references");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "SUM(Sheet1:Sheet2!A2,[Book.xlsx]Sheet1:Sheet2!A4,Table1[A2])",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        "SUM(Sheet1:Sheet2!#REF!,[Book.xlsx]Sheet1:Sheet2!A2,Table1[A2])",
        "formula structural row delete should preserve 3D qualifier boundaries");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(B1,LOG10(B1),B1foo,_B1,B1_,R1C1,"B1",Table1[B1]))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"(SUM(C1,LOG10(C1),B1foo,_B1,B1_,R1C1,"B1",Table1[B1]))",
        "formula structural column insert should preserve function and name-like token boundaries");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(B1,Table1[[#Headers],[B1]],Table1[@[B1]],B2))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"(SUM(C1,Table1[[#Headers],[B1]],Table1[@[B1]],C2))",
        "formula structural column insert should preserve multi-bracket structured reference text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(B1,"B1""B2",B2,Table1[B1]))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"(SUM(C1,"B1""B2",C2,Table1[B1]))",
        "formula structural column insert should preserve escaped string literal text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"('B1 Sheet'!B1+[B1.xlsx]Sheet1!B1+[Book.xlsx]B1!B1)",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"('B1 Sheet'!C1+[B1.xlsx]Sheet1!C1+[Book.xlsx]B1!C1)",
        "formula structural column insert should preserve quoted sheet and external workbook token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"('B1'' Sheet'!B1+'A1''B1'!A1+[B1.xlsx]B1!B1)",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"('B1'' Sheet'!C1+'A1''B1'!A1+[B1.xlsx]B1!C1)",
        "formula structural column insert should preserve escaped quoted sheet-name token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(A:A,$2:$3,Sheet1!$B:$C))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 2, 2}),
        R"(SUM(A:A,$4:$5,Sheet1!$B:$C))",
        "formula structural row insert should move whole-row axes and preserve whole-column axes");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM($B:$C,2:3,Sheet1!$2:$3))",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 2}),
        R"(SUM($D:$E,2:3,Sheet1!$2:$3))",
        "formula structural column insert should move whole-column axes and preserve whole-row axes");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "XFD1048576+$XFD$1048576+1048576:1048576+Sheet1!XFD1048576",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 1048576, 1}),
        "#REF!+#REF!+#REF!+Sheet1!#REF!",
        "formula structural row insert should ref row references shifted past Excel bounds");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1048576:B1048576",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 1048576, 1}),
        "#REF!:#REF!",
        "formula structural row insert should ref range endpoints shifted past Excel bounds");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "XFD1048576+$XFD$1048576+XFD:XFD+Sheet1!XFD1048576",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 16384, 1}),
        "#REF!+#REF!+#REF!+Sheet1!#REF!",
        "formula structural column insert should ref column references shifted past Excel bounds");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "XFD1:XFD2",
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 16384, 1}),
        "#REF!:#REF!",
        "formula structural column insert should ref range endpoints shifted past Excel bounds");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(1:1,2:3,4:5,$5:$6,Sheet1!B:C))",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 2}),
        R"(SUM(1:1,#REF!,2:3,$3:$4,Sheet1!B:C))",
        "formula structural row delete should ref deleted whole-row axes and shift later whole-row axes");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(SUM(A:A,B:C,D:E,$E:$F,Sheet1!2:3))",
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteColumns, 2, 2}),
        R"(SUM(A:A,#REF!,B:C,$C:$D,Sheet1!2:3))",
        "formula structural column delete should ref deleted whole-column axes and shift later whole-column axes");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            R"(B1+"B1)", FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        R"(C1+"B1)",
        "formula structural column insert should preserve unterminated string token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "B1+[B1", FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        "C1+[B1",
        "formula structural column insert should preserve unterminated bracket token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "B1+'B1", FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 1}),
        "C1+'B1",
        "formula structural column insert should preserve unterminated quoted-sheet token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            "A1+B1", FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 3, 0}),
        "A1+B1",
        "formula structural rewrite should preserve text for zero-count edits");

    const std::string zero_count_formula =
        R"(SUM(B2:C3,'B2'' Sheet'!B2,Table1[B2],"B2"))";
    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            zero_count_formula,
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertRows, 2, 0}),
        zero_count_formula,
        "formula structural row insert should preserve text for zero-count edits");
    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            zero_count_formula,
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteRows, 2, 0}),
        zero_count_formula,
        "formula structural row delete should preserve text for zero-count edits");
    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            zero_count_formula,
            FormulaStructuralEdit {FormulaStructuralEditKind::InsertColumns, 2, 0}),
        zero_count_formula,
        "formula structural column insert should preserve text for zero-count edits");
    check_equal(
        fastxlsx::detail::rewrite_formula_references_for_structural_edit(
            zero_count_formula,
            FormulaStructuralEdit {FormulaStructuralEditKind::DeleteColumns, 2, 0}),
        zero_count_formula,
        "formula structural column delete should preserve text for zero-count edits");
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

void test_formula_reference_audit_matches_sheet_names_case_insensitively()
{
    const std::vector<fastxlsx::detail::FormulaAuditSheetCatalogEntry> catalog {
        {"Data", "RenamedData"},
    };

    const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
        fastxlsx::detail::audit_formula_references(
            "data!A1+RENAMEDDATA!B2+[Book.xlsx]data!C3+data:Other!D4",
            catalog);

    check(audits.size() == 4,
        "formula audit should report local, external, and 3D sheet qualifiers");

    check(audits[0].matched_current_workbook_sheet,
        "formula audit should match source sheet names ignoring ASCII case");
    check_equal(audits[0].referenced_sheet_name, "data",
        "formula audit should preserve the formula's referenced sheet spelling");
    check_equal(audits[0].matched_source_sheet_name, "Data",
        "formula audit should report the canonical source sheet name");
    check_equal(audits[0].matched_planned_sheet_name, "RenamedData",
        "formula audit should report the canonical planned sheet name");
    check(audits[0].references_renamed_source_name,
        "formula audit should flag case-varied source-name references as stale after rename");
    check(!audits[0].references_planned_sheet_name,
        "formula audit should not treat a source-name match as a planned-name match");

    check(audits[1].matched_current_workbook_sheet,
        "formula audit should match planned sheet names ignoring ASCII case");
    check_equal(audits[1].referenced_sheet_name, "RENAMEDDATA",
        "formula audit should preserve case-varied planned reference spelling");
    check(!audits[1].references_renamed_source_name,
        "formula audit should not flag planned-name references as stale");
    check(audits[1].references_planned_sheet_name,
        "formula audit should mark planned-name references");

    check(audits[2].external_workbook_qualifier &&
            !audits[2].matched_current_workbook_sheet,
        "formula audit should not local-match external workbook qualifiers");
    check(audits[3].sheet_range_qualifier &&
            !audits[3].matched_current_workbook_sheet,
        "formula audit should not local-match 3D sheet-range qualifiers");
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

void test_scan_workbook_defined_name_formulas_rejects_malformed_structure()
{
    const std::vector<fastxlsx::detail::FormulaAuditSheetCatalogEntry> catalog {
        {"Sheet1", "Sheet1"},
    };

    check(throws_exception([&] {
        (void)fastxlsx::detail::scan_workbook_defined_name_formulas(
            R"xml(<workbook><definedNames></workbook>)xml", catalog);
    }), "definedName scanner should reject mismatched workbook XML tags");

    check(throws_exception([&] {
        (void)fastxlsx::detail::scan_workbook_defined_name_formulas(
            R"xml(<workbook><definedNames><definedName name="A">Sheet1!A1</definedName></definedNames>)xml",
            catalog);
    }), "definedName scanner should reject unclosed workbook XML tags");
}

void test_rewrite_formula_sheet_references()
{
    const std::string formula =
        R"(SUM(Old!A1,'Old Sheet'!B:B,'O''Brien'!1:1,"Old!A1",Table1[Old!A1],[Book.xlsx]Old!C3,Old:Other!D4,LOG10(Old!E5)))";
    const std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> rewrites {
        {"Old", "New Name"},
        {"Old Sheet", "Renamed"},
        {"O'Brien", "Quote's"},
    };

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(formula, rewrites),
        R"(SUM('New Name'!A1,'Renamed'!B:B,'Quote''s'!1:1,"Old!A1",Table1[Old!A1],[Book.xlsx]Old!C3,Old:Other!D4,LOG10('New Name'!E5)))",
        "formula sheet rewrite should update local sheet qualifiers only");

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(
            "[Book.xlsx]Old!A1+Old:Other!B2+Table1[Old!C3]", rewrites),
        "[Book.xlsx]Old!A1+Old:Other!B2+Table1[Old!C3]",
        "formula sheet rewrite should skip external, 3D, and structured refs");

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(
            R"(Old!A1+"Old!B2)", rewrites),
        R"('New Name'!A1+"Old!B2)",
        "formula sheet rewrite should preserve unterminated string token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(
            "Old!A1+[Old.xlsx", rewrites),
        "'New Name'!A1+[Old.xlsx",
        "formula sheet rewrite should preserve unterminated bracket token text");

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(
            "Old!A1+'Old", rewrites),
        "'New Name'!A1+'Old",
        "formula sheet rewrite should preserve unterminated quoted-sheet token text");

    check(throws_exception([&] {
        (void)fastxlsx::detail::rewrite_formula_sheet_references(
            "Old!A1",
            std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> {
                {"Old", "A"},
                {"old", "B"},
            });
    }), "formula sheet rewrite should reject ambiguous rewrite rules");
}

void test_formula_reference_audit_recovery_tokens()
{
    const std::vector<fastxlsx::detail::FormulaAuditSheetCatalogEntry> catalog {
        {"Old", "New"},
    };

    {
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references(R"(Old!A1+"Old!B2)", catalog);
        check(audits.size() == 1,
            "formula audit should ignore references inside unterminated string tokens");
        check_equal(audits[0].qualified_reference_text, "Old!A1",
            "formula audit unterminated string surviving reference mismatch");
    }

    {
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references("Old!A1+[Old.xlsx", catalog);
        check(audits.size() == 1,
            "formula audit should ignore references inside unterminated bracket tokens");
        check_equal(audits[0].qualified_reference_text, "Old!A1",
            "formula audit unterminated bracket surviving reference mismatch");
    }

    {
        const std::vector<fastxlsx::detail::FormulaReferenceAuditFields> audits =
            fastxlsx::detail::audit_formula_references("Old!A1+'Old", catalog);
        check(audits.size() == 1,
            "formula audit should ignore references inside unterminated quoted-sheet tokens");
        check_equal(audits[0].qualified_reference_text, "Old!A1",
            "formula audit unterminated quoted-sheet surviving reference mismatch");
    }
}

void test_rewrite_formula_sheet_references_accepts_source_and_planned_aliases()
{
    const std::string formula =
        "Source!A1+Temp!B2+[Book.xlsx]Source!C3+Source:Other!D4";
    const std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> rewrites {
        {"Source", "Final"},
        {"Temp", "Final"},
    };

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(formula, rewrites),
        "'Final'!A1+'Final'!B2+[Book.xlsx]Source!C3+Source:Other!D4",
        "formula sheet rewrite should handle source and current planned aliases");
}

void test_rewrite_formula_sheet_references_matches_case_insensitively()
{
    const std::string formula =
        "data!A1+DATA!B2+Data!C3+[Book.xlsx]data!D4+data:Other!E5+Table1[data!F6]";
    const std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> rewrites {
        {"Data", "Final Data"},
    };

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(formula, rewrites),
        "'Final Data'!A1+'Final Data'!B2+'Final Data'!C3+"
        "[Book.xlsx]data!D4+data:Other!E5+Table1[data!F6]",
        "formula sheet rewrite should case-insensitively rewrite only local sheet qualifiers");

    check_equal(
        fastxlsx::detail::rewrite_formula_sheet_references(
            "Other!A1",
            rewrites),
        "Other!A1",
        "formula sheet rewrite should preserve bytes when no local qualifier matches");
}

void test_rewrite_workbook_defined_name_formula_references()
{
    const std::string workbook_xml =
        R"xml(<workbook><definedNames>)xml"
        R"xml(<definedName name="Global">Old!A1</definedName>)xml"
        R"xml(<definedName name="Escaped">'Old Sheet'!B2&amp;"x"</definedName>)xml"
        R"xml(<definedName name="External">[Book.xlsx]Old!C3</definedName>)xml"
        R"xml(</definedNames></workbook>)xml";
    const std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> rewrites {
        {"Old", "New Sheet"},
        {"Old Sheet", "R&D"},
    };

    check_equal(
        fastxlsx::detail::rewrite_workbook_defined_name_formula_references(
            workbook_xml, rewrites),
        R"xml(<workbook><definedNames>)xml"
        R"xml(<definedName name="Global">'New Sheet'!A1</definedName>)xml"
        R"xml(<definedName name="Escaped">'R&amp;D'!B2&amp;"x"</definedName>)xml"
        R"xml(<definedName name="External">[Book.xlsx]Old!C3</definedName>)xml"
        R"xml(</definedNames></workbook>)xml",
        "definedName formula rewrite should update direct local formulas and preserve skipped formulas");

    check_equal(
        fastxlsx::detail::rewrite_workbook_defined_name_formula_references(
            workbook_xml,
            std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> {
                {"Missing", "Other"},
            }),
        workbook_xml,
        "definedName formula rewrite should preserve bytes when no formula changes");

    check(throws_exception([&] {
        (void)fastxlsx::detail::rewrite_workbook_defined_name_formula_references(
            R"xml(<workbook><definedNames><definedName name="Nested"><x>Old!A1</x></definedName></definedNames></workbook>)xml",
            rewrites);
    }), "definedName formula rewrite should reject nested definedName XML");
}

void test_rewrite_workbook_defined_name_formula_references_prefixed_and_case_boundary()
{
    const std::string workbook_xml =
        R"xml(<x:workbook xmlns:x="urn:test"><x:definedNames>)xml"
        R"xml(<x:definedName name="CaseLocal">data!A1+DATA!B2</x:definedName>)xml"
        R"xml(<x:definedName name="Skipped">[Book.xlsx]data!C3+data:Other!D4+Table1[data!E5]</x:definedName>)xml"
        R"xml(</x:definedNames></x:workbook>)xml";
    const std::vector<fastxlsx::detail::FormulaSheetReferenceRewrite> rewrites {
        {"Data", "Renamed & O'Brien"},
    };

    check_equal(
        fastxlsx::detail::rewrite_workbook_defined_name_formula_references(
            workbook_xml, rewrites),
        R"xml(<x:workbook xmlns:x="urn:test"><x:definedNames>)xml"
        R"xml(<x:definedName name="CaseLocal">'Renamed &amp; O''Brien'!A1+'Renamed &amp; O''Brien'!B2</x:definedName>)xml"
        R"xml(<x:definedName name="Skipped">[Book.xlsx]data!C3+data:Other!D4+Table1[data!E5]</x:definedName>)xml"
        R"xml(</x:definedNames></x:workbook>)xml",
        "definedName formula rewrite should support prefixed workbook metadata, case-insensitive local matches, and XML escaping");
}

} // namespace

int main()
{
    try {
        test_tokenize_formula_foundation();
        test_tokenize_formula_operator_number_and_recovery_boundaries();
        test_formula_reference_qualifier_classifier();
        test_scan_formula_references();
        test_scan_formula_quoted_sheet_qualifier_with_escaped_quote();
        test_translate_formula_references();
        test_translate_formula_out_of_bounds();
        test_zero_delta_preserves_formula();
        test_rewrite_formula_references_for_structural_edit();
        test_formula_reference_audit_fields();
        test_formula_reference_audit_matches_sheet_names_case_insensitively();
        test_scan_workbook_defined_name_formulas();
        test_scan_workbook_defined_name_formulas_rejects_malformed_structure();
        test_rewrite_formula_sheet_references();
        test_formula_reference_audit_recovery_tokens();
        test_rewrite_formula_sheet_references_accepts_source_and_planned_aliases();
        test_rewrite_formula_sheet_references_matches_case_insensitively();
        test_rewrite_workbook_defined_name_formula_references();
        test_rewrite_workbook_defined_name_formula_references_prefixed_and_case_boundary();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
