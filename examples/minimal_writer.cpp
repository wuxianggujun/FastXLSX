#include <fastxlsx/fastxlsx.hpp>

#include <filesystem>
#include <iostream>

int main()
{
    auto workbook = fastxlsx::Workbook::create();
    auto& sheet = workbook.add_worksheet("Summary");

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

    const auto output = std::filesystem::current_path() / "fastxlsx-minimal-example.xlsx";
    workbook.save(output);

    std::cout << "Wrote " << output.string() << '\n';
    return 0;
}
