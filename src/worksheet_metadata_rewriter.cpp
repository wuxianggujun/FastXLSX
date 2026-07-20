#include <fastxlsx/detail/worksheet_metadata_rewriter.hpp>

#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr int auto_filter_schema_rank = 5;
constexpr int data_validations_schema_rank = 12;
constexpr int hyperlink_schema_rank = 13;

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool is_closing_tag(std::string_view raw_xml) noexcept
{
    return raw_xml.size() > 2 && raw_xml[0] == '<' && raw_xml[1] == '/';
}

std::optional<std::string_view> attribute_value(
    std::string_view raw_tag, std::string_view requested_name)
{
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw FastXlsxError("worksheet metadata contains an invalid XML tag");
    }

    std::size_t position = 1;
    if (position < raw_tag.size() && raw_tag[position] == '/') {
        ++position;
    }
    while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
        && raw_tag[position] != '/' && raw_tag[position] != '>') {
        ++position;
    }

    while (position < raw_tag.size()) {
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size() || raw_tag[position] == '/'
            || raw_tag[position] == '>') {
            return std::nullopt;
        }

        const std::size_t name_begin = position;
        while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
            && raw_tag[position] != '=' && raw_tag[position] != '/'
            && raw_tag[position] != '>') {
            ++position;
        }
        const std::string_view name = raw_tag.substr(name_begin, position - name_begin);
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size() || raw_tag[position] != '=') {
            throw FastXlsxError(
                "worksheet metadata contains an attribute without a value");
        }
        ++position;
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size()
            || (raw_tag[position] != '"' && raw_tag[position] != '\'')) {
            throw FastXlsxError(
                "worksheet metadata contains an unquoted attribute value");
        }

        const char quote = raw_tag[position++];
        const std::size_t value_begin = position;
        while (position < raw_tag.size() && raw_tag[position] != quote) {
            ++position;
        }
        if (position >= raw_tag.size()) {
            throw FastXlsxError(
                "worksheet metadata contains an unterminated attribute value");
        }
        const std::string_view value =
            raw_tag.substr(value_begin, position - value_begin);
        ++position;
        if (name == requested_name) {
            return value;
        }
    }

    return std::nullopt;
}

struct A1Coordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::optional<A1Coordinate> parse_a1_coordinate(std::string_view text)
{
    std::size_t position = 0;
    if (position < text.size() && text[position] == '$') {
        ++position;
    }

    std::uint32_t column = 0;
    const std::size_t column_begin = position;
    while (position < text.size()
        && std::isalpha(static_cast<unsigned char>(text[position])) != 0) {
        const char upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(text[position])));
        if (upper < 'A' || upper > 'Z'
            || column > (16384U - static_cast<std::uint32_t>(upper - 'A' + 1)) / 26U) {
            return std::nullopt;
        }
        column = column * 26U + static_cast<std::uint32_t>(upper - 'A' + 1);
        ++position;
    }
    if (position == column_begin || column == 0 || column > 16384U) {
        return std::nullopt;
    }
    if (position < text.size() && text[position] == '$') {
        ++position;
    }

    std::uint32_t row = 0;
    const std::size_t row_begin = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
        const std::uint32_t digit = static_cast<std::uint32_t>(text[position] - '0');
        if (row > (1048576U - digit) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + digit;
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0 || row > 1048576U) {
        return std::nullopt;
    }
    return A1Coordinate {row, column};
}

struct A1Range {
    A1Coordinate first;
    A1Coordinate last;
};

std::optional<A1Range> parse_a1_range(std::string_view reference)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1));
    if (!first.has_value() || !last.has_value()
        || first->row > last->row || first->column > last->column) {
        return std::nullopt;
    }
    return A1Range {*first, *last};
}

bool hyperlink_ref_contains_target(
    std::string_view reference, const A1Coordinate& target)
{
    const std::optional<A1Range> range = parse_a1_range(reference);
    if (!range.has_value()) {
        throw FastXlsxError("existing worksheet hyperlink ref is not a valid A1 range");
    }
    return target.row >= range->first.row && target.row <= range->last.row
        && target.column >= range->first.column
        && target.column <= range->last.column;
}

std::uint64_t event_end_offset(const WorksheetEvent& event)
{
    if (event.raw_xml.size()
        > std::numeric_limits<std::uint64_t>::max() - event.raw_xml_offset) {
        throw FastXlsxError("worksheet metadata event offset exceeds supported range");
    }
    return event.raw_xml_offset + static_cast<std::uint64_t>(event.raw_xml.size());
}

std::optional<int> worksheet_suffix_schema_rank(std::string_view element_name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetCalcPr", 1},
        {"sheetProtection", 2},
        {"protectedRanges", 3},
        {"scenarios", 4},
        {"autoFilter", 5},
        {"sortState", 6},
        {"dataConsolidate", 7},
        {"customSheetViews", 8},
        {"mergeCells", 9},
        {"phoneticPr", 10},
        {"conditionalFormatting", 11},
        {"dataValidations", 12},
        {"hyperlinks", hyperlink_schema_rank},
        {"printOptions", 14},
        {"pageMargins", 15},
        {"pageSetup", 16},
        {"headerFooter", 17},
        {"rowBreaks", 18},
        {"colBreaks", 19},
        {"customProperties", 20},
        {"cellWatches", 21},
        {"ignoredErrors", 22},
        {"smartTags", 23},
        {"drawing", 24},
        {"legacyDrawing", 25},
        {"legacyDrawingHF", 26},
        {"picture", 27},
        {"oleObjects", 28},
        {"controls", 29},
        {"webPublishItems", 30},
        {"tableParts", 31},
        {"extLst", 32},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [element_name](const auto& entry) { return entry.first == element_name; });
    if (found == std::end(ranks)) {
        return std::nullopt;
    }
    return found->second;
}

std::string internal_hyperlink_xml(const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" location=\"";
    append_escaped_xml_attribute(xml, hyperlink.location);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string external_hyperlink_xml(const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" r:id=\"";
    append_escaped_xml_attribute(xml, hyperlink.relationship_id);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string_view data_validation_type_name(DataValidationType type)
{
    switch (type) {
    case DataValidationType::Whole:
        return "whole";
    case DataValidationType::Decimal:
        return "decimal";
    case DataValidationType::List:
        return "list";
    case DataValidationType::Date:
        return "date";
    case DataValidationType::Time:
        return "time";
    case DataValidationType::TextLength:
        return "textLength";
    case DataValidationType::Custom:
        return "custom";
    }
    throw FastXlsxError("unknown data validation type");
}

std::string_view data_validation_operator_name(DataValidationOperator operator_type)
{
    switch (operator_type) {
    case DataValidationOperator::Between:
        return "between";
    case DataValidationOperator::NotBetween:
        return "notBetween";
    case DataValidationOperator::Equal:
        return "equal";
    case DataValidationOperator::NotEqual:
        return "notEqual";
    case DataValidationOperator::GreaterThan:
        return "greaterThan";
    case DataValidationOperator::LessThan:
        return "lessThan";
    case DataValidationOperator::GreaterThanOrEqual:
        return "greaterThanOrEqual";
    case DataValidationOperator::LessThanOrEqual:
        return "lessThanOrEqual";
    }
    throw FastXlsxError("unknown data validation operator");
}

std::string_view data_validation_error_style_name(DataValidationErrorStyle error_style)
{
    switch (error_style) {
    case DataValidationErrorStyle::Stop:
        return "stop";
    case DataValidationErrorStyle::Warning:
        return "warning";
    case DataValidationErrorStyle::Information:
        return "information";
    }
    throw FastXlsxError("unknown data validation error style");
}

bool data_validation_operator_requires_formula2(DataValidationOperator operator_type) noexcept
{
    return operator_type == DataValidationOperator::Between
        || operator_type == DataValidationOperator::NotBetween;
}

std::optional<std::uint64_t> parse_unsigned_decimal(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

std::string data_validations_opening_with_count(
    std::string_view raw_tag, std::uint64_t count, bool expand_self_closing)
{
    std::string tag(raw_tag);
    if (expand_self_closing) {
        std::size_t slash = tag.size() - 2;
        while (slash > 0 && is_xml_space(tag[slash])) {
            --slash;
        }
        if (tag[slash] != '/') {
            throw FastXlsxError(
                "worksheet data validation rewrite expected a self-closing container");
        }
        tag.erase(slash, 1);
    }

    const std::optional<std::string_view> current = attribute_value(tag, "count");
    std::string count_text;
    append_unsigned_decimal(count_text, count);
    if (current.has_value()) {
        const std::size_t value_offset = static_cast<std::size_t>(
            current->data() - tag.data());
        tag.replace(value_offset, current->size(), count_text);
    } else {
        if (tag.size() < 2 || tag.back() != '>') {
            throw FastXlsxError(
                "worksheet data validation container has an invalid opening tag");
        }
        tag.insert(tag.size() - 1, " count=\"" + count_text + "\"");
    }
    return tag;
}

std::string worksheet_root_with_relationship_namespace(std::string_view raw_tag)
{
    constexpr std::string_view relationship_namespace =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const std::optional<std::string_view> current = attribute_value(raw_tag, "xmlns:r");
    if (current.has_value()) {
        if (*current != relationship_namespace) {
            throw FastXlsxError(
                "worksheet r namespace is not the OpenXML relationships namespace");
        }
        return std::string(raw_tag);
    }
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>'
        || is_closing_tag(raw_tag)) {
        throw FastXlsxError(
            "worksheet external hyperlink edit requires a valid worksheet root tag");
    }

    std::string root(raw_tag);
    root.insert(root.size() - 1,
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    return root;
}

bool is_synthetic_self_closing_end(const WorksheetEvent& event) noexcept
{
    if (!event.self_closing) {
        return false;
    }
    return event.kind == WorksheetEventKind::WorksheetEnd
        || event.kind == WorksheetEventKind::SheetDataEnd
        || event.kind == WorksheetEventKind::RowEnd
        || event.kind == WorksheetEventKind::CellEnd;
}

void write_bytes(std::ofstream& output, std::string_view bytes)
{
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw FastXlsxError("failed to write staged worksheet metadata");
    }
}

std::string expand_self_closing_tag(std::string_view raw_tag)
{
    std::string opening(raw_tag);
    std::size_t slash = opening.size() - 2;
    while (slash > 0 && is_xml_space(opening[slash])) {
        --slash;
    }
    if (opening[slash] != '/') {
        throw FastXlsxError(
            "worksheet hyperlink rewrite expected a self-closing hyperlinks tag");
    }
    opening.erase(slash, 1);
    return opening;
}

} // namespace

std::string serialize_worksheet_auto_filter(CellRange range)
{
    std::string xml = "<autoFilter ref=\"";
    xml += range_reference(range);
    xml += "\"/>";
    return xml;
}

void validate_data_validation_rule(const DataValidationRule& rule)
{
    if (rule.formula1.empty()) {
        throw FastXlsxError("data validation formula1 cannot be empty");
    }

    if (rule.hide_dropdown_arrow && rule.type != DataValidationType::List) {
        throw FastXlsxError("hide_dropdown_arrow is only valid for list data validations");
    }

    if (rule.type == DataValidationType::List || rule.type == DataValidationType::Custom) {
        if (rule.operator_type.has_value()) {
            throw FastXlsxError("list and custom data validations do not accept an operator");
        }
        if (!rule.formula2.empty()) {
            throw FastXlsxError("list and custom data validations do not accept formula2");
        }
        return;
    }

    if (!rule.operator_type.has_value()) {
        throw FastXlsxError("data validation operator is required for this type");
    }
    if (data_validation_operator_requires_formula2(*rule.operator_type)) {
        if (rule.formula2.empty()) {
            throw FastXlsxError("between data validations require formula2");
        }
    } else if (!rule.formula2.empty()) {
        throw FastXlsxError("single-formula data validation operator cannot use formula2");
    }
}

std::string serialize_data_validation(
    std::span<const CellRange> ranges, const DataValidationRule& rule)
{
    validate_data_validation_rule(rule);
    const std::string range_text = sqref(ranges);

    std::string xml = "<dataValidation type=\"";
    xml += data_validation_type_name(rule.type);
    xml += '"';
    if (rule.allow_blank) {
        xml += " allowBlank=\"1\"";
    }
    if (rule.hide_dropdown_arrow) {
        xml += " showDropDown=\"1\"";
    }
    if (rule.show_input_message) {
        xml += " showInputMessage=\"1\"";
    }
    if (rule.show_error_message) {
        xml += " showErrorMessage=\"1\"";
    }
    if (rule.error_style.has_value()) {
        xml += " errorStyle=\"";
        xml += data_validation_error_style_name(*rule.error_style);
        xml += '"';
    }
    if (!rule.error_title.empty()) {
        xml += " errorTitle=\"";
        append_escaped_xml_attribute(xml, rule.error_title);
        xml += '"';
    }
    if (!rule.error.empty()) {
        xml += " error=\"";
        append_escaped_xml_attribute(xml, rule.error);
        xml += '"';
    }
    if (!rule.prompt_title.empty()) {
        xml += " promptTitle=\"";
        append_escaped_xml_attribute(xml, rule.prompt_title);
        xml += '"';
    }
    if (!rule.prompt.empty()) {
        xml += " prompt=\"";
        append_escaped_xml_attribute(xml, rule.prompt);
        xml += '"';
    }
    if (rule.operator_type.has_value()) {
        xml += " operator=\"";
        xml += data_validation_operator_name(*rule.operator_type);
        xml += '"';
    }
    xml += " sqref=\"";
    append_escaped_xml_attribute(xml, range_text);
    xml += "\"><formula1>";
    append_escaped_xml_text(xml, rule.formula1);
    xml += "</formula1>";
    if (!rule.formula2.empty()) {
        xml += "<formula2>";
        append_escaped_xml_text(xml, rule.formula2);
        xml += "</formula2>";
    }
    xml += "</dataValidation>";
    return xml;
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view cell_reference)
{
    const std::optional<A1Coordinate> target = parse_a1_coordinate(cell_reference);
    if (!target.has_value()) {
        throw FastXlsxError("hyperlink cell reference is invalid");
    }

    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_hyperlinks = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<std::uint64_t> first_after_hyperlinks_offset;
    std::optional<WorksheetInternalHyperlinkRewritePlan> existing_container_plan;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet hyperlink metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "hyperlinks") {
                    if (!saw_hyperlinks || existing_container_plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous hyperlinks metadata");
                    }
                    existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                        WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose,
                        event.raw_xml_offset,
                    };
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_hyperlink_child = metadata_stack.size() == 1
                && metadata_stack.front() == "hyperlinks";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to hyperlinks is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > hyperlink_schema_rank
                    && !first_after_hyperlinks_offset.has_value()) {
                    first_after_hyperlinks_offset = event.raw_xml_offset;
                }
                if (event.element_name == "hyperlinks") {
                    if (saw_hyperlinks) {
                        throw FastXlsxError("worksheet contains duplicate hyperlinks containers");
                    }
                    saw_hyperlinks = true;
                    if (event.self_closing) {
                        existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                            WorksheetInternalHyperlinkRewriteAction::
                                ExpandSelfClosingContainer,
                            event.raw_xml_offset,
                        };
                    }
                }
            } else if (top_level && event.element_name == "hyperlinks") {
                throw FastXlsxError(
                    "worksheet hyperlinks metadata appears before sheetData");
            }

            if (direct_hyperlink_child) {
                if (event.element_name != "hyperlink") {
                    throw FastXlsxError(
                        "worksheet hyperlinks container has an unsupported child element");
                }
                const std::optional<std::string_view> reference =
                    attribute_value(event.raw_xml, "ref");
                if (!reference.has_value() || reference->empty()) {
                    throw FastXlsxError("existing worksheet hyperlink is missing its ref");
                }
                if (hyperlink_ref_contains_target(*reference, *target)) {
                    throw FastXlsxError(
                        "worksheet already has a hyperlink covering the requested cell");
                }
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet hyperlink metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "worksheet hyperlink edit requires sheetData and a closing worksheet root");
    }
    if (saw_hyperlinks) {
        if (!existing_container_plan.has_value()) {
            throw FastXlsxError("worksheet hyperlinks container has no closing boundary");
        }
        return *existing_container_plan;
    }
    return WorksheetInternalHyperlinkRewritePlan {
        WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore,
        first_after_hyperlinks_offset.value_or(worksheet_end_offset),
    };
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.location.empty()) {
        throw FastXlsxError("internal hyperlink location cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.target.empty()) {
        throw FastXlsxError("external hyperlink target cannot be empty");
    }
    if (hyperlink.relationship_id.empty()) {
        throw FastXlsxError("external hyperlink relationship id cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

WorksheetDataValidationRewritePlan plan_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk)
{
    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_data_validations = false;
    std::size_t child_count = 0;
    std::optional<std::uint64_t> declared_count;
    std::optional<std::uint64_t> first_after_data_validations_offset;
    std::uint64_t worksheet_end_offset = 0;
    std::uint64_t container_start_offset = 0;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<WorksheetDataValidationRewritePlan> plan;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet data validation metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "dataValidations") {
                    if (!saw_data_validations || plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous data validation metadata");
                    }
                    if (declared_count.has_value()
                        && *declared_count != static_cast<std::uint64_t>(child_count)) {
                        throw FastXlsxError(
                            "worksheet data validation count does not match its direct children");
                    }
                    plan = WorksheetDataValidationRewritePlan {
                        WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose,
                        event.raw_xml_offset,
                        container_start_offset,
                        static_cast<std::uint64_t>(child_count) + 1U};
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_data_validation_child = metadata_stack.size() == 1
                && metadata_stack.front() == "dataValidations";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to data validations is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > data_validations_schema_rank
                    && !first_after_data_validations_offset.has_value()) {
                    first_after_data_validations_offset = event.raw_xml_offset;
                }
                if (event.element_name == "dataValidations") {
                    if (saw_data_validations) {
                        throw FastXlsxError(
                            "worksheet contains duplicate data validation containers");
                    }
                    saw_data_validations = true;
                    container_start_offset = event.raw_xml_offset;
                    if (const std::optional<std::string_view> count =
                            attribute_value(event.raw_xml, "count")) {
                        declared_count = parse_unsigned_decimal(*count);
                        if (!declared_count.has_value()) {
                            throw FastXlsxError(
                                "worksheet data validation count is not an unsigned integer");
                        }
                    }
                    if (event.self_closing) {
                        if (declared_count.has_value() && *declared_count != 0) {
                            throw FastXlsxError(
                                "self-closing data validation container must have count zero");
                        }
                        plan = WorksheetDataValidationRewritePlan {
                            WorksheetDataValidationRewritePlan::Action::ExpandSelfClosingContainer,
                            event.raw_xml_offset,
                            event.raw_xml_offset,
                            1};
                    }
                }
            } else if (top_level && event.element_name == "dataValidations") {
                throw FastXlsxError(
                    "worksheet data validation metadata appears before sheetData");
            }

            if (direct_data_validation_child) {
                if (event.element_name != "dataValidation") {
                    throw FastXlsxError(
                        "data validation container has an unsupported child element");
                }
                ++child_count;
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet data validation metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "data validation edit requires sheetData and a closing worksheet root");
    }
    if (saw_data_validations) {
        if (!plan.has_value()) {
            throw FastXlsxError(
                "worksheet data validation container has no closing boundary");
        }
        return *plan;
    }
    return WorksheetDataValidationRewritePlan {
        WorksheetDataValidationRewritePlan::Action::InsertContainerBefore,
        first_after_data_validations_offset.value_or(worksheet_end_offset),
        0,
        1};
}

WorksheetAutoFilterRewritePlan plan_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk)
{
    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_auto_filter = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<std::uint64_t> first_after_auto_filter_offset;
    std::optional<WorksheetAutoFilterRewritePlan> existing_plan;
    std::uint64_t auto_filter_start_offset = 0;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet auto-filter metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "autoFilter") {
                    if (!saw_auto_filter || existing_plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous auto-filter metadata");
                    }
                    existing_plan = WorksheetAutoFilterRewritePlan {
                        true, auto_filter_start_offset, event_end_offset(event)};
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to autoFilter is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > auto_filter_schema_rank
                    && !first_after_auto_filter_offset.has_value()) {
                    first_after_auto_filter_offset = event.raw_xml_offset;
                }
                if (event.element_name == "autoFilter") {
                    if (saw_auto_filter) {
                        throw FastXlsxError(
                            "worksheet contains duplicate autoFilter elements");
                    }
                    saw_auto_filter = true;
                    const std::optional<std::string_view> reference =
                        attribute_value(event.raw_xml, "ref");
                    if (!reference.has_value() || reference->empty()) {
                        throw FastXlsxError(
                            "existing worksheet autoFilter is missing its ref");
                    }
                    if (!parse_a1_range(*reference).has_value()) {
                        throw FastXlsxError(
                            "existing worksheet autoFilter ref is not a valid A1 range");
                    }
                    auto_filter_start_offset = event.raw_xml_offset;
                    if (event.self_closing) {
                        existing_plan = WorksheetAutoFilterRewritePlan {
                            true, event.raw_xml_offset, event_end_offset(event)};
                    }
                }
            } else if (top_level && event.element_name == "autoFilter") {
                throw FastXlsxError(
                    "worksheet autoFilter metadata appears before sheetData");
            } else if (!top_level && event.element_name == "autoFilter") {
                throw FastXlsxError(
                    "worksheet autoFilter metadata is nested below another element");
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet auto-filter metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "auto-filter edit requires sheetData and a closing worksheet root");
    }
    if (saw_auto_filter) {
        if (!existing_plan.has_value()) {
            throw FastXlsxError(
                "worksheet autoFilter element has no closing boundary");
        }
        return *existing_plan;
    }
    return WorksheetAutoFilterRewritePlan {
        false,
        first_after_auto_filter_offset.value_or(worksheet_end_offset),
        first_after_auto_filter_offset.value_or(worksheet_end_offset)};
}

void write_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view hyperlink_xml,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path,
    bool ensure_relationship_namespace)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet hyperlink metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }
            if (!applied && event.raw_xml_offset == plan.source_offset) {
                switch (plan.action) {
                case WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore:
                    write_bytes(output, "<hyperlinks>");
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    break;
                case WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose:
                    write_bytes(output, hyperlink_xml);
                    break;
                case WorksheetInternalHyperlinkRewriteAction::ExpandSelfClosingContainer:
                    write_bytes(output, expand_self_closing_tag(event.raw_xml));
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    applied = true;
                    return;
                }
                applied = true;
            }
            if (ensure_relationship_namespace
                && event.kind == WorksheetEventKind::WorksheetStart) {
                write_bytes(output,
                    worksheet_root_with_relationship_namespace(event.raw_xml));
            } else {
                write_bytes(output, event.raw_xml);
            }
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet hyperlink rewrite did not reach its planned insertion boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError("failed to finalize staged worksheet hyperlink metadata file");
    }
}

void write_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view data_validation_xml,
    const WorksheetDataValidationRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet data validation metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            if (plan.action == WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose
                && event.raw_xml_offset == plan.container_start_offset
                && event.kind == WorksheetEventKind::Metadata
                && !is_closing_tag(event.raw_xml)) {
                write_bytes(output, data_validations_opening_with_count(
                    event.raw_xml, plan.new_count, false));
                return;
            }

            if (applied || event.raw_xml_offset != plan.source_offset) {
                write_bytes(output, event.raw_xml);
                return;
            }

            switch (plan.action) {
            case WorksheetDataValidationRewritePlan::Action::InsertContainerBefore:
                write_bytes(output, "<dataValidations count=\"1\">");
                write_bytes(output, data_validation_xml);
                write_bytes(output, "</dataValidations>");
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetDataValidationRewritePlan::Action::AppendBeforeContainerClose:
                write_bytes(output, data_validation_xml);
                write_bytes(output, event.raw_xml);
                break;
            case WorksheetDataValidationRewritePlan::Action::ExpandSelfClosingContainer:
                write_bytes(output, data_validations_opening_with_count(
                    event.raw_xml, plan.new_count, true));
                write_bytes(output, data_validation_xml);
                write_bytes(output, "</dataValidations>");
                break;
            }
            applied = true;
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet data validation rewrite did not reach its planned insertion boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet data validation metadata file");
    }
}

void write_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view auto_filter_xml,
    const WorksheetAutoFilterRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet auto-filter metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }

            if (!applied && event.raw_xml_offset == plan.source_offset) {
                write_bytes(output, auto_filter_xml);
                applied = true;
            }
            if (plan.has_existing_auto_filter
                && event.raw_xml_offset >= plan.source_offset
                && event.raw_xml_offset < plan.source_end_offset) {
                return;
            }
            write_bytes(output, event.raw_xml);
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet auto-filter rewrite did not reach its planned boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError(
            "failed to finalize staged worksheet auto-filter metadata file");
    }
}

void write_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        internal_hyperlink_xml(hyperlink), plan, output_path, false);
}

void write_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        external_hyperlink_xml(hyperlink), plan, output_path, true);
}

} // namespace fastxlsx::detail
