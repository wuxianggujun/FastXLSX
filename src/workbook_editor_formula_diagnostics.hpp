#pragma once

#include "package_reader.hpp"
#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <vector>

namespace fastxlsx::detail {

[[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit>
workbook_editor_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions);

[[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit>
workbook_editor_source_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const PackageReader& reader);

[[nodiscard]] std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
workbook_editor_defined_name_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const PackageReader& reader);

} // namespace fastxlsx::detail
