#pragma once

/// @file fastxlsx.hpp
/// Umbrella public header for FastXLSX.
///
/// Includes the small in-memory Workbook API plus Streaming write/read and
/// existing-workbook edit facades. Workbook buffers rows until save(); use
/// WorkbookWriter for large ordered exports and WorkbookReader for bounded
/// forward traversal of an existing worksheet. Prefer including the narrower
/// header when compile surface matters:
///
/// - `fastxlsx/workbook.hpp` for Workbook, Worksheet, Cell, RowOptions, and
///   FastXlsxError.
/// - `fastxlsx/cell_value.hpp` for CellValue, the owning semantic value used by
///   future in-memory/editor APIs.
/// - `fastxlsx/streaming_writer.hpp` for WorkbookWriter, WorksheetWriter,
///   CellView, and StringStrategy.
/// - `fastxlsx/workbook_editor.hpp` for WorkbookEditor, the Patch-mode facade
///   that edits existing workbook sheet data / targeted existing cells and
///   writes a new package.
/// - `fastxlsx/worksheet_reader.hpp` for forward-only, bounded-memory row/cell,
///   worksheet-metadata/data-validation, shared-string item/run, cell-format,
///   and style-component traversal of an existing workbook.
/// - `fastxlsx/image.hpp` for stb-backed PNG/JPEG metadata probing.

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/document_properties.hpp>
#include <fastxlsx/image.hpp>
#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/worksheet_metadata.hpp>
#include <fastxlsx/worksheet_reader.hpp>
