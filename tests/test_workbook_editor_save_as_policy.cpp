#include "../src/workbook_editor_save_as_policy.hpp"

#include <fastxlsx/workbook_editor.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique_suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("fastxlsx-save-as-policy-" + std::to_string(unique_suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

bool throws_fastxlsx_error(auto&& callable)
{
    try {
        callable();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    }
    return false;
}

void write_placeholder_file(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary);
    file << "placeholder";
}

void test_save_as_policy_accepts_new_file_with_existing_parent()
{
    TemporaryDirectory temp_dir;
    const std::filesystem::path source_path = temp_dir.path() / "source.xlsx";
    const std::filesystem::path output_path = temp_dir.path() / "output.xlsx";
    write_placeholder_file(source_path);

    fastxlsx::detail::validate_workbook_editor_save_as_path(source_path, output_path);
}

void test_save_as_policy_rejects_invalid_output_paths()
{
    TemporaryDirectory temp_dir;
    const std::filesystem::path source_path = temp_dir.path() / "source.xlsx";
    write_placeholder_file(source_path);

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_save_as_path(source_path, {});
    }), "save-as policy should reject empty output paths");

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_save_as_path(source_path, temp_dir.path());
    }), "save-as policy should reject existing directory outputs");

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_save_as_path(
            source_path, temp_dir.path() / "missing" / "output.xlsx");
    }), "save-as policy should reject missing output parent directories");

    check(throws_fastxlsx_error([&] {
        fastxlsx::detail::validate_workbook_editor_save_as_path(source_path, source_path);
    }), "save-as policy should reject overwriting the source package");
}

} // namespace

int main()
{
    try {
        test_save_as_policy_accepts_new_file_with_existing_parent();
        test_save_as_policy_rejects_invalid_output_paths();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
