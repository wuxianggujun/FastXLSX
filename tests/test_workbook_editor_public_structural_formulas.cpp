#include "zip_test_utils.hpp"

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
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

struct StructuralFormulaSource {
    std::filesystem::path path;
    fastxlsx::StyleId formula_style;
};

StructuralFormulaSource write_structural_formula_source(std::string_view name)
{
    StructuralFormulaSource source;
    source.path = fastxlsx::test::artifact_path(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source.path);
    source.formula_style = writer.add_style(fastxlsx::CellStyle {"0.00"});

    fastxlsx::WorksheetWriter insert_rows = writer.add_worksheet("InsertRows");
    insert_rows.append_row({fastxlsx::CellView::number(1.0)});
    insert_rows.append_row({fastxlsx::CellView::number(2.0),
        fastxlsx::CellView::formula(
            "A1+A2+$A$1+$A$2+SUM(A1:A2)").with_style(source.formula_style)});
    insert_rows.append_row({fastxlsx::CellView::number(3.0)});

    fastxlsx::WorksheetWriter delete_rows = writer.add_worksheet("DeleteRows");
    delete_rows.append_row({fastxlsx::CellView::number(1.0)});
    delete_rows.append_row({fastxlsx::CellView::number(2.0)});
    delete_rows.append_row({fastxlsx::CellView::number(3.0)});
    delete_rows.append_row({fastxlsx::CellView::number(0.0),
        fastxlsx::CellView::formula(
            "A1+A2+A3+$A$1+$A$2+$A$3+SUM(A1:A3)")
            .with_style(source.formula_style)});

    fastxlsx::WorksheetWriter insert_columns = writer.add_worksheet("InsertColumns");
    insert_columns.append_row({fastxlsx::CellView::number(1.0),
        fastxlsx::CellView::number(0.0), fastxlsx::CellView::number(3.0)});
    insert_columns.append_row({fastxlsx::CellView::number(0.0),
        fastxlsx::CellView::number(0.0), fastxlsx::CellView::number(0.0),
        fastxlsx::CellView::formula(
            "A1+C1+$A$1+$C$1+SUM(A1:C1)").with_style(source.formula_style)});

    fastxlsx::WorksheetWriter delete_columns = writer.add_worksheet("DeleteColumns");
    delete_columns.append_row({fastxlsx::CellView::number(1.0),
        fastxlsx::CellView::number(2.0), fastxlsx::CellView::number(3.0)});
    delete_columns.append_row({fastxlsx::CellView::number(0.0),
        fastxlsx::CellView::number(0.0), fastxlsx::CellView::number(0.0),
        fastxlsx::CellView::formula(
            "A1+B1+D1+$A$1+$B$1+$D$1+SUM(A1:D1)")
            .with_style(source.formula_style)});

    writer.close();
    return source;
}

void check_formula(const fastxlsx::WorksheetEditor& sheet,
    std::string_view reference, std::string_view expected,
    fastxlsx::StyleId style, const char* message)
{
    const fastxlsx::CellValue value = sheet.get_cell(reference);
    check(value.kind() == fastxlsx::CellValueKind::Formula
            && value.text_value() == expected
            && value.has_style()
            && value.style_id().value() == style.value(),
        message);
}

void test_moved_formulas_use_structural_rewrites_save_retry_and_reopen()
{
    const StructuralFormulaSource source = write_structural_formula_source(
        "fastxlsx-workbook-editor-structural-formulas-source.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source.path);
    const std::filesystem::path output = fastxlsx::test::artifact_path(
        "fastxlsx-workbook-editor-structural-formulas-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source.path);
    fastxlsx::WorksheetEditor insert_rows = editor.worksheet("InsertRows");
    fastxlsx::WorksheetEditor delete_rows = editor.worksheet("DeleteRows");
    fastxlsx::WorksheetEditor insert_columns = editor.worksheet("InsertColumns");
    fastxlsx::WorksheetEditor delete_columns = editor.worksheet("DeleteColumns");

    insert_rows.insert_rows(2, 1);
    delete_rows.delete_rows(2, 1);
    insert_columns.insert_columns(2, 2);
    delete_columns.delete_columns(2, 2);

    check_formula(insert_rows, "B3",
        "A1+A3+$A$1+$A$3+SUM(A1:A3)", source.formula_style,
        "insert_rows should structurally rewrite references in a moved formula");
    check_formula(delete_rows, "B3",
        "A1+#REF!+A2+$A$1+#REF!+$A$2+SUM(A1:A2)", source.formula_style,
        "delete_rows should preserve earlier references and reject deleted references");
    check_formula(insert_columns, "F2",
        "A1+E1+$A$1+$E$1+SUM(A1:E1)", source.formula_style,
        "insert_columns should structurally rewrite references in a moved formula");
    check_formula(delete_columns, "B2",
        "A1+#REF!+B1+$A$1+#REF!+$B$1+SUM(A1:B1)", source.formula_style,
        "delete_columns should preserve earlier references and reject deleted references");
    check(!insert_rows.contains_cell("B2") && !delete_rows.contains_cell("B4")
            && insert_columns.get_cell("D2").kind()
                == fastxlsx::CellValueKind::Number
            && !delete_columns.contains_cell("D2"),
        "structural edits should remove formulas from their old coordinates");
    check(insert_rows.has_pending_changes() && delete_rows.has_pending_changes()
            && insert_columns.has_pending_changes()
            && delete_columns.has_pending_changes()
            && editor.has_unsaved_changes(),
        "structural formula edits should dirty every affected materialized session");

    check(throws_fastxlsx_error(
              [&] { editor.save_as(fastxlsx::test::artifact_dir()); }),
        "structural formula save to a directory should fail after staging");
    check_formula(insert_rows, "B3",
        "A1+A3+$A$1+$A$3+SUM(A1:A3)", source.formula_style,
        "failed save should preserve the current structural formula projection");
    check(editor.has_unsaved_changes() && insert_rows.has_pending_changes()
            && delete_rows.has_pending_changes()
            && insert_columns.has_pending_changes()
            && delete_columns.has_pending_changes(),
        "failed save should retain all dirty structural sessions for retry");

    editor.save_as(output);
    check(!editor.has_unsaved_changes() && !insert_rows.has_pending_changes()
            && !delete_rows.has_pending_changes()
            && !insert_columns.has_pending_changes()
            && !delete_columns.has_pending_changes(),
        "successful structural formula retry should clear dirty session state");
    check(fastxlsx::test::read_zip_entries(source.path) == source_entries,
        "structural formula save_as should leave the source package unchanged");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "structural formula rewrite should preserve styles.xml bytes");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const fastxlsx::WorksheetEditor reopened_insert_rows =
        reopened.worksheet("InsertRows");
    const fastxlsx::WorksheetEditor reopened_delete_rows =
        reopened.worksheet("DeleteRows");
    const fastxlsx::WorksheetEditor reopened_insert_columns =
        reopened.worksheet("InsertColumns");
    const fastxlsx::WorksheetEditor reopened_delete_columns =
        reopened.worksheet("DeleteColumns");
    check_formula(reopened_insert_rows, "B3",
        "A1+A3+$A$1+$A$3+SUM(A1:A3)", source.formula_style,
        "reopened insert_rows output should keep structural formula semantics");
    check_formula(reopened_delete_rows, "B3",
        "A1+#REF!+A2+$A$1+#REF!+$A$2+SUM(A1:A2)", source.formula_style,
        "reopened delete_rows output should keep structural formula semantics");
    check_formula(reopened_insert_columns, "F2",
        "A1+E1+$A$1+$E$1+SUM(A1:E1)", source.formula_style,
        "reopened insert_columns output should keep structural formula semantics");
    check_formula(reopened_delete_columns, "B2",
        "A1+#REF!+B1+$A$1+#REF!+$B$1+SUM(A1:B1)", source.formula_style,
        "reopened delete_columns output should keep structural formula semantics");
}

} // namespace

int main()
{
    try {
        test_moved_formulas_use_structural_rewrites_save_retry_and_reopen();
        std::cout << "WorkbookEditor public structural formula tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WorkbookEditor public structural formula test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
