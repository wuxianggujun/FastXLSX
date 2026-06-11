#pragma once

/// @file fastxlsx.hpp
/// Umbrella public header for FastXLSX.
///
/// Includes both the small in-memory Workbook API and the streaming-oriented
/// WorkbookWriter API. Workbook buffers rows until save(); use WorkbookWriter for
/// large ordered exports that should not retain row data. Prefer including the
/// narrower header when compile surface matters:
///
/// - `fastxlsx/workbook.hpp` for Workbook, Worksheet, Cell, RowOptions, and
///   FastXlsxError.
/// - `fastxlsx/cell_value.hpp` for CellValue, the owning semantic value used by
///   future in-memory/editor APIs.
/// - `fastxlsx/streaming_writer.hpp` for WorkbookWriter, WorksheetWriter,
///   CellView, and StringStrategy.
/// - `fastxlsx/workbook_editor.hpp` for WorkbookEditor, the Patch-mode facade
///   that edits an existing workbook's sheet data and writes a new package.
/// - `fastxlsx/image.hpp` for stb-backed PNG/JPEG metadata probing.

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/document_properties.hpp>
#include <fastxlsx/image.hpp>
#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
