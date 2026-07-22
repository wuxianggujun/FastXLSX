#pragma once

#include <fastxlsx/worksheet_reader.hpp>

#include <functional>
#include <string>

namespace fastxlsx::detail {

using StylesInputChunkCallback = std::function<bool(std::string& output_chunk)>;

[[nodiscard]] CellFormatReadSummary read_cell_formats_from_chunk_source(
    const StylesInputChunkCallback& read_next_chunk,
    const CellFormatReadCallbacks& callbacks,
    CellFormatReaderOptions options);

} // namespace fastxlsx::detail
