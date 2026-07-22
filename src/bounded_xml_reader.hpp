#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

using BoundedXmlInputChunkCallback = std::function<bool(std::string& output_chunk)>;

struct BoundedXmlCallbacks {
    std::function<void(std::string_view)> on_text;
    std::function<void(std::string_view)> on_tag;
    std::function<void()> on_special_markup;
};

void scan_bounded_xml_from_chunk_source(
    const BoundedXmlInputChunkCallback& read_next_chunk,
    const BoundedXmlCallbacks& callbacks,
    std::size_t max_window_bytes,
    std::string_view diagnostic_context);

} // namespace fastxlsx::detail
