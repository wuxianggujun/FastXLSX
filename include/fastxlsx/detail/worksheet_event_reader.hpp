#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

/// Internal worksheet XML event categories for the first P8 reader slice.
///
/// This is a bounded streaming-shape contract for future controlled worksheet
/// rewrites. It is not a public API, a full XML parser, schema validation, or a
/// worksheet DOM/cell-matrix materialization layer.
enum class WorksheetEventKind {
    XmlDeclaration,
    ProcessingInstruction,
    Comment,
    WorksheetStart,
    WorksheetEnd,
    Metadata,
    SheetDataStart,
    SheetDataEnd,
    RowStart,
    RowEnd,
    CellStart,
    CellEnd,
    RawText,
    CellValueMarkup,
    CellValue,
    Unsupported,
};

/// A non-owning worksheet event view.
///
/// Event string views are callback-lifetime only when emitted by the chunk or
/// chunk-source scanners. Callers must copy fields they need beyond the current
/// callback.
struct WorksheetEvent {
    WorksheetEventKind kind = WorksheetEventKind::Unsupported;
    std::string_view raw_xml;
    std::string_view element_name;
    std::string_view row_number;
    std::string_view cell_reference;
    std::string_view text;
    bool self_closing = false;
    /// Absolute byte offset of raw_xml/text in the scanned worksheet source.
    ///
    /// Offsets are measured from the first byte returned by the chunk source.
    /// They are intended for internal staged/indexed rewrite foundations; they
    /// are not ZIP-entry offsets and do not imply source package seekability.
    std::uint64_t raw_xml_offset = 0;
};

using WorksheetEventCallback = std::function<void(const WorksheetEvent&)>;
using WorksheetEventWindowCallback = std::function<void()>;

struct WorksheetEventReaderTelemetry {
    std::uint64_t parsed_event_count = 0;
    std::uint64_t callback_event_count = 0;
    std::uint64_t coalesced_input_event_count = 0;
    std::uint64_t coalesced_output_event_count = 0;
    std::uint64_t simple_inline_string_fast_path_count = 0;
    std::uint64_t simple_inline_string_fast_path_bytes = 0;
    std::uint64_t simple_inline_string_fallback_count = 0;
};

/// Internal pull-based worksheet XML chunk source.
///
/// The callback should replace output_chunk with the next input chunk and return
/// true. Returning false signals end-of-input. Event-reader callbacks never
/// retain views into output_chunk; source implementations may reuse the same
/// storage across calls.
using WorksheetInputChunkCallback = std::function<bool(std::string& output_chunk)>;

struct WorksheetEventReaderOptions {
    /// Maximum source bytes retained by the chunk/window scanner.
    ///
    /// This bounds incomplete XML markup or text tokens while the scanner waits
    /// for the next chunk. It is an internal guardrail, not a public large-file
    /// performance target.
    std::size_t max_window_bytes = 64U * 1024U;

    /// Copies current row/cell attributes so later nested/end events can expose
    /// them safely.
    ///
    /// Target-only scanners can disable this when they only need the attribute
    /// on the start tag event and track active cell state themselves.
    bool copy_context_attributes = true;

    /// Coalesces adjacent non-formula cell value wrapper/text events before
    /// invoking the callback. Complete simple inline-string payloads already
    /// present in the bounded window are emitted as one exact-byte span;
    /// complex or boundary-split payloads retain the ordinary parser path.
    ///
    /// This preserves exact source bytes and parser validation while reducing
    /// callback traffic for Patch rewrite hot paths. The default remains false
    /// so general event consumers keep the detailed event contract.
    bool coalesce_cell_value_events = false;

    /// Optional internal counters for profiling parser/callback traffic.
    WorksheetEventReaderTelemetry* telemetry = nullptr;

};

/// Scans worksheet XML from a pull-based chunk source.
///
/// The scanner recognizes XML declaration / processing instructions, comments,
/// the worksheet root, sheetData, row/cell boundaries, raw text segments, cell
/// value wrapper markup, cell value text in value-like child elements, and raw
/// worksheet metadata tags. It consumes bounded chunks without concatenating the
/// full worksheet; incomplete XML markup or text is retained only up to
/// WorksheetEventReaderOptions::max_window_bytes. It performs minimal structure
/// checks needed for state hygiene and intentionally does not decode XML
/// entities, repair namespaces, validate the worksheet schema, follow
/// relationships, or cache a full worksheet.
///
/// Event string views are valid only for the duration of the callback. Callers
/// must copy any field they need to retain after the callback returns.
void scan_worksheet_events_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetEventCallback& callback,
    WorksheetEventReaderOptions options = {},
    const WorksheetEventWindowCallback& window_consumed_callback = {});

} // namespace fastxlsx::detail
