#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

[[nodiscard]] WorksheetDataValidationReadSummary
read_worksheet_data_validations_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetDataValidationReadCallbacks& callbacks,
    WorksheetDataValidationReaderOptions options = {});

} // namespace fastxlsx::detail
