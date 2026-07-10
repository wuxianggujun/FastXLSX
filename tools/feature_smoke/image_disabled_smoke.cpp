#include <fastxlsx/image.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <span>

int main()
{
#if FASTXLSX_HAS_IMAGES
    return 2;
#else
    try {
        (void)fastxlsx::read_image_info(std::span<const std::byte> {});
    } catch (const fastxlsx::FastXlsxError&) {
        return 0;
    }
    return 1;
#endif
}