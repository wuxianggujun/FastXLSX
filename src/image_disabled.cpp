#include <fastxlsx/image.hpp>
#include <fastxlsx/workbook.hpp>

#include <string>

namespace fastxlsx {
namespace {

[[noreturn]] void throw_images_disabled()
{
    throw FastXlsxError(
        "FastXLSX image support is disabled; rebuild with FASTXLSX_ENABLE_IMAGES=ON");
}

} // namespace

ImageInfo read_image_info(const std::filesystem::path&)
{
    throw_images_disabled();
}

ImageInfo read_image_info(std::span<const std::byte>)
{
    throw_images_disabled();
}

ImagePixels read_image_pixels(const std::filesystem::path&)
{
    throw_images_disabled();
}

ImagePixels read_image_pixels(std::span<const std::uint8_t>)
{
    throw_images_disabled();
}

} // namespace fastxlsx