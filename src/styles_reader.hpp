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

[[nodiscard]] StyleComponentReadSummary read_style_components_from_chunk_source(
    const StylesInputChunkCallback& read_next_chunk,
    const StyleComponentReadCallbacks& callbacks,
    StyleComponentReaderOptions options);

} // namespace fastxlsx::detail
