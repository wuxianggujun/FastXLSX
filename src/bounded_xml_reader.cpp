#include "bounded_xml_reader.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace {

[[noreturn]] void fail(std::string_view context, std::string_view message)
{
    throw fastxlsx::FastXlsxError(
        std::string(context) + " reader " + std::string(message));
}

std::size_t find_markup_end(std::string_view xml, std::size_t open) noexcept
{
    char quote = '\0';
    for (std::size_t index = open + 1; index < xml.size(); ++index) {
        const char ch = xml[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '>') {
            return index;
        }
    }
    return std::string_view::npos;
}

std::size_t consume_available(std::string_view window,
    bool final_chunk,
    const fastxlsx::detail::BoundedXmlCallbacks& callbacks,
    std::string_view context)
{
    std::size_t position = 0;
    while (position < window.size()) {
        if (window[position] != '<') {
            const std::size_t open = window.find('<', position);
            if (open == std::string_view::npos && !final_chunk) {
                return position;
            }
            const std::size_t end =
                open == std::string_view::npos ? window.size() : open;
            if (callbacks.on_text) {
                callbacks.on_text(window.substr(position, end - position));
            }
            position = end;
            continue;
        }

        constexpr std::string_view comment_open = "<!--";
        const std::string_view remaining = window.substr(position);
        if (!final_chunk && remaining.size() < comment_open.size()
            && comment_open.starts_with(remaining)) {
            return position;
        }
        if (remaining.starts_with(comment_open)) {
            const std::size_t end = window.find("-->", position + 4);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                fail(context, "found an unterminated XML comment");
            }
            if (callbacks.on_special_markup) {
                callbacks.on_special_markup();
            }
            position = end + 3;
            continue;
        }
        if (window.substr(position).starts_with("<?")) {
            const std::size_t end = window.find("?>", position + 2);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                fail(context, "found an unterminated processing instruction");
            }
            if (callbacks.on_special_markup) {
                callbacks.on_special_markup();
            }
            position = end + 2;
            continue;
        }
        if (window.substr(position).starts_with("<!")) {
            fail(context, "does not support declaration or CDATA markup");
        }

        const std::size_t close = find_markup_end(window, position);
        if (close == std::string_view::npos) {
            if (!final_chunk) {
                return position;
            }
            fail(context, "found unterminated XML markup");
        }
        if (callbacks.on_tag) {
            callbacks.on_tag(window.substr(position, close + 1 - position));
        }
        position = close + 1;
    }
    return position;
}

void process_window(std::string& window,
    bool final_chunk,
    const fastxlsx::detail::BoundedXmlCallbacks& callbacks,
    std::string_view context)
{
    const std::size_t consumed =
        consume_available(window, final_chunk, callbacks, context);
    if (consumed != 0) {
        window.erase(0, consumed);
    }
}

void process_source_chunk(std::string_view chunk,
    std::size_t max_window_bytes,
    std::string& window,
    const fastxlsx::detail::BoundedXmlCallbacks& callbacks,
    std::string_view context)
{
    std::size_t chunk_offset = 0;
    while (chunk_offset < chunk.size()) {
        process_window(window, false, callbacks, context);
        if (window.size() >= max_window_bytes) {
            fail(context, "exceeded bounded input window");
        }

        const std::size_t available = max_window_bytes - window.size();
        const std::size_t bytes_to_append =
            std::min(available, chunk.size() - chunk_offset);
        window.append(chunk.data() + chunk_offset, bytes_to_append);
        chunk_offset += bytes_to_append;
        process_window(window, false, callbacks, context);

        if (bytes_to_append == 0 && !window.empty()) {
            fail(context, "exceeded bounded input window");
        }
    }
}

} // namespace

namespace fastxlsx::detail {

void scan_bounded_xml_from_chunk_source(
    const BoundedXmlInputChunkCallback& read_next_chunk,
    const BoundedXmlCallbacks& callbacks,
    std::size_t max_window_bytes,
    std::string_view diagnostic_context)
{
    if (!read_next_chunk) {
        throw FastXlsxError(
            std::string(diagnostic_context) + " reader requires a chunk source");
    }
    if (max_window_bytes == 0) {
        throw FastXlsxError(std::string(diagnostic_context)
            + " reader requires nonzero max_xml_window_bytes");
    }

    std::string window;
    window.reserve(std::min<std::size_t>(max_window_bytes, 4096U));

    std::string chunk;
    while (read_next_chunk(chunk)) {
        process_source_chunk(
            chunk, max_window_bytes, window, callbacks, diagnostic_context);
    }
    process_window(window, true, callbacks, diagnostic_context);
}

} // namespace fastxlsx::detail
