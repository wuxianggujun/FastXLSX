#include "../src/workbook_editor_sheet_data_replacement.hpp"

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/workbook.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
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

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

void materialize_session(
    fastxlsx::detail::MaterializedWorksheetSessionRegistry& registry,
    std::string planned_name)
{
    fastxlsx::detail::CellStore store;
    registry.materialize(std::move(planned_name), std::move(store));
}

void test_input_diagnostic_counts_ragged_rows()
{
    const std::vector<std::vector<fastxlsx::CellValue>> rows {
        {fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::text("alpha")},
        {},
        {fastxlsx::CellValue::boolean(true)},
    };

    const fastxlsx::detail::WorkbookEditorSheetDataReplacementInputDiagnostic diagnostic =
        fastxlsx::detail::workbook_editor_sheet_data_replacement_input_diagnostic(rows);

    check(diagnostic.row_count == 3, "sheet data diagnostic should count input rows");
    check(diagnostic.cell_count == 3, "sheet data diagnostic should count ragged cells");
}

void test_store_projection_preserves_empty_row_gaps()
{
    const std::vector<std::vector<fastxlsx::CellValue>> rows {
        {fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::text("alpha")},
        {},
        {fastxlsx::CellValue::boolean(true)},
    };

    const fastxlsx::detail::CellStore store =
        fastxlsx::detail::workbook_editor_sheet_data_replacement_store_from_rows(rows);

    check(store.cell_count() == 3, "replacement store should contain explicit cells only");
    check(store.try_cell(1, 1) != nullptr, "replacement store should contain A1");
    check(store.try_cell(1, 2) != nullptr, "replacement store should contain B1");
    check(store.try_cell(2, 1) == nullptr, "empty row should remain a sparse gap");

    const fastxlsx::detail::CellRecord* c3 = store.try_cell(3, 1);
    check(c3 != nullptr, "replacement store should contain A3 after an empty row");
    check(c3->kind == fastxlsx::CellValueKind::Boolean,
        "replacement store should preserve boolean cell kind");
    check(c3->boolean_value, "replacement store should preserve boolean payload");
}

void test_store_projection_enforces_cell_guardrails()
{
    const std::vector<std::vector<fastxlsx::CellValue>> rows {
        {fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)},
        {fastxlsx::CellValue::number(3.0)},
    };

    fastxlsx::detail::CellStoreOptions options;
    options.max_cells = 2;

    check(throws_fastxlsx_error([&] {
        (void)fastxlsx::detail::workbook_editor_sheet_data_replacement_store_from_rows(
            rows, options);
    }), "replacement store should enforce CellStore max_cells guardrail");
}

void test_target_validation_accepts_current_catalog_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data", "Other"});
    catalog.record_rename("Data", "Renamed");

    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Other");

    fastxlsx::detail::validate_workbook_editor_sheet_data_replacement_target(
        catalog, registry, "Renamed");
}

void test_target_validation_rejects_missing_current_names()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_sheet_data_replacement_target(
            catalog, registry, "Missing");
    }), "sheet data replacement target validation should reject missing sheets");
}

void test_target_validation_rejects_materialized_session_conflicts()
{
    fastxlsx::detail::WorkbookEditorSheetCatalogPlan catalog({"Data"});
    fastxlsx::detail::MaterializedWorksheetSessionRegistry registry;
    materialize_session(registry, "Data");

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_sheet_data_replacement_target(
            catalog, registry, "Data");
    }), "sheet data replacement target validation should reject materialized sheets");
}

} // namespace

int main()
{
    try {
        test_input_diagnostic_counts_ragged_rows();
        test_store_projection_preserves_empty_row_gaps();
        test_store_projection_enforces_cell_guardrails();
        test_target_validation_accepts_current_catalog_names();
        test_target_validation_rejects_missing_current_names();
        test_target_validation_rejects_materialized_session_conflicts();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}

