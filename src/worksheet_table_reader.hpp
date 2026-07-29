#pragma once

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

class PackageReader;

[[nodiscard]] WorksheetTableReadSummary read_worksheet_tables_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetTableReadCallbacks& callbacks,
    WorksheetTableReaderOptions options = {});

} // namespace fastxlsx::detail
