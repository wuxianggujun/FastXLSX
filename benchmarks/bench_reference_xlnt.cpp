#include "reference_benchmark_common.hpp"

#include <xlnt/xlnt.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

using fastxlsx_reference_benchmark::Options;
using fastxlsx_reference_benchmark::ScopedTimer;
using fastxlsx_reference_benchmark::StringDistributionStats;

void write_sheet(xlnt::worksheet worksheet, const Options& options,
    std::uint32_t sheet_index, StringDistributionStats& string_distribution)
{
    for (std::uint32_t row = 1; row <= options.rows; ++row) {
        for (std::uint32_t col = 1; col <= options.cols; ++col) {
            auto cell = worksheet.cell(fastxlsx_reference_benchmark::cell_reference(row, col));
            if (fastxlsx_reference_benchmark::should_write_string(options, row, col)) {
                fastxlsx_reference_benchmark::observe_string_cell(
                    string_distribution, options.string_pattern, row, col);
                cell.value(fastxlsx_reference_benchmark::make_string_value(
                    options.string_pattern, sheet_index, row, col));
            } else {
                cell.value(fastxlsx_reference_benchmark::make_number_value(sheet_index, row, col));
            }
        }
    }
}

void run_benchmark(const Options& options)
{
    fastxlsx_reference_benchmark::ensure_parent_directory(options.output);
    xlnt::workbook workbook;
    StringDistributionStats string_distribution;
    const ScopedTimer timer;

    auto first_sheet = workbook.active_sheet();
    first_sheet.title("Sheet1");
    write_sheet(first_sheet, options, 1, string_distribution);

    for (std::uint32_t sheet_index = 2; sheet_index <= options.sheets; ++sheet_index) {
        auto worksheet = workbook.create_sheet();
        worksheet.title("Sheet" + std::to_string(sheet_index));
        write_sheet(worksheet, options, sheet_index, string_distribution);
    }

    workbook.save(options.output.string());

    const auto elapsed_ms = timer.elapsed_ms();
    const auto peak_bytes = fastxlsx_reference_benchmark::peak_memory_bytes();
    const auto output_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(options.output));
    fastxlsx_reference_benchmark::write_result_json(
        options, elapsed_ms, peak_bytes, output_bytes, string_distribution);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options =
            fastxlsx_reference_benchmark::parse_args(argc, argv, "xlnt", "workbook-api");
        run_benchmark(options);
        std::cout << "Wrote " << options.output.string() << '\n';
        std::cout << "Wrote " << options.result.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "xlnt reference benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
