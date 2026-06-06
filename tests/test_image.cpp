#include <fastxlsx/image.hpp>
#include <fastxlsx/workbook.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename Func>
void check_fastxlsx_error(Func func, const char* message)
{
    try {
        func();
    } catch (const fastxlsx::FastXlsxError&) {
        return;
    } catch (const std::exception& error) {
        throw TestFailure(std::string(message) + ": unexpected exception: " + error.what());
    }

    throw TestFailure(message);
}

const std::array<unsigned char, 67>& tiny_rgba_png()
{
    static constexpr std::array<unsigned char, 67> bytes {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    return bytes;
}

std::span<const std::byte> tiny_png_bytes()
{
    const auto& bytes = tiny_rgba_png();
    return std::as_bytes(std::span<const unsigned char>(bytes.data(), bytes.size()));
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw TestFailure("failed to create image test file");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw TestFailure("failed to write image test file");
    }
}

void test_image_info_memory()
{
#ifdef FASTXLSX_TEST_HAS_STB
    const fastxlsx::ImageInfo info = fastxlsx::read_image_info(tiny_png_bytes());
    check(info.format == fastxlsx::ImageFormat::Png, "PNG memory format detection failed");
    check(info.width == 1, "PNG memory width failed");
    check(info.height == 1, "PNG memory height failed");
    check(info.channel_count == 4, "PNG memory channel count failed");

    const std::array<unsigned char, 6> gif_header {'G', 'I', 'F', '8', '9', 'a'};
    check_fastxlsx_error(
        [&gif_header] {
            (void)fastxlsx::read_image_info(
                std::as_bytes(std::span<const unsigned char>(gif_header.data(), gif_header.size())));
        },
        "unsupported memory image format should fail");
#else
    check_fastxlsx_error(
        [] { (void)fastxlsx::read_image_info(tiny_png_bytes()); },
        "image info memory reader should require opt-in stb support");
#endif
}

void test_image_info_file()
{
    const auto output_path = std::filesystem::current_path() / "fastxlsx-image-info.png";
    write_bytes(output_path, tiny_png_bytes());

#ifdef FASTXLSX_TEST_HAS_STB
    const fastxlsx::ImageInfo info = fastxlsx::read_image_info(output_path);
    check(info.format == fastxlsx::ImageFormat::Png, "PNG file format detection failed");
    check(info.width == 1, "PNG file width failed");
    check(info.height == 1, "PNG file height failed");
    check(info.channel_count == 4, "PNG file channel count failed");

    check_fastxlsx_error(
        [] {
            (void)fastxlsx::read_image_info(
                std::filesystem::current_path() / "fastxlsx-missing-image.png");
        },
        "missing image file should fail");
#else
    check_fastxlsx_error(
        [&output_path] { (void)fastxlsx::read_image_info(output_path); },
        "image info file reader should require opt-in stb support");
#endif
}

} // namespace

int main()
{
    try {
        test_image_info_memory();
        test_image_info_file();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
