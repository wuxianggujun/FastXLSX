#include "workbook_editor_formula_diagnostics.hpp"

#include "package_editor.hpp"
#include "workbook_editor_worksheet_access.hpp"

#include <fastxlsx/detail/formula.hpp>
#include <fastxlsx/detail/formula_reference_audit.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <cstdint>
#include <cstddef>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
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

struct SourceWorksheetFormulaCell {
    WorksheetCellReference cell;
    std::string formula_text;
};

struct ActiveSourceFormulaCell {
    WorksheetCellReference cell;
    std::string raw_formula_text;
    std::optional<std::uint64_t> shared_formula_index;
    bool shared_formula = false;
};

struct SourceSharedFormulaDefinition {
    WorksheetCellReference base_cell;
    std::string formula_text;
};

using SourceSharedFormulaDefinitions =
    std::map<std::uint64_t, SourceSharedFormulaDefinition>;

std::string unescape_source_formula_text(std::string_view value)
{
    std::string output;
    output.reserve(value.size());

    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const char ch = value[offset];
        if (ch != '&') {
            output.push_back(ch);
            continue;
        }

        const std::size_t semicolon = value.find(';', offset + 1);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "source worksheet formula audit found an unterminated XML entity");
        }

        const std::string_view entity = value.substr(offset + 1, semicolon - offset - 1);
        if (entity == "amp") {
            output.push_back('&');
        } else if (entity == "lt") {
            output.push_back('<');
        } else if (entity == "gt") {
            output.push_back('>');
        } else if (entity == "quot") {
            output.push_back('"');
        } else if (entity == "apos") {
            output.push_back('\'');
        } else {
            output.push_back('&');
            output.append(entity);
            output.push_back(';');
        }
        offset = semicolon;
    }

    return output;
}

bool is_closing_markup(std::string_view raw_xml) noexcept
{
    return raw_xml.size() >= 2 && raw_xml[0] == '<' && raw_xml[1] == '/';
}

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string_view tag_body(std::string_view raw_tag) noexcept
{
    if (raw_tag.size() < 2 || raw_tag.front() != '<') {
        return {};
    }

    std::string_view body = raw_tag.substr(1, raw_tag.size() - 2);
    while (!body.empty() && is_xml_space(body.front())) {
        body.remove_prefix(1);
    }
    if (!body.empty() && body.front() == '/') {
        body.remove_prefix(1);
        while (!body.empty() && is_xml_space(body.front())) {
            body.remove_prefix(1);
        }
    }
    while (!body.empty() && is_xml_space(body.back())) {
        body.remove_suffix(1);
    }
    if (!body.empty() && body.back() == '/') {
        body.remove_suffix(1);
        while (!body.empty() && is_xml_space(body.back())) {
            body.remove_suffix(1);
        }
    }
    return body;
}

std::optional<std::string_view> source_formula_tag_attribute(
    std::string_view raw_tag,
    std::string_view attribute_name)
{
    std::string_view body = tag_body(raw_tag);
    std::size_t offset = 0;
    while (offset < body.size() && !is_xml_space(body[offset])) {
        ++offset;
    }

    while (offset < body.size()) {
        while (offset < body.size() && is_xml_space(body[offset])) {
            ++offset;
        }
        if (offset >= body.size() || body[offset] == '/') {
            break;
        }

        const std::size_t name_begin = offset;
        while (offset < body.size() && !is_xml_space(body[offset])
               && body[offset] != '=' && body[offset] != '/') {
            ++offset;
        }
        const std::string_view name = body.substr(name_begin, offset - name_begin);

        while (offset < body.size() && is_xml_space(body[offset])) {
            ++offset;
        }
        if (offset >= body.size() || body[offset] != '=') {
            return std::nullopt;
        }
        ++offset;
        while (offset < body.size() && is_xml_space(body[offset])) {
            ++offset;
        }
        if (offset >= body.size()
            || (body[offset] != '"' && body[offset] != '\'')) {
            return std::nullopt;
        }

        const char quote = body[offset++];
        const std::size_t value_begin = offset;
        while (offset < body.size() && body[offset] != quote) {
            ++offset;
        }
        if (offset >= body.size()) {
            return std::nullopt;
        }

        if (name == attribute_name) {
            return body.substr(value_begin, offset - value_begin);
        }
        ++offset;
    }

    return std::nullopt;
}

std::optional<std::uint64_t> parse_source_shared_formula_index(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

bool source_formula_tag_is_shared(std::string_view raw_tag)
{
    const std::optional<std::string_view> formula_type =
        source_formula_tag_attribute(raw_tag, "t");
    return formula_type.has_value() && *formula_type == "shared";
}

std::optional<std::uint64_t> source_formula_tag_shared_index(std::string_view raw_tag)
{
    const std::optional<std::string_view> index =
        source_formula_tag_attribute(raw_tag, "si");
    if (!index.has_value()) {
        return std::nullopt;
    }
    return parse_source_shared_formula_index(*index);
}

WorksheetCellReference worksheet_cell_reference_from_a1(std::string_view cell_reference)
{
    const WorksheetEditorCellCoordinate coordinate =
        parse_worksheet_editor_a1_cell_reference(cell_reference);
    return WorksheetCellReference {coordinate.row, coordinate.column};
}

SourceWorksheetFormulaCell source_formula_cell_from_definition(
    const WorksheetCellReference& follower_cell,
    const SourceSharedFormulaDefinition& definition)
{
    const FormulaTranslationDelta delta {
        static_cast<std::int64_t>(follower_cell.row)
            - static_cast<std::int64_t>(definition.base_cell.row),
        static_cast<std::int64_t>(follower_cell.column)
            - static_cast<std::int64_t>(definition.base_cell.column),
    };
    return SourceWorksheetFormulaCell {
        follower_cell,
        translate_formula_references(definition.formula_text, delta),
    };
}

void push_completed_source_formula_cell(
    std::vector<SourceWorksheetFormulaCell>& formula_cells,
    SourceSharedFormulaDefinitions& shared_formula_definitions,
    const ActiveSourceFormulaCell& active_formula)
{
    if (active_formula.raw_formula_text.empty()) {
        return;
    }

    const std::string formula_text =
        unescape_source_formula_text(active_formula.raw_formula_text);
    formula_cells.push_back(SourceWorksheetFormulaCell {
        active_formula.cell,
        formula_text,
    });

    if (active_formula.shared_formula
        && active_formula.shared_formula_index.has_value()) {
        shared_formula_definitions[*active_formula.shared_formula_index] =
            SourceSharedFormulaDefinition {
                active_formula.cell,
                formula_text,
            };
    }
}

std::vector<SourceWorksheetFormulaCell> scan_source_worksheet_formula_cells(
    const PackageReader& reader,
    const WorkbookSheetReference& sheet)
{
    PackageReaderChunkCallback source = reader.entry_chunk_source(sheet.part_name.zip_path());
    std::vector<SourceWorksheetFormulaCell> formula_cells;
    std::optional<ActiveSourceFormulaCell> active_formula;
    SourceSharedFormulaDefinitions shared_formula_definitions;

    scan_worksheet_events_from_chunk_source(
        [source = std::move(source)](std::string& output_chunk) mutable {
            return source(output_chunk);
        },
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::CellValueMarkup
                && event.element_name == "f") {
                if (event.self_closing) {
                    if (source_formula_tag_is_shared(event.raw_xml)) {
                        const std::optional<std::uint64_t> shared_index =
                            source_formula_tag_shared_index(event.raw_xml);
                        if (shared_index.has_value()) {
                            const auto definition =
                                shared_formula_definitions.find(*shared_index);
                            if (definition != shared_formula_definitions.end()) {
                                formula_cells.push_back(source_formula_cell_from_definition(
                                    worksheet_cell_reference_from_a1(event.cell_reference),
                                    definition->second));
                            }
                        }
                    }
                    return;
                }

                if (is_closing_markup(event.raw_xml)) {
                    if (active_formula.has_value()) {
                        push_completed_source_formula_cell(
                            formula_cells, shared_formula_definitions, *active_formula);
                    }
                    active_formula.reset();
                    return;
                }

                ActiveSourceFormulaCell next_formula;
                next_formula.cell = worksheet_cell_reference_from_a1(event.cell_reference);
                next_formula.shared_formula = source_formula_tag_is_shared(event.raw_xml);
                next_formula.shared_formula_index =
                    source_formula_tag_shared_index(event.raw_xml);
                active_formula = std::move(next_formula);
                return;
            }

            if (event.kind == WorksheetEventKind::CellValue
                && active_formula.has_value()) {
                active_formula->raw_formula_text.append(event.text);
            }
        });

    return formula_cells;
}

const WorkbookSheetReference& find_source_sheet_reference(
    const std::vector<WorkbookSheetReference>& sheets,
    std::string_view source_name)
{
    for (const WorkbookSheetReference& sheet : sheets) {
        if (sheet.name == source_name) {
            return sheet;
        }
    }
    throw FastXlsxError(
        "source worksheet formula audit could not resolve source sheet '" +
        std::string(source_name) + "'");
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

std::vector<WorkbookEditorFormulaReferenceAudit>
workbook_editor_source_formula_reference_audits(
    const std::vector<WorkbookEditorSheetCatalogEntry>& catalog,
    const PackageReader& reader)
{
    const std::vector<FormulaAuditSheetCatalogEntry> formula_catalog =
        formula_audit_catalog_from_sheet_catalog(catalog);
    const std::vector<WorkbookSheetReference> workbook_sheets = reader.workbook_sheets();
    std::vector<WorkbookEditorFormulaReferenceAudit> audits;

    for (const WorkbookEditorSheetCatalogEntry& formula_sheet : catalog) {
        const WorkbookSheetReference& source_sheet =
            find_source_sheet_reference(workbook_sheets, formula_sheet.source_name);
        std::vector<SourceWorksheetFormulaCell> formula_cells;
        try {
            formula_cells = scan_source_worksheet_formula_cells(reader, source_sheet);
        } catch (const std::exception& error) {
            throw FastXlsxError(
                "failed to scan source worksheet formula references for sheet '" +
                formula_sheet.source_name + "': " + error.what());
        }

        for (const SourceWorksheetFormulaCell& formula_cell : formula_cells) {
            const std::vector<FormulaReferenceAuditFields> formula_audits =
                audit_formula_references(formula_cell.formula_text, formula_catalog);
            for (const FormulaReferenceAuditFields& fields : formula_audits) {
                WorkbookEditorFormulaReferenceAudit audit;
                audit.formula_sheet_source_name = formula_sheet.source_name;
                audit.formula_sheet_planned_name = formula_sheet.planned_name;
                audit.formula_cell = formula_cell.cell;
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
