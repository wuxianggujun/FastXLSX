#include "workbook_editor_formula_diagnostics.hpp"

#include "package_editor.hpp"

#include <fastxlsx/detail/formula_reference_audit.hpp>

#include <exception>
#include <string>
#include <utility>

namespace fastxlsx::detail {

namespace {

std::vector<FormulaAuditSheetCatalogEntry> formula_audit_catalog_from_sheet_catalog(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog)
{
    std::vector<FormulaAuditSheetCatalogEntry> audit_catalog;
    audit_catalog.reserve(catalog.size());
    for (const WorkbookEditorSheetCatalogEntry& entry : catalog) {
        audit_catalog.push_back(FormulaAuditSheetCatalogEntry {
            entry.source_name,
            entry.planned_name,
        });
    }
    return audit_catalog;
}

template <typename PublicAudit>
void copy_formula_reference_audit_fields(
    PublicAudit& audit, const FormulaReferenceAuditFields& fields)
{
    audit.formula_text = fields.formula_text;
    audit.sheet_qualifier_text = fields.sheet_qualifier_text;
    audit.reference_text = fields.reference_text;
    audit.qualified_reference_text = fields.qualified_reference_text;
    audit.referenced_sheet_name = fields.referenced_sheet_name;
    audit.qualifier_quoted = fields.qualifier_quoted;
    audit.external_workbook_qualifier = fields.external_workbook_qualifier;
    audit.sheet_range_qualifier = fields.sheet_range_qualifier;
    audit.matched_current_workbook_sheet = fields.matched_current_workbook_sheet;
    audit.matched_source_sheet_name = fields.matched_source_sheet_name;
    audit.matched_planned_sheet_name = fields.matched_planned_sheet_name;
    audit.references_renamed_source_name = fields.references_renamed_source_name;
    audit.references_planned_sheet_name = fields.references_planned_sheet_name;
}

} // namespace

std::vector<WorkbookEditorFormulaReferenceAudit> workbook_editor_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const MaterializedWorksheetSessionRegistry& materialized_sessions)
{
    const std::vector<FormulaAuditSheetCatalogEntry> formula_catalog =
        formula_audit_catalog_from_sheet_catalog(catalog);
    std::vector<WorkbookEditorFormulaReferenceAudit> audits;

    for (const WorkbookEditorSheetCatalogEntry& formula_sheet : catalog) {
        const MaterializedWorksheetSession* session =
            materialized_sessions.try_session(formula_sheet.planned_name);
        if (session == nullptr) {
            continue;
        }

        for (const auto& [position, record] : session->store().records()) {
            if (record.kind != CellValueKind::Formula) {
                continue;
            }

            const std::vector<FormulaReferenceAuditFields> formula_audits =
                audit_formula_references(record.text_value, formula_catalog);
            for (const FormulaReferenceAuditFields& fields : formula_audits) {
                WorkbookEditorFormulaReferenceAudit audit;
                audit.formula_sheet_source_name = formula_sheet.source_name;
                audit.formula_sheet_planned_name = formula_sheet.planned_name;
                audit.formula_cell = WorksheetCellReference {
                    position.row,
                    position.column,
                };
                copy_formula_reference_audit_fields(audit, fields);

                audits.push_back(std::move(audit));
            }
        }
    }

    return audits;
}

std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
workbook_editor_defined_name_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const PackageReader& reader)
{
    const std::vector<FormulaAuditSheetCatalogEntry> formula_catalog =
        formula_audit_catalog_from_sheet_catalog(catalog);
    const PartName workbook_part = reader.workbook_part();
    if (const PackageReaderEntry* entry = reader.find_entry(workbook_part.zip_path());
        entry != nullptr
        && entry->uncompressed_size
            > package_editor_workbook_xml_materialization_byte_limit) {
        throw FastXlsxError(
            "source workbook definedName formula audit exceeds small workbook XML limit");
    }

    std::string workbook_xml;
    try {
        workbook_xml = reader.read_entry(workbook_part.zip_path());
    } catch (const std::exception& error) {
        throw FastXlsxError(
            "failed to read source workbook XML for definedName formula audit: "
            + std::string(error.what()));
    }

    std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit> audits;
    const std::vector<DefinedNameFormulaReferenceAudit> defined_name_audits =
        audit_workbook_defined_name_formula_references(workbook_xml, formula_catalog);
    for (const DefinedNameFormulaReferenceAudit& defined_name_audit :
         defined_name_audits) {
        const SourceDefinedNameFormula& defined_name = defined_name_audit.defined_name;
        WorkbookEditorDefinedNameFormulaReferenceAudit audit;
        audit.defined_name = defined_name.name;
        audit.local_sheet_scope = defined_name.local_sheet_scope;
        audit.local_sheet_id_text = defined_name.local_sheet_id_text;
        audit.local_sheet_scope_resolved = defined_name.local_sheet_scope_resolved;
        audit.scope_sheet_source_name = defined_name.scope_sheet_source_name;
        audit.scope_sheet_planned_name = defined_name.scope_sheet_planned_name;
        copy_formula_reference_audit_fields(audit, defined_name_audit.reference);
        audits.push_back(std::move(audit));
    }

    return audits;
}

} // namespace fastxlsx::detail

