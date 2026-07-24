#pragma once

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

[[nodiscard]] WorksheetHyperlinkReadSummary
read_worksheet_hyperlinks_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships,
    const WorksheetHyperlinkReadCallbacks& callbacks,
    WorksheetHyperlinkReaderOptions options = {});

} // namespace fastxlsx::detail
