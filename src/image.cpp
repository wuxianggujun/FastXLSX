#include <fastxlsx/image.hpp>

#include <fastxlsx/workbook.hpp>

#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#ifdef FASTXLSX_HAS_STB
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

namespace fastxlsx {
namespace {

constexpr std::array<unsigned char, 8> png_signature {
    0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a,
};

unsigned char byte_at(std::span<const std::byte> bytes, std::size_t index) noexcept
{
    return std::to_integer<unsigned char>(bytes[index]);
}

std::optional<ImageFormat> detect_image_format(std::span<const std::byte> bytes)
{
    if (bytes.size() >= png_signature.size()) {
        bool is_png = true;
        for (std::size_t index = 0; index < png_signature.size(); ++index) {
            if (byte_at(bytes, index) != png_signature[index]) {
                is_png = false;
                break;
            }
        }
        if (is_png) {
            return ImageFormat::Png;
        }
    }

    if (bytes.size() >= 3 && byte_at(bytes, 0) == 0xff && byte_at(bytes, 1) == 0xd8
        && byte_at(bytes, 2) == 0xff) {
        return ImageFormat::Jpeg;
    }

    return std::nullopt;
}

ImageFormat require_supported_format(std::span<const std::byte> bytes)
{
    if (auto format = detect_image_format(bytes); format.has_value()) {
        return *format;
    }
    throw FastXlsxError("unsupported image format; current image info reader accepts PNG and JPEG");
}

#ifndef FASTXLSX_HAS_STB
[[noreturn]] void throw_stb_disabled()
{
    throw FastXlsxError(
        "FastXLSX was built without stb image support; configure with FASTXLSX_ENABLE_STB=ON");
}
#else

ImageInfo make_image_info(ImageFormat format, int width, int height, int channels)
{
    if (width <= 0 || height <= 0 || channels <= 0) {
        throw FastXlsxError("stb returned invalid image metadata");
    }
    return ImageInfo {
        format,
        static_cast<std::uint32_t>(width),
        static_cast<std::uint32_t>(height),
        static_cast<std::uint32_t>(channels),
    };
}

struct StreamCallbackState {
    std::ifstream* stream = nullptr;
};

int read_from_stream(void* user, char* data, int size)
{
    auto* state = static_cast<StreamCallbackState*>(user);
    state->stream->read(data, static_cast<std::streamsize>(size));
    return static_cast<int>(state->stream->gcount());
}

void skip_in_stream(void* user, int count)
{
    auto* state = static_cast<StreamCallbackState*>(user);
    state->stream->clear();
    state->stream->seekg(static_cast<std::streamoff>(count), std::ios::cur);
}

int stream_is_eof(void* user)
{
    auto* state = static_cast<StreamCallbackState*>(user);
    return state->stream->eof() ? 1 : 0;
}

ImageInfo read_image_info_from_stb_memory(ImageFormat format, std::span<const std::byte> bytes)
{
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw FastXlsxError("image buffer is too large for stb_image");
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    if (stbi_info_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels)
        == 0) {
        throw FastXlsxError("stb failed to read image metadata");
    }
    return make_image_info(format, width, height, channels);
}

ImageInfo read_image_info_from_stb_stream(ImageFormat format, std::ifstream& stream)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    StreamCallbackState state {&stream};
    stbi_io_callbacks callbacks {
        read_from_stream,
        skip_in_stream,
        stream_is_eof,
    };
    if (stbi_info_from_callbacks(&callbacks, &state, &width, &height, &channels) == 0) {
        throw FastXlsxError("stb failed to read image metadata");
    }
    return make_image_info(format, width, height, channels);
}

#endif

} // namespace

ImageInfo read_image_info(const std::filesystem::path& path)
{
#ifndef FASTXLSX_HAS_STB
    (void)path;
    throw_stb_disabled();
#else
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to open image file");
    }

    std::array<std::byte, 16> prefix {};
    stream.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
    const std::streamsize bytes_read = stream.gcount();
    if (bytes_read <= 0) {
        throw FastXlsxError("image file is empty");
    }

    const auto prefix_span =
        std::span<const std::byte>(prefix.data(), static_cast<std::size_t>(bytes_read));
    const ImageFormat format = require_supported_format(prefix_span);

    stream.clear();
    stream.seekg(0, std::ios::beg);
    if (!stream) {
        throw FastXlsxError("failed to rewind image file for stb");
    }

    return read_image_info_from_stb_stream(format, stream);
#endif
}

ImageInfo read_image_info(std::span<const std::byte> bytes)
{
#ifndef FASTXLSX_HAS_STB
    (void)bytes;
    throw_stb_disabled();
#else
    if (bytes.empty()) {
        throw FastXlsxError("image buffer is empty");
    }
    const ImageFormat format = require_supported_format(bytes);
    return read_image_info_from_stb_memory(format, bytes);
#endif
}

} // namespace fastxlsx
