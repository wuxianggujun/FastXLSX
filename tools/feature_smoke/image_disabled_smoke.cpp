#include <fastxlsx/image.hpp>
#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_reader.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

int main()
{
#if FASTXLSX_HAS_IMAGES
    return 2;
#else
    bool image_helper_threw = false;
    try {
        (void)fastxlsx::read_image_info(std::span<const std::byte> {});
    } catch (const fastxlsx::FastXlsxError&) {
        image_helper_threw = true;
    }

    const std::filesystem::path path =
        std::filesystem::current_path() / "fastxlsx-image-disabled-smoke.xlsx";
    bool worksheet_reader_threw = false;
    try {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
        fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
        sheet.append_row({fastxlsx::CellView::text("no images")});
        writer.close();
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
        try {
            (void)reader.read_worksheet_images("Data");
        } catch (const fastxlsx::FastXlsxError&) {
            worksheet_reader_threw = true;
        }
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(path, cleanup_error);
        return 3;
    }
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    return image_helper_threw && worksheet_reader_threw ? 0 : 1;
#endif
}
