#pragma once

#include <cstddef>
#include <functional>
#include <span>
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
/// All string views point into the source worksheet XML passed to
/// scan_worksheet_events() and are valid only while that source buffer remains
/// alive. Callers must copy fields they need beyond the current callback.
struct WorksheetEvent {
    WorksheetEventKind kind = WorksheetEventKind::Unsupported;
    std::string_view raw_xml;
    std::string_view element_name;
    std::string_view row_number;
    std::string_view cell_reference;
    std::string_view text;
    bool self_closing = false;
};

using WorksheetEventCallback = std::function<void(const WorksheetEvent&)>;

struct WorksheetEventReaderOptions {
    /// Maximum source bytes retained by the chunk/window scanner.
    ///
    /// This bounds incomplete XML markup or text tokens while the scanner waits
    /// for the next chunk. It is an internal guardrail, not a public large-file
    /// performance target.
    std::size_t max_window_bytes = 64U * 1024U;
};

/// Scans worksheet XML and emits source-order events through callback.
///
/// The first implementation slice recognizes XML declaration / processing
/// instructions, comments, the worksheet root, sheetData, row/cell boundaries,
/// raw text segments, cell value wrapper markup, cell value text in value-like
/// child elements, and raw worksheet metadata tags. It performs minimal
/// structure checks needed for state hygiene and intentionally does not decode
/// XML entities, repair namespaces, validate the worksheet schema, follow
/// relationships, or cache a full worksheet.
void scan_worksheet_events(
    std::string_view worksheet_xml, const WorksheetEventCallback& callback);

/// Scans worksheet XML chunks and emits source-order events through callback.
///
/// This is the first internal input-side slice for future low-memory worksheet
/// rewrites. The scanner consumes bounded chunks without concatenating the full
/// worksheet; incomplete XML markup or text is retained only up to
/// WorksheetEventReaderOptions::max_window_bytes.
///
/// Event string views are valid only for the duration of the callback. Callers
/// must copy any field they need to retain after the callback returns.
void scan_worksheet_events_from_chunks(
    std::span<const std::string_view> worksheet_xml_chunks,
    const WorksheetEventCallback& callback,
    WorksheetEventReaderOptions options = {});

} // namespace fastxlsx::detail
