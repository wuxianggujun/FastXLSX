#pragma once

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/worksheet_reader.hpp>

#include <functional>
#include <string_view>

namespace fastxlsx::detail {

struct WorksheetHyperlinkInternalCallbacks {
    std::function<void(
        const WorksheetHyperlinkView&, std::string_view relationship_id)>
        on_hyperlink;
};

[[nodiscard]] WorksheetHyperlinkReadSummary
read_worksheet_hyperlinks_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const RelationshipSet* worksheet_relationships,
    const WorksheetHyperlinkReadCallbacks& callbacks,
    WorksheetHyperlinkReaderOptions options = {},
    const WorksheetHyperlinkInternalCallbacks& internal_callbacks = {});

} // namespace fastxlsx::detail
