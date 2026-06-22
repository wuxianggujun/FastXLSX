#pragma once

#include "workbook_editor_sheet_catalog.hpp"

#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <vector>

namespace fastxlsx::detail {

class PackageEditor;
class PackageReader;

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
    const PackageEditor& editor);

} // namespace fastxlsx::detail
