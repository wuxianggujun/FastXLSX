#pragma once

#include <fastxlsx/worksheet_reader.hpp>

#include "package_reader.hpp"

namespace fastxlsx::detail {

[[nodiscard]] WorksheetImageReadSummary read_worksheet_images_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetImageReadCallbacks& callbacks,
    WorksheetImageReaderOptions options = {});

} // namespace fastxlsx::detail
