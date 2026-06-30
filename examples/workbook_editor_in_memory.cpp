#include <fastxlsx/fastxlsx.hpp>

#include <filesystem>
#include <iostream>

int main()
{
    const auto source =
        std::filesystem::current_path() / "fastxlsx-editor-source-example.xlsx";
    const auto output =
        std::filesystem::current_path() / "fastxlsx-editor-output-example.xlsx";

    auto seed = fastxlsx::Workbook::create();
    seed.add_worksheet("Data");
    seed.add_worksheet("Notes");

    auto& data = seed.worksheet("Data");
    data.append_row({
        fastxlsx::Cell::text("Item"),
        fastxlsx::Cell::text("Count"),
        fastxlsx::Cell::text("Double"),
    });
    data.append_row({
        fastxlsx::Cell::text("Widgets"),
        fastxlsx::Cell::number(12.0),
        fastxlsx::Cell::formula("B2*2"),
    });
    seed.worksheet("Notes").append_row({fastxlsx::Cell::text("preserved")});
    seed.save(source);

    auto editor = fastxlsx::WorkbookEditor::open(source);
    auto sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    sheet.set_cell("A2", fastxlsx::CellValue::text("Inserted"));
    sheet.set_cell("B2", fastxlsx::CellValue::number(5.0));
    sheet.set_cell("C2", fastxlsx::CellValue::formula("B2*2"));
    sheet.set_cell("D1", fastxlsx::CellValue::text("Edited"));

    const auto used_range = sheet.used_range();
    const auto materialized_cells = sheet.cell_count();
    const auto estimated_memory = sheet.estimated_memory_usage();

    editor.save_as(output);

    std::cout << "Wrote source " << source.string() << '\n';
    std::cout << "Wrote edited output " << output.string() << '\n';
    std::cout << "Materialized cells: " << materialized_cells
              << ", estimated materialized bytes: " << estimated_memory << '\n';
    if (used_range.has_value()) {
        std::cout << "Used range: R" << used_range->first_row << "C"
                  << used_range->first_column << ":R" << used_range->last_row
                  << "C" << used_range->last_column << '\n';
    }

    return 0;
}
