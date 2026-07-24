#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

[[nodiscard]] WorksheetConditionalFormatReadSummary
read_worksheet_conditional_formats_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetConditionalFormatReadCallbacks& callbacks,
    WorksheetConditionalFormatReaderOptions options = {});

} // namespace fastxlsx::detail
