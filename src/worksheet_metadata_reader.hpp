#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

[[nodiscard]] WorksheetMetadataReadSummary read_worksheet_metadata_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetMetadataReadCallbacks& callbacks,
    WorksheetMetadataReaderOptions options = {});

} // namespace fastxlsx::detail
