#include <fastxlsx/fastxlsx.hpp>

#include <filesystem>
#include <iostream>

int main()
{
    const auto output = std::filesystem::current_path() / "fastxlsx-streaming-example.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output);
    auto sheet = workbook.add_worksheet("Export");

    sheet.set_column_width(1, 1, 18.0);
    sheet.set_column_width(2, 4, 14.0);
    sheet.freeze_panes(1, 0);
    sheet.set_auto_filter({1, 1, 4, 4});

    sheet.append_row({
        fastxlsx::CellView::text("Item"),
        fastxlsx::CellView::text("Q1"),
        fastxlsx::CellView::text("Q2"),
        fastxlsx::CellView::text("Total"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Widgets"),
        fastxlsx::CellView::number(120.0),
        fastxlsx::CellView::number(150.0),
        fastxlsx::CellView::formula("SUM(B2:C2)"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Gadgets"),
        fastxlsx::CellView::number(80.0),
        fastxlsx::CellView::number(95.0),
        fastxlsx::CellView::formula("SUM(B3:C3)"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Complete"),
        fastxlsx::CellView::boolean(true),
        fastxlsx::CellView::text(" reviewed "),
        fastxlsx::CellView::formula("SUM(D2:D3)"),
    });

    workbook.close();

    std::cout << "Wrote " << output.string() << '\n';
    return 0;
}
