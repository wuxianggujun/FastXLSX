#include "workbook_editor_image_edit.hpp"

#include <fastxlsx/workbook.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

namespace {

std::optional<ImageFormat> image_format_from_content_type(std::string_view content_type)
{
    if (content_type == "image/png") {
        return ImageFormat::Png;
    }
    if (content_type == "image/jpeg") {
        return ImageFormat::Jpeg;
    }
    return std::nullopt;
}

std::optional<ImageFormat> image_format_from_media_part(const PackagePart& part)
{
    if (!part.name.value().starts_with("/xl/media/")) {
        return std::nullopt;
    }

    const std::optional<ImageFormat> content_type_format =
        image_format_from_content_type(part.content_type);
    if (!content_type_format.has_value()) {
        return std::nullopt;
    }

    const std::string extension = part.name.extension();
    std::optional<ImageFormat> extension_format;
    if (extension == "png") {
        extension_format = ImageFormat::Png;
    } else if (extension == "jpg" || extension == "jpeg") {
        extension_format = ImageFormat::Jpeg;
    } else {
        return std::nullopt;
    }

    if (*extension_format != *content_type_format) {
        return std::nullopt;
    }
    return extension_format;
}

} // namespace

WorkbookEditorImageTarget resolve_workbook_editor_image_target(
    const PackageManifest& manifest,
    std::string_view image_part_name)
{
    const PartName part_name(image_part_name);
    const PackagePart* part = manifest.find_part(part_name);
    if (part == nullptr) {
        throw FastXlsxError("WorkbookEditor image target is not present in current package");
    }

    const std::optional<ImageFormat> format = image_format_from_media_part(*part);
    if (!format.has_value()) {
        throw FastXlsxError(
            "WorkbookEditor image target must be an existing PNG/JPEG xl/media part whose "
            "content type matches its extension");
    }

    return WorkbookEditorImageTarget {part_name, *format};
}

void validate_workbook_editor_image_replacement_format(
    ImageFormat target_format,
    ImageFormat replacement_format)
{
    if (replacement_format != target_format) {
        throw FastXlsxError(
            "WorkbookEditor image replacement format does not match target media part");
    }
}

} // namespace fastxlsx::detail
