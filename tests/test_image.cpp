#include <fastxlsx/image.hpp>
#include <fastxlsx/workbook.hpp>

#include "image_test_bytes.hpp"

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
    const fastxlsx::ImageInfo png_info = fastxlsx::read_image_info(fastxlsx::test::tiny_png_bytes());
    check(png_info.format == fastxlsx::ImageFormat::Png, "PNG memory format detection failed");
    check(png_info.width == 1, "PNG memory width failed");
    check(png_info.height == 1, "PNG memory height failed");
    check(png_info.channel_count == 4, "PNG memory channel count failed");

    const fastxlsx::ImageInfo jpeg_info = fastxlsx::read_image_info(fastxlsx::test::tiny_jpeg_bytes());
    check(jpeg_info.format == fastxlsx::ImageFormat::Jpeg, "JPEG memory format detection failed");
    check(jpeg_info.width == 2, "JPEG memory width failed");
    check(jpeg_info.height == 1, "JPEG memory height failed");
    check(jpeg_info.channel_count == 3, "JPEG memory channel count failed");

    const std::array<unsigned char, 6> gif_header {'G', 'I', 'F', '8', '9', 'a'};
    check_fastxlsx_error(
        [&gif_header] {
            (void)fastxlsx::read_image_info(
                std::as_bytes(std::span<const unsigned char>(gif_header.data(), gif_header.size())));
        },
        "unsupported memory image format should fail");
#else
    check_fastxlsx_error(
        [] { (void)fastxlsx::read_image_info(fastxlsx::test::tiny_png_bytes()); },
        "image info memory reader should require opt-in stb support");
#endif
}

void test_image_info_file()
{
    const auto png_path = std::filesystem::current_path() / "fastxlsx-image-info.png";
    write_bytes(png_path, fastxlsx::test::tiny_png_bytes());
    const auto jpeg_path = std::filesystem::current_path() / "fastxlsx-image-info.jpg";
    write_bytes(jpeg_path, fastxlsx::test::tiny_jpeg_bytes());

#ifdef FASTXLSX_TEST_HAS_STB
    const fastxlsx::ImageInfo png_info = fastxlsx::read_image_info(png_path);
    check(png_info.format == fastxlsx::ImageFormat::Png, "PNG file format detection failed");
    check(png_info.width == 1, "PNG file width failed");
    check(png_info.height == 1, "PNG file height failed");
    check(png_info.channel_count == 4, "PNG file channel count failed");

    const fastxlsx::ImageInfo jpeg_info = fastxlsx::read_image_info(jpeg_path);
    check(jpeg_info.format == fastxlsx::ImageFormat::Jpeg, "JPEG file format detection failed");
    check(jpeg_info.width == 2, "JPEG file width failed");
    check(jpeg_info.height == 1, "JPEG file height failed");
    check(jpeg_info.channel_count == 3, "JPEG file channel count failed");

    check_fastxlsx_error(
        [] {
            (void)fastxlsx::read_image_info(
                std::filesystem::current_path() / "fastxlsx-missing-image.png");
        },
        "missing image file should fail");
#else
    check_fastxlsx_error(
        [&png_path] { (void)fastxlsx::read_image_info(png_path); },
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
