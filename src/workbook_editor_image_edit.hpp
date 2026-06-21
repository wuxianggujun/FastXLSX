#pragma once

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/image.hpp>

#include <string_view>

namespace fastxlsx::detail {

struct WorkbookEditorImageTarget {
    PartName part_name;
    ImageFormat format = ImageFormat::Png;
};

[[nodiscard]] WorkbookEditorImageTarget resolve_workbook_editor_image_target(
    const PackageManifest& manifest,
    std::string_view image_part_name);

void validate_workbook_editor_image_replacement_format(
    ImageFormat target_format,
    ImageFormat replacement_format);

} // namespace fastxlsx::detail
