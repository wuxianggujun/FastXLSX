#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace fastxlsx {

/// Supported image container formats for the current image metadata helper.
///
/// API mode: small media metadata helper. This enum does not imply OpenXML
/// drawing/media part support; it only describes formats accepted by
/// read_image_info().
enum class ImageFormat {
    Png,
    Jpeg,
};

/// Basic image metadata read before later media/drawing package generation.
///
/// API mode: small media metadata helper. Width and height are pixel dimensions
/// reported by `stb_image`; channel_count is the source component count reported
/// by `stbi_info`. This struct does not own pixel data and is not worksheet
/// state.
struct ImageInfo {
    ImageFormat format = ImageFormat::Png;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channel_count = 0;
};

/// Reads PNG/JPEG image dimensions and channel count from a file.
///
/// API mode: small media metadata helper used by the current streaming image
/// insertion slice for validation. When FastXLSX is built with
/// `FASTXLSX_ENABLE_STB=ON`, this uses `stb_image` header probing through file
/// callbacks and does not decode a full pixel buffer or retain image bytes. It
/// has no OpenXML side effects, does not allocate media parts, does not write
/// drawing XML or relationships, and does not touch worksheet streaming rows.
/// Without the opt-in stb dependency, the function is still declared but throws
/// FastXlsxError.
///
/// @throws FastXlsxError if stb support is disabled, the file cannot be opened,
/// the format is outside the current PNG/JPEG slice, or stb cannot read image
/// metadata.
[[nodiscard]] ImageInfo read_image_info(const std::filesystem::path& path);

/// Reads PNG/JPEG image dimensions and channel count from memory.
///
/// API mode: small media metadata helper. The byte span is consumed only for
/// the duration of the call. With `FASTXLSX_ENABLE_STB=ON`, this uses
/// `stbi_info_from_memory` and avoids full pixel decode. The API does not retain
/// the supplied bytes, does not create OpenXML package parts, and does not
/// provide existing-workbook image preservation.
///
/// @throws FastXlsxError if stb support is disabled, the span is empty, the
/// format is outside the current PNG/JPEG slice, or stb cannot read image
/// metadata.
[[nodiscard]] ImageInfo read_image_info(std::span<const std::byte> bytes);

} // namespace fastxlsx
