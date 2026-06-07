#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace fastxlsx {

/// Supported image container formats for the current image metadata helper.
///
/// API mode: small media metadata helper. This enum does not imply OpenXML
/// drawing/media part support; it only describes formats accepted by
/// read_image_info() and read_image_pixels().
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

/// Decoded PNG/JPEG pixels owned by the caller.
///
/// API mode: small Phase 5 / stb image pixel helper. Width and height are
/// pixel dimensions reported by `stb_image`; channel_count is the decoded
/// component count returned by `stbi_load` without forcing a different output
/// channel layout. Pixels are tightly packed row-major bytes and have
/// `width * height * channel_count` entries.
///
/// This helper allocates a complete decoded pixel buffer in memory. It does
/// not create media parts, drawing XML, relationships, or content types; it is
/// not used by the `WorksheetWriter::add_image()` streaming insertion hot path;
/// and it does not perform PNG/JPEG container conversion or existing-workbook
/// image editing.
struct ImagePixels {
    ImageFormat format = ImageFormat::Png;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channel_count = 0;
    std::vector<std::uint8_t> pixels;
};

/// Reads PNG/JPEG image dimensions and channel count from a file.
///
/// API mode: small media metadata helper used by the current streaming image
/// insertion slice for validation. FastXLSX requires the default `stb`
/// dependency and uses `stb_image` header probing through file callbacks. This
/// does not decode a full pixel buffer or retain image bytes. It has no
/// OpenXML side effects, does not allocate media parts, does not write drawing
/// XML or relationships, and does not touch worksheet streaming rows.
///
/// @throws FastXlsxError if the file cannot be opened, the file is empty, the
/// format is outside the current PNG/JPEG slice, or stb cannot read image
/// metadata.
[[nodiscard]] ImageInfo read_image_info(const std::filesystem::path& path);

/// Reads PNG/JPEG image dimensions and channel count from memory.
///
/// API mode: small media metadata helper. The byte span is consumed only for
/// the duration of the call. FastXLSX uses `stbi_info_from_memory` and avoids
/// full pixel decode. The API does not retain the supplied bytes, does not
/// create OpenXML package parts, and does not provide existing-workbook image
/// preservation.
///
/// @throws FastXlsxError if the span is empty, the format is outside the
/// current PNG/JPEG slice, or stb cannot read image metadata.
[[nodiscard]] ImageInfo read_image_info(std::span<const std::byte> bytes);

/// Decodes PNG/JPEG pixels from a file into an owned byte buffer.
///
/// API mode: small Phase 5 / stb image pixel helper for callers that explicitly
/// need decoded pixels. This reads and decodes the image during the call and
/// allocates a full `ImagePixels::pixels` buffer. It does not create OpenXML
/// media/drawing parts, does not participate in `WorksheetWriter::add_image()`,
/// and does not edit or preserve images in existing workbooks.
///
/// @throws FastXlsxError if the file cannot be opened, the file is empty, the
/// format is outside the current PNG/JPEG slice, or stb cannot decode pixels.
[[nodiscard]] ImagePixels read_image_pixels(const std::filesystem::path& path);

/// Decodes PNG/JPEG pixels from memory into an owned byte buffer.
///
/// API mode: small Phase 5 / stb image pixel helper. The byte span is consumed
/// only for the duration of the call; FastXLSX does not retain the encoded
/// bytes. This allocates a complete decoded pixel buffer, does not force output
/// channel conversion, does not create OpenXML package parts, and does not
/// provide existing-workbook image preservation or editing.
///
/// @throws FastXlsxError if the span is empty, the format is outside the
/// current PNG/JPEG slice, or stb cannot decode pixels.
[[nodiscard]] ImagePixels read_image_pixels(std::span<const std::uint8_t> bytes);

} // namespace fastxlsx
