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
    WorksheetEventReaderOptions options = {});

} // namespace fastxlsx::detail
