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
/// - `fastxlsx/streaming_writer.hpp` for WorkbookWriter, WorksheetWriter,
///   CellView, and StringStrategy.

#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook.hpp>
