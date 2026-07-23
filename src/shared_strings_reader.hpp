#pragma once

#include <fastxlsx/worksheet_reader.hpp>

#include <functional>
#include <string>

namespace fastxlsx::detail {

using SharedStringsInputChunkCallback = std::function<bool(std::string& output_chunk)>;

[[nodiscard]] SharedStringReadSummary read_shared_strings_from_chunk_source(
    const SharedStringsInputChunkCallback& read_next_chunk,
    const SharedStringReadCallbacks& callbacks,
    SharedStringReaderOptions options);

[[nodiscard]] SharedStringRunReadSummary read_shared_string_runs_from_chunk_source(
    const SharedStringsInputChunkCallback& read_next_chunk,
    const SharedStringRunReadCallbacks& callbacks,
    SharedStringRunReaderOptions options);

} // namespace fastxlsx::detail
