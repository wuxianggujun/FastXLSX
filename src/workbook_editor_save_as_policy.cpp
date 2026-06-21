#include "workbook_editor_save_as_policy.hpp"

#include <fastxlsx/workbook_editor.hpp>

#include <system_error>

namespace fastxlsx::detail {

namespace {

bool same_existing_path(
    const std::filesystem::path& left, const std::filesystem::path& right) noexcept
{
    std::error_code error;
    const bool same = std::filesystem::equivalent(left, right, error);
    return !error && same;
}

bool path_is_existing_directory(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    return !error && directory;
}

bool path_parent_is_not_directory(const std::filesystem::path& path) noexcept
{
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return false;
    }

    std::error_code error;
    const bool directory = std::filesystem::is_directory(parent, error);
    return error || !directory;
}

} // namespace

void validate_workbook_editor_save_as_path(
    const std::filesystem::path& source_path,
    const std::filesystem::path& output_path)
{
    if (output_path.empty()) {
        throw FastXlsxError("PackageEditor output path cannot be empty");
    }
    if (path_is_existing_directory(output_path)) {
        throw FastXlsxError("PackageEditor output path is an existing directory");
    }
    if (path_parent_is_not_directory(output_path)) {
        throw FastXlsxError("PackageEditor output parent path is not an existing directory");
    }
    if (same_existing_path(source_path, output_path)) {
        throw FastXlsxError("PackageEditor cannot save over the source package");
    }
}

} // namespace fastxlsx::detail
