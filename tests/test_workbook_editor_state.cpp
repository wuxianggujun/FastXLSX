#include "../src/workbook_editor_state.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
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

void test_editor_options_project_to_cell_store_options()
{
    fastxlsx::WorkbookEditorOptions options;
    options.max_replacement_cells = 123;
    options.replacement_memory_budget_bytes = 456;

    const fastxlsx::detail::CellStoreOptions store_options =
        fastxlsx::detail::workbook_editor_cell_store_options_from_editor_options(options);
    check(store_options.max_cells == 123,
        "editor options should project max_replacement_cells to CellStoreOptions");
    check(store_options.memory_budget_bytes == 456,
        "editor options should project replacement_memory_budget_bytes to CellStoreOptions");
}

void test_worksheet_options_project_to_cell_store_options()
{
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 321;
    options.memory_budget_bytes = 654;

    const fastxlsx::detail::CellStoreOptions store_options =
        fastxlsx::detail::workbook_editor_cell_store_options_from_worksheet_options(options);
    check(store_options.max_cells == 321,
        "worksheet options should project max_cells to CellStoreOptions");
    check(store_options.memory_budget_bytes == 654,
        "worksheet options should project memory_budget_bytes to CellStoreOptions");
}

void test_source_sheet_names_keep_reader_order()
{
    const std::vector<fastxlsx::detail::WorkbookSheetReference> sheets {
        fastxlsx::detail::WorkbookSheetReference {
            "First",
            "1",
            "rId1",
            fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        },
        fastxlsx::detail::WorkbookSheetReference {
            "Second",
            "2",
            "rId2",
            fastxlsx::detail::PartName("/xl/worksheets/sheet2.xml"),
        },
    };

    const std::vector<std::string> names =
        fastxlsx::detail::workbook_editor_source_sheet_names_from_workbook_sheets(sheets);
    check(names.size() == 2, "source sheet name projection should preserve size");
    check(names[0] == "First", "source sheet name projection should preserve first name");
    check(names[1] == "Second", "source sheet name projection should preserve second name");
}

void test_public_catalog_projection_preserves_semantics()
{
    const std::vector<fastxlsx::detail::WorkbookEditorSheetCatalogEntry> internal_catalog {
        fastxlsx::detail::WorkbookEditorSheetCatalogEntry {
            "Source",
            "Renamed",
            true,
        },
        fastxlsx::detail::WorkbookEditorSheetCatalogEntry {
            "Stable",
            "Stable",
            false,
        },
    };

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> public_catalog =
        fastxlsx::detail::workbook_editor_public_catalog_from_detail_catalog(internal_catalog);
    check(public_catalog.size() == 2, "public catalog projection should preserve size");
    check(public_catalog[0].source_name == "Source",
        "public catalog projection should preserve source name");
    check(public_catalog[0].planned_name == "Renamed",
        "public catalog projection should preserve planned name");
    check(public_catalog[0].renamed,
        "public catalog projection should preserve renamed flag");
    check(public_catalog[1].source_name == "Stable" &&
            public_catalog[1].planned_name == "Stable" && !public_catalog[1].renamed,
        "public catalog projection should preserve stable entries");
}

} // namespace

int main()
{
    try {
        test_editor_options_project_to_cell_store_options();
        test_worksheet_options_project_to_cell_store_options();
        test_source_sheet_names_keep_reader_order();
        test_public_catalog_projection_preserves_semantics();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
