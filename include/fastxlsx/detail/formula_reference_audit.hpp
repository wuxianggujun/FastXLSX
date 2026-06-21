#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

struct FormulaAuditSheetCatalogEntry {
    std::string source_name;
    std::string planned_name;
};

struct FormulaReferenceAuditFields {
    std::string formula_text;
    std::string sheet_qualifier_text;
    std::string reference_text;
    std::string qualified_reference_text;
    std::string referenced_sheet_name;
    bool qualifier_quoted = false;
    bool external_workbook_qualifier = false;
    bool sheet_range_qualifier = false;
    bool matched_current_workbook_sheet = false;
    std::string matched_source_sheet_name;
    std::string matched_planned_sheet_name;
    bool references_renamed_source_name = false;
    bool references_planned_sheet_name = false;
};

struct SourceDefinedNameFormula {
    std::string name;
    std::string formula_text;
    bool local_sheet_scope = false;
    std::string local_sheet_id_text;
    bool local_sheet_scope_resolved = false;
    std::string scope_sheet_source_name;
    std::string scope_sheet_planned_name;
};

struct DefinedNameFormulaReferenceAudit {
    SourceDefinedNameFormula defined_name;
    FormulaReferenceAuditFields reference;
};

[[nodiscard]] std::vector<FormulaReferenceAuditFields> audit_formula_references(
    std::string_view formula_text,
    std::span<const FormulaAuditSheetCatalogEntry> catalog);

[[nodiscard]] std::vector<SourceDefinedNameFormula> scan_workbook_defined_name_formulas(
    std::string_view workbook_xml,
    std::span<const FormulaAuditSheetCatalogEntry> catalog);

[[nodiscard]] std::vector<DefinedNameFormulaReferenceAudit>
audit_workbook_defined_name_formula_references(
    std::string_view workbook_xml,
    std::span<const FormulaAuditSheetCatalogEntry> catalog);

} // namespace fastxlsx::detail
