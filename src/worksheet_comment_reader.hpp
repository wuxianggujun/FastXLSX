#pragma once

#include <fastxlsx/worksheet_reader.hpp>

#include "package_reader.hpp"

namespace fastxlsx::detail {

[[nodiscard]] WorksheetCommentReadSummary read_worksheet_comments_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetCommentReadCallbacks& callbacks,
    WorksheetCommentReaderOptions options = {});

} // namespace fastxlsx::detail
