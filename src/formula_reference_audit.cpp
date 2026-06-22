#include <fastxlsx/detail/formula_reference_audit.hpp>

#include <fastxlsx/detail/formula.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

namespace {

struct FormulaSheetCatalogMatch {
    FormulaAuditSheetCatalogEntry entry;
    bool source_name_matched = false;
    bool planned_name_matched = false;
};

struct XmlTagRange {
    std::size_t open = 0;
    std::size_t close = 0;
};

char ascii_lower(char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

bool ascii_equals_ignoring_case(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (ascii_lower(lhs[index]) != ascii_lower(rhs[index])) {
            return false;
        }
    }
    return true;
}

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool is_xml_name_stop(char ch) noexcept
{
    return is_xml_space(ch) || ch == '/' || ch == '>' || ch == '=';
}

std::string_view xml_local_name(std::string_view name) noexcept
{
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return name;
    }
    return name.substr(colon + 1);
}

std::size_t find_xml_tag_end(std::string_view xml, std::size_t open_offset)
{
    char quote = '\0';
    for (std::size_t offset = open_offset + 1; offset < xml.size(); ++offset) {
        const char ch = xml[offset];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '>') {
            return offset;
        }
    }

    throw FastXlsxError("workbook definedName formula audit found an unclosed XML tag");
}

std::string_view start_tag_name(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 1;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    const std::size_t begin = offset;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    if (begin == offset) {
        throw FastXlsxError("workbook definedName formula audit found a nameless XML tag");
    }
    return xml.substr(begin, offset - begin);
}

std::string_view closing_tag_name(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 2;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    const std::size_t begin = offset;
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    if (begin == offset) {
        throw FastXlsxError("workbook definedName formula audit found a nameless XML closing tag");
    }
    return xml.substr(begin, offset - begin);
}

bool is_self_closing_tag(std::string_view xml, const XmlTagRange& tag) noexcept
{
    std::size_t offset = tag.close;
    while (offset > tag.open + 1) {
        --offset;
        if (!is_xml_space(xml[offset])) {
            return xml[offset] == '/';
        }
    }
    return false;
}

std::string unescape_xml_text(std::string_view value)
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
                "workbook definedName formula audit found an unterminated XML entity");
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

std::size_t start_tag_attribute_offset(std::string_view xml, const XmlTagRange& tag)
{
    std::size_t offset = tag.open + 1;
    while (offset < tag.close && is_xml_space(xml[offset])) {
        ++offset;
    }
    while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
        ++offset;
    }
    return offset;
}

std::optional<std::string> find_unqualified_xml_attribute(
    std::string_view xml,
    const XmlTagRange& tag,
    std::string_view attribute_name)
{
    std::size_t offset = start_tag_attribute_offset(xml, tag);
    while (offset < tag.close) {
        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset == tag.close || xml[offset] == '/') {
            break;
        }

        const std::size_t name_begin = offset;
        while (offset < tag.close && !is_xml_name_stop(xml[offset])) {
            ++offset;
        }
        if (name_begin == offset) {
            throw FastXlsxError(
                "workbook definedName formula audit found a nameless XML attribute");
        }
        const std::string_view name = xml.substr(name_begin, offset - name_begin);

        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset == tag.close || xml[offset] != '=') {
            throw FastXlsxError(
                "workbook definedName formula audit found an XML attribute without '='");
        }
        ++offset;
        while (offset < tag.close && is_xml_space(xml[offset])) {
            ++offset;
        }
        if (offset == tag.close || (xml[offset] != '"' && xml[offset] != '\'')) {
            throw FastXlsxError(
                "workbook definedName formula audit found an unquoted XML attribute");
        }

        const char quote = xml[offset++];
        const std::size_t value_begin = offset;
        while (offset < tag.close && xml[offset] != quote) {
            ++offset;
        }
        if (offset == tag.close) {
            throw FastXlsxError(
                "workbook definedName formula audit found an unclosed XML attribute");
        }
        const std::string_view raw_value = xml.substr(value_begin, offset - value_begin);
        ++offset;

        if (name == attribute_name) {
            return unescape_xml_text(raw_value);
        }
    }

    return std::nullopt;
}

std::optional<std::size_t> parse_zero_based_index(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }

    std::size_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        value = value * 10U + digit;
    }
    return value;
}

std::string decoded_formula_sheet_name(
    std::string_view formula, const FormulaSheetQualifier& qualifier)
{
    std::string decoded;
    const std::string_view raw_name =
        formula.substr(qualifier.name_offset, qualifier.name_length);
    decoded.reserve(raw_name.size());

    for (std::size_t index = 0; index < raw_name.size(); ++index) {
        if (qualifier.quoted && raw_name[index] == '\'' && index + 1 < raw_name.size()
            && raw_name[index + 1] == '\'') {
            decoded += '\'';
            ++index;
            continue;
        }
        decoded += raw_name[index];
    }

    return decoded;
}

bool formula_sheet_qualifier_has_external_workbook(std::string_view decoded_sheet_name)
{
    return decoded_sheet_name.size() >= 3 && decoded_sheet_name.front() == '['
        && decoded_sheet_name.find(']') != std::string_view::npos;
}

bool formula_sheet_qualifier_has_sheet_range(std::string_view decoded_sheet_name)
{
    std::size_t search_offset = 0;
    if (!decoded_sheet_name.empty() && decoded_sheet_name.front() == '[') {
        const std::size_t workbook_end = decoded_sheet_name.find(']');
        if (workbook_end != std::string_view::npos) {
            search_offset = workbook_end + 1;
        }
    }
    return decoded_sheet_name.find(':', search_offset) != std::string_view::npos;
}

std::optional<FormulaSheetCatalogMatch> match_formula_sheet_catalog_entry(
    std::span<const FormulaAuditSheetCatalogEntry> catalog,
    std::string_view referenced_sheet_name)
{
    std::optional<FormulaSheetCatalogMatch> match;
    for (const FormulaAuditSheetCatalogEntry& entry : catalog) {
        const bool source_name_matched =
            ascii_equals_ignoring_case(entry.source_name, referenced_sheet_name);
        const bool planned_name_matched =
            ascii_equals_ignoring_case(entry.planned_name, referenced_sheet_name);
        if (!source_name_matched && !planned_name_matched) {
            continue;
        }

        if (match.has_value()
            && (match->entry.source_name != entry.source_name
                || match->entry.planned_name != entry.planned_name)) {
            return std::nullopt;
        }

        if (!match.has_value()) {
            match = FormulaSheetCatalogMatch {entry, source_name_matched, planned_name_matched};
        } else {
            match->source_name_matched = match->source_name_matched || source_name_matched;
            match->planned_name_matched = match->planned_name_matched || planned_name_matched;
        }
    }
    return match;
}

FormulaReferenceAuditFields build_formula_reference_audit_fields(
    std::string_view formula_text,
    const FormulaReference& reference,
    std::span<const FormulaAuditSheetCatalogEntry> catalog)
{
    FormulaReferenceAuditFields audit;
    audit.formula_text = std::string(formula_text);
    audit.sheet_qualifier_text = std::string(formula_text.substr(
        reference.sheet.offset, reference.sheet.length));
    audit.reference_text = std::string(formula_text.substr(reference.offset, reference.length));
    audit.qualified_reference_text = std::string(formula_text.substr(
        reference.sheet.offset,
        reference.offset + reference.length - reference.sheet.offset));
    audit.referenced_sheet_name = decoded_formula_sheet_name(formula_text, reference.sheet);
    audit.qualifier_quoted = reference.sheet.quoted;
    audit.external_workbook_qualifier =
        formula_sheet_qualifier_has_external_workbook(audit.referenced_sheet_name);
    audit.sheet_range_qualifier =
        formula_sheet_qualifier_has_sheet_range(audit.referenced_sheet_name);

    if (audit.external_workbook_qualifier || audit.sheet_range_qualifier) {
        return audit;
    }

    const std::optional<FormulaSheetCatalogMatch> match =
        match_formula_sheet_catalog_entry(catalog, audit.referenced_sheet_name);
    if (match.has_value()) {
        audit.matched_current_workbook_sheet = true;
        audit.matched_source_sheet_name = match->entry.source_name;
        audit.matched_planned_sheet_name = match->entry.planned_name;
        audit.references_planned_sheet_name = match->planned_name_matched;
        audit.references_renamed_source_name = match->source_name_matched
            && match->entry.source_name != match->entry.planned_name;
    }
    return audit;
}

std::string quoted_formula_sheet_qualifier(std::string_view sheet_name)
{
    if (sheet_name.empty()) {
        throw FastXlsxError("formula sheet rewrite replacement sheet name is empty");
    }

    std::string qualifier;
    qualifier.reserve(sheet_name.size() + 3);
    qualifier.push_back('\'');
    for (const char ch : sheet_name) {
        if (ch == '\'') {
            qualifier += "''";
        } else {
            qualifier.push_back(ch);
        }
    }
    qualifier += "'!";
    return qualifier;
}

std::optional<std::string> replacement_formula_sheet_name(
    std::span<const FormulaSheetReferenceRewrite> rewrites,
    std::string_view referenced_sheet_name)
{
    std::optional<std::string> replacement;
    for (const FormulaSheetReferenceRewrite& rewrite : rewrites) {
        if (!ascii_equals_ignoring_case(rewrite.source_sheet_name, referenced_sheet_name)) {
            continue;
        }
        if (replacement.has_value() && *replacement != rewrite.replacement_sheet_name) {
            throw FastXlsxError(
                "formula sheet rewrite has ambiguous replacement sheet names");
        }
        replacement = rewrite.replacement_sheet_name;
    }
    return replacement;
}

} // namespace

std::vector<FormulaReferenceAuditFields> audit_formula_references(
    std::string_view formula_text,
    std::span<const FormulaAuditSheetCatalogEntry> catalog)
{
    std::vector<FormulaReferenceAuditFields> audits;
    const std::vector<FormulaReference> references = scan_formula_references(formula_text);
    for (const FormulaReference& reference : references) {
        if (!reference.sheet.present) {
            continue;
        }

        audits.push_back(build_formula_reference_audit_fields(formula_text, reference, catalog));
    }
    return audits;
}

std::string rewrite_formula_sheet_references(
    std::string_view formula_text,
    std::span<const FormulaSheetReferenceRewrite> rewrites)
{
    if (rewrites.empty() || formula_text.empty()) {
        return std::string(formula_text);
    }

    std::string rewritten;
    rewritten.reserve(formula_text.size());
    std::size_t cursor = 0;
    bool changed = false;

    const std::vector<FormulaReference> references = scan_formula_references(formula_text);
    for (const FormulaReference& reference : references) {
        if (!reference.sheet.present || reference.sheet.offset < cursor) {
            continue;
        }

        const std::string referenced_sheet_name =
            decoded_formula_sheet_name(formula_text, reference.sheet);
        if (formula_sheet_qualifier_has_external_workbook(referenced_sheet_name)
            || formula_sheet_qualifier_has_sheet_range(referenced_sheet_name)) {
            continue;
        }

        const std::optional<std::string> replacement =
            replacement_formula_sheet_name(rewrites, referenced_sheet_name);
        if (!replacement.has_value()) {
            continue;
        }

        rewritten.append(formula_text.substr(cursor, reference.sheet.offset - cursor));
        rewritten += quoted_formula_sheet_qualifier(*replacement);
        cursor = reference.sheet.offset + reference.sheet.length;
        changed = true;
    }

    if (!changed) {
        return std::string(formula_text);
    }

    rewritten.append(formula_text.substr(cursor));
    return rewritten;
}

std::vector<SourceDefinedNameFormula> scan_workbook_defined_name_formulas(
    std::string_view workbook_xml,
    std::span<const FormulaAuditSheetCatalogEntry> catalog)
{
    std::vector<SourceDefinedNameFormula> defined_names;
    std::vector<std::string> element_stack;
    bool inside_workbook = false;
    bool inside_defined_names = false;
    std::size_t defined_names_child_depth = 0;

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("workbook definedName formula audit found a truncated XML tag");
        }
        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError(
                    "workbook definedName formula audit found an unclosed XML comment");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const char marker = workbook_xml[open + 1];
        const XmlTagRange tag {open, close};

        if (marker == '/') {
            const std::string_view closing_name =
                xml_local_name(closing_tag_name(workbook_xml, tag));
            if (element_stack.empty()) {
                throw FastXlsxError(
                    "workbook definedName formula audit found an unmatched XML closing tag");
            }
            if (element_stack.back() != closing_name) {
                throw FastXlsxError(
                    "workbook definedName formula audit found mismatched XML tags");
            }
            if (inside_defined_names) {
                if (defined_names_child_depth == 0 && closing_name == "definedNames") {
                    inside_defined_names = false;
                } else if (defined_names_child_depth > 0) {
                    --defined_names_child_depth;
                }
            }
            if (element_stack.size() == 1 && closing_name == "workbook") {
                inside_workbook = false;
            }
            element_stack.pop_back();
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        const std::string_view local_name =
            xml_local_name(start_tag_name(workbook_xml, tag));
        const bool self_closing = is_self_closing_tag(workbook_xml, tag);
        const std::size_t element_depth = element_stack.size();

        if (!inside_defined_names) {
            if (inside_workbook && element_depth == 1 && local_name == "definedNames") {
                inside_defined_names = !self_closing;
                defined_names_child_depth = 0;
            }
        } else {
            if (defined_names_child_depth == 0 && local_name == "definedName" && !self_closing) {
                SourceDefinedNameFormula defined_name;
                if (const std::optional<std::string> name =
                        find_unqualified_xml_attribute(workbook_xml, tag, "name");
                    name.has_value()) {
                    defined_name.name = *name;
                }
                if (const std::optional<std::string> local_sheet_id =
                        find_unqualified_xml_attribute(workbook_xml, tag, "localSheetId");
                    local_sheet_id.has_value()) {
                    defined_name.local_sheet_scope = true;
                    defined_name.local_sheet_id_text = *local_sheet_id;
                    const std::optional<std::size_t> sheet_index =
                        parse_zero_based_index(*local_sheet_id);
                    if (sheet_index.has_value() && *sheet_index < catalog.size()) {
                        defined_name.local_sheet_scope_resolved = true;
                        defined_name.scope_sheet_source_name = catalog[*sheet_index].source_name;
                        defined_name.scope_sheet_planned_name = catalog[*sheet_index].planned_name;
                    }
                }

                const std::size_t closing_open = workbook_xml.find("</", close + 1);
                if (closing_open == std::string_view::npos) {
                    throw FastXlsxError(
                        "workbook definedName formula audit found an unclosed definedName");
                }
                const std::size_t closing_close =
                    find_xml_tag_end(workbook_xml, closing_open);
                const XmlTagRange closing_tag {closing_open, closing_close};
                if (xml_local_name(closing_tag_name(workbook_xml, closing_tag))
                    != "definedName") {
                    throw FastXlsxError(
                        "workbook definedName formula audit found nested XML in definedName text");
                }
                defined_name.formula_text = unescape_xml_text(
                    workbook_xml.substr(close + 1, closing_open - close - 1));
                defined_names.push_back(std::move(defined_name));
            }
            if (!self_closing) {
                ++defined_names_child_depth;
            }
        }

        if (!self_closing) {
            if (element_depth == 0 && local_name == "workbook") {
                inside_workbook = true;
            }
            element_stack.emplace_back(local_name);
        }
        offset = close + 1;
    }

    if (!element_stack.empty()) {
        throw FastXlsxError("workbook definedName formula audit found unclosed XML tags");
    }

    return defined_names;
}

std::vector<DefinedNameFormulaReferenceAudit> audit_workbook_defined_name_formula_references(
    std::string_view workbook_xml,
    std::span<const FormulaAuditSheetCatalogEntry> catalog)
{
    std::vector<DefinedNameFormulaReferenceAudit> audits;
    const std::vector<SourceDefinedNameFormula> defined_names =
        scan_workbook_defined_name_formulas(workbook_xml, catalog);
    for (const SourceDefinedNameFormula& defined_name : defined_names) {
        const std::vector<FormulaReferenceAuditFields> references =
            audit_formula_references(defined_name.formula_text, catalog);
        for (const FormulaReferenceAuditFields& reference : references) {
            audits.push_back(DefinedNameFormulaReferenceAudit {
                defined_name,
                reference,
            });
        }
    }
    return audits;
}

std::string rewrite_workbook_defined_name_formula_references(
    std::string_view workbook_xml,
    std::span<const FormulaSheetReferenceRewrite> rewrites)
{
    if (rewrites.empty() || workbook_xml.empty()) {
        return std::string(workbook_xml);
    }

    std::string rewritten_workbook;
    rewritten_workbook.reserve(workbook_xml.size());
    std::size_t rewrite_cursor = 0;
    bool changed = false;

    std::vector<std::string> element_stack;
    bool inside_workbook = false;
    bool inside_defined_names = false;
    std::size_t defined_names_child_depth = 0;

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("workbook definedName formula rewrite found a truncated XML tag");
        }
        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError(
                    "workbook definedName formula rewrite found an unclosed XML comment");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const char marker = workbook_xml[open + 1];
        const XmlTagRange tag {open, close};

        if (marker == '/') {
            const std::string_view closing_name =
                xml_local_name(closing_tag_name(workbook_xml, tag));
            if (element_stack.empty()) {
                throw FastXlsxError(
                    "workbook definedName formula rewrite found an unmatched XML closing tag");
            }
            if (element_stack.back() != closing_name) {
                throw FastXlsxError(
                    "workbook definedName formula rewrite found mismatched XML tags");
            }
            if (inside_defined_names) {
                if (defined_names_child_depth == 0 && closing_name == "definedNames") {
                    inside_defined_names = false;
                } else if (defined_names_child_depth > 0) {
                    --defined_names_child_depth;
                }
            }
            if (element_stack.size() == 1 && closing_name == "workbook") {
                inside_workbook = false;
            }
            element_stack.pop_back();
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        const std::string_view local_name =
            xml_local_name(start_tag_name(workbook_xml, tag));
        const bool self_closing = is_self_closing_tag(workbook_xml, tag);
        const std::size_t element_depth = element_stack.size();

        if (!inside_defined_names) {
            if (inside_workbook && element_depth == 1 && local_name == "definedNames") {
                inside_defined_names = !self_closing;
                defined_names_child_depth = 0;
            }
        } else {
            if (defined_names_child_depth == 0 && local_name == "definedName" && !self_closing) {
                const std::size_t closing_open = workbook_xml.find("</", close + 1);
                if (closing_open == std::string_view::npos) {
                    throw FastXlsxError(
                        "workbook definedName formula rewrite found an unclosed definedName");
                }
                const std::size_t closing_close =
                    find_xml_tag_end(workbook_xml, closing_open);
                const XmlTagRange closing_tag {closing_open, closing_close};
                if (xml_local_name(closing_tag_name(workbook_xml, closing_tag))
                    != "definedName") {
                    throw FastXlsxError(
                        "workbook definedName formula rewrite found nested XML in definedName text");
                }

                const std::string formula_text = unescape_xml_text(
                    workbook_xml.substr(close + 1, closing_open - close - 1));
                const std::string rewritten_formula =
                    rewrite_formula_sheet_references(formula_text, rewrites);
                if (rewritten_formula != formula_text) {
                    rewritten_workbook.append(
                        workbook_xml.substr(rewrite_cursor, close + 1 - rewrite_cursor));
                    rewritten_workbook += escape_xml_text(rewritten_formula);
                    rewrite_cursor = closing_open;
                    changed = true;
                }
            }
            if (!self_closing) {
                ++defined_names_child_depth;
            }
        }

        if (!self_closing) {
            if (element_depth == 0 && local_name == "workbook") {
                inside_workbook = true;
            }
            element_stack.emplace_back(local_name);
        }
        offset = close + 1;
    }

    if (!element_stack.empty()) {
        throw FastXlsxError("workbook definedName formula rewrite found unclosed XML tags");
    }

    if (!changed) {
        return std::string(workbook_xml);
    }

    rewritten_workbook.append(workbook_xml.substr(rewrite_cursor));
    return rewritten_workbook;
}

} // namespace fastxlsx::detail
