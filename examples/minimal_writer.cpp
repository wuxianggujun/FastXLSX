#include <fastxlsx/fastxlsx.hpp>

#include <filesystem>
#include <iostream>

int main()
{
    auto workbook = fastxlsx::Workbook::create();
    workbook.add_worksheet("Summary");
    workbook.add_worksheet("Scratch").append_row({fastxlsx::Cell::text("temporary")});

    auto& sheet = workbook.worksheet("Summary");
    sheet.append_row({
        fastxlsx::Cell::text("Name"),
        fastxlsx::Cell::text("Value"),
        fastxlsx::Cell::text("Enabled"),
    });
    sheet.append_row({
        fastxlsx::Cell::text("FastXLSX"),
        fastxlsx::Cell::number(1.0),
        fastxlsx::Cell::boolean(true),
    });
    sheet.append_row({
        fastxlsx::Cell::text("Formula"),
        fastxlsx::Cell::formula("SUM(B2:B2)"),
        fastxlsx::Cell::boolean(false),
    });

    workbook.rename_worksheet("Summary", "Report");
    workbook.remove_worksheet("Scratch");

    const auto buffered_cells = workbook.cell_count();
    const auto estimated_memory = workbook.estimated_memory_usage();

    const auto output = std::filesystem::current_path() / "fastxlsx-minimal-example.xlsx";
    workbook.save(output);

    std::cout << "Wrote " << output.string() << '\n';
    std::cout << "Buffered sheets: " << workbook.worksheet_count()
              << ", cells: " << buffered_cells
              << ", estimated buffered bytes: " << estimated_memory << '\n';
    return 0;
}
