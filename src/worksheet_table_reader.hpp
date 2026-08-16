#pragma once

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/worksheet_reader.hpp>

namespace fastxlsx::detail {

class PackageReader;

struct WorksheetTablePackageView {
    WorksheetTableView table;
    std::string relationship_id;
    PartName table_part;
};

struct WorksheetTablePackageReadCallbacks {
    std::function<void(const WorksheetTablePackageView&)> on_table;
};

[[nodiscard]] WorksheetTableReadSummary read_worksheet_tables_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetTableReadCallbacks& callbacks,
    WorksheetTableReaderOptions options = {});

// Internal companion that retains the package-local identity needed by Patch
// table lifecycle edits. Public table reads intentionally use the projection
// above and never expose relationship ids or part names.
[[nodiscard]] WorksheetTableReadSummary read_worksheet_table_package_views_from_package(
    const PackageReader& package,
    const PartName& worksheet_part,
    const WorksheetTablePackageReadCallbacks& callbacks,
    WorksheetTableReaderOptions options = {});

} // namespace fastxlsx::detail
