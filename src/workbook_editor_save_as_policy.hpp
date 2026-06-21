#pragma once

#include <filesystem>

namespace fastxlsx::detail {

void validate_workbook_editor_save_as_path(
    const std::filesystem::path& source_path,
    const std::filesystem::path& output_path);

} // namespace fastxlsx::detail
