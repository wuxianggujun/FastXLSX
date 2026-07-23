#include "worksheet_data_validation_reader.hpp"

#include <fastxlsx/detail/worksheet_metadata_serializer.hpp>

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;

bool is_xml_space(char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\r'
        || character == '\n';
}

bool is_closing_tag(std::string_view raw_xml) noexcept
{
    return raw_xml.size() > 2 && raw_xml.front() == '<' && raw_xml[1] == '/';
}

bool is_ascii_name_boundary(char character) noexcept
{
    return is_xml_space(character) || character == '/' || character == '>'
        || character == '?';
}

struct ParsedTag {
    bool closing = false;
    bool self_closing = false;
    std::string_view qualified_name;
    std::string_view local_name;
    std::string_view prefix;
    std::vector<std::pair<std::string_view, std::string_view>> attributes;
};

ParsedTag parse_tag(std::string_view raw_xml)
{
    if (raw_xml.size() < 3 || raw_xml.front() != '<' || raw_xml.back() != '>') {
        throw FastXlsxError("worksheet data validation contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet data validation received unsupported declaration markup");
    }

    ParsedTag result;
    result.closing = is_closing_tag(raw_xml);
    std::size_t position = result.closing ? 2U : 1U;
    const std::size_t name_begin = position;
    while (position < raw_xml.size()
        && !is_ascii_name_boundary(raw_xml[position])) {
        ++position;
    }
    if (position == name_begin) {
        throw FastXlsxError(
            "worksheet data validation contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const std::size_t separator = result.qualified_name.find(':');
    if (separator != std::string_view::npos
        && (separator == 0 || separator + 1U == result.qualified_name.size()
            || result.qualified_name.find(':', separator + 1U) != std::string_view::npos)) {
        throw FastXlsxError(
            "worksheet data validation contains an invalid qualified element name");
    }
    if (separator == std::string_view::npos) {
        result.local_name = result.qualified_name;
    } else {
        result.local_name = result.qualified_name.substr(separator + 1U);
        result.prefix = result.qualified_name.substr(0, separator + 1U);
    }

    if (result.closing) {
        while (position < raw_xml.size() - 1U
            && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet data validation closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet data validation contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            ++position;
            if (position != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet data validation contains trailing XML tag bytes");
            }
            return result;
        }
        if (raw_xml[position] == '/') {
            ++position;
            while (position < raw_xml.size() - 1U
                && is_xml_space(raw_xml[position])) {
                ++position;
            }
            if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
                throw FastXlsxError(
                    "worksheet data validation contains an invalid self-closing tag tail");
            }
            result.self_closing = true;
            ++position;
            return result;
        }

        const std::size_t attribute_begin = position;
        while (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '=' && raw_xml[position] != '/'
            && raw_xml[position] != '>') {
            ++position;
        }
        if (position == attribute_begin) {
            throw FastXlsxError(
                "worksheet data validation contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet data validation contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet data validation contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet data validation contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet data validation attributes are not separated by whitespace");
        }
        for (const auto& [existing_name, existing_value] : result.attributes) {
            (void)existing_value;
            if (existing_name == attribute_name) {
                throw FastXlsxError(
                    "worksheet data validation contains a duplicate attribute");
            }
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError(
        "worksheet data validation contains an incomplete XML tag");
}

std::optional<std::string_view> attribute_value(
    const ParsedTag& tag, std::string_view requested_name)
{
    for (const auto& [name, value] : tag.attributes) {
        if (name == requested_name) {
            return value;
        }
    }
    return std::nullopt;
}

std::uint64_t parse_unsigned_decimal(
    std::string_view value, std::string_view label)
{
    if (value.empty()) {
        throw FastXlsxError(
            "worksheet data validation has an invalid " + std::string(label));
    }
    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw FastXlsxError(
                "worksheet data validation has an invalid " + std::string(label));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw FastXlsxError(
                "worksheet data validation has an invalid " + std::string(label));
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

bool parse_boolean(std::string_view value, std::string_view label)
{
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw FastXlsxError(
        "worksheet data validation has an invalid " + std::string(label));
}

bool is_valid_xml_code_point(std::uint32_t code_point) noexcept
{
    return code_point == 0x09U || code_point == 0x0AU || code_point == 0x0DU
        || (code_point >= 0x20U && code_point <= 0xD7FFU)
        || (code_point >= 0xE000U && code_point <= 0xFFFDU)
        || (code_point >= 0x10000U && code_point <= 0x10FFFFU);
}

void append_utf8(
    std::string& output, std::uint32_t code_point,
    std::size_t limit, std::string_view label)
{
    if (!is_valid_xml_code_point(code_point)) {
        throw FastXlsxError(
            "worksheet data validation contains an invalid XML character in "
            + std::string(label));
    }
    std::size_t byte_count = 1;
    if (code_point > 0x7FU) {
        byte_count = code_point <= 0x7FFU ? 2U
            : (code_point <= 0xFFFFU ? 3U : 4U);
    }
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet data validation " + std::string(label)
            + " exceeds its configured text limit");
    }
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

std::uint32_t parse_entity_code_point(
    std::string_view body, std::string_view label)
{
    if (body.size() < 2 || body.front() != '#') {
        throw FastXlsxError(
            "worksheet data validation contains an unknown XML entity in "
            + std::string(label));
    }
    std::size_t position = 1;
    int base = 10;
    if (position < body.size() && (body[position] == 'x' || body[position] == 'X')) {
        base = 16;
        ++position;
    }
    if (position == body.size()) {
        throw FastXlsxError(
            "worksheet data validation contains an empty XML character reference");
    }
    std::uint64_t value = 0;
    for (; position < body.size(); ++position) {
        const char character = body[position];
        std::uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<std::uint32_t>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<std::uint32_t>(character - 'a' + 10);
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<std::uint32_t>(character - 'A' + 10);
        } else {
            throw FastXlsxError(
                "worksheet data validation contains an invalid XML character reference");
        }
        if (digit >= static_cast<std::uint32_t>(base)
            || value > (0x10FFFFU - digit) / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "worksheet data validation XML character reference overflows");
        }
        value = value * static_cast<std::uint32_t>(base) + digit;
    }
    return static_cast<std::uint32_t>(value);
}

void append_decoded_xml(
    std::string& output,
    std::string_view raw,
    std::size_t limit,
    std::string_view label,
    bool normalize_literal_whitespace)
{
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] != '&') {
            const char character = normalize_literal_whitespace
                && is_xml_space(raw[position]) ? ' ' : raw[position];
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet data validation " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(character);
            ++position;
            continue;
        }

        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet data validation contains an unterminated XML entity in "
                + std::string(label));
        }
        const std::string_view body = raw.substr(position + 1U,
            semicolon - position - 1U);
        if (body == "amp") {
            append_utf8(output, '&', limit, label);
        } else if (body == "lt") {
            append_utf8(output, '<', limit, label);
        } else if (body == "gt") {
            append_utf8(output, '>', limit, label);
        } else if (body == "quot") {
            append_utf8(output, '"', limit, label);
        } else if (body == "apos") {
            append_utf8(output, '\'', limit, label);
        } else {
            append_utf8(output, parse_entity_code_point(body, label), limit, label);
        }
        position = semicolon + 1U;
    }
}

std::string decode_xml_value(
    std::string_view raw,
    std::size_t limit,
    std::string_view label,
    bool normalize_literal_whitespace)
{
    std::string decoded;
    decoded.reserve(std::min(raw.size(), limit));
    append_decoded_xml(decoded, raw, limit, label, normalize_literal_whitespace);
    return decoded;
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
    std::uint64_t column = 0;
    const std::size_t column_begin = position;
    while (position < text.size()) {
        char character = text[position];
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
        if (character < 'A' || character > 'Z') {
            break;
        }
        column = column * 26U + static_cast<std::uint32_t>(character - 'A' + 1);
        if (column > max_excel_columns) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == column_begin || column == 0) {
        return std::nullopt;
    }
    if (position < text.size() && text[position] == '$') {
        ++position;
    }
    std::uint64_t row = 0;
    const std::size_t row_begin = position;
    while (position < text.size() && text[position] >= '0'
        && text[position] <= '9') {
        row = row * 10U + static_cast<std::uint32_t>(text[position] - '0');
        if (row > max_excel_rows) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0) {
        return std::nullopt;
    }
    return A1Coordinate {static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column)};
}

struct A1Range {
    A1Coordinate first;
    A1Coordinate last;
};

std::optional<A1Range> parse_a1_range(std::string_view reference)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference
                                            : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1U));
    if (!first.has_value() || !last.has_value() || first->row > last->row
        || first->column > last->column) {
        return std::nullopt;
    }
    return A1Range {*first, *last};
}

CellRange to_cell_range(const A1Range& range) noexcept
{
    return CellRange {range.first.row, range.first.column,
        range.last.row, range.last.column};
}

std::vector<CellRange> parse_sqref(
    std::string_view text, std::size_t max_ranges)
{
    std::vector<CellRange> ranges;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && is_xml_space(text[position])) {
            ++position;
        }
        if (position == text.size()) {
            break;
        }
        const std::size_t begin = position;
        while (position < text.size() && !is_xml_space(text[position])) {
            ++position;
        }
        const std::string_view token = text.substr(begin, position - begin);
        const std::optional<A1Range> range = parse_a1_range(token);
        if (!range.has_value()) {
            throw FastXlsxError(
                "worksheet data validation sqref contains an invalid A1 range");
        }
        if (ranges.size() >= max_ranges) {
            throw FastXlsxError(
                "worksheet data validation exceeds max_ranges_per_validation");
        }
        ranges.push_back(to_cell_range(*range));
    }
    if (ranges.empty()) {
        throw FastXlsxError(
            "worksheet data validation sqref cannot be empty");
    }
    return ranges;
}

DataValidationType parse_validation_type(std::string_view value)
{
    if (value == "whole") {
        return DataValidationType::Whole;
    }
    if (value == "decimal") {
        return DataValidationType::Decimal;
    }
    if (value == "list") {
        return DataValidationType::List;
    }
    if (value == "date") {
        return DataValidationType::Date;
    }
    if (value == "time") {
        return DataValidationType::Time;
    }
    if (value == "textLength") {
        return DataValidationType::TextLength;
    }
    if (value == "custom") {
        return DataValidationType::Custom;
    }
    throw FastXlsxError(
        "worksheet data validation has an unsupported type");
}

DataValidationOperator parse_validation_operator(std::string_view value)
{
    if (value == "between") {
        return DataValidationOperator::Between;
    }
    if (value == "notBetween") {
        return DataValidationOperator::NotBetween;
    }
    if (value == "equal") {
        return DataValidationOperator::Equal;
    }
    if (value == "notEqual") {
        return DataValidationOperator::NotEqual;
    }
    if (value == "greaterThan") {
        return DataValidationOperator::GreaterThan;
    }
    if (value == "lessThan") {
        return DataValidationOperator::LessThan;
    }
    if (value == "greaterThanOrEqual") {
        return DataValidationOperator::GreaterThanOrEqual;
    }
    if (value == "lessThanOrEqual") {
        return DataValidationOperator::LessThanOrEqual;
    }
    throw FastXlsxError(
        "worksheet data validation has an unsupported operator");
}

DataValidationErrorStyle parse_error_style(std::string_view value)
{
    if (value == "stop") {
        return DataValidationErrorStyle::Stop;
    }
    if (value == "warning") {
        return DataValidationErrorStyle::Warning;
    }
    if (value == "information") {
        return DataValidationErrorStyle::Information;
    }
    throw FastXlsxError(
        "worksheet data validation has an unsupported errorStyle");
}

std::optional<int> prefix_schema_rank(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetPr", 1}, {"dimension", 2}, {"sheetViews", 3},
        {"sheetFormatPr", 4}, {"cols", 5},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [name](const auto& value) { return value.first == name; });
    return found == std::end(ranks) ? std::nullopt
                                    : std::optional<int>(found->second);
}

std::optional<int> suffix_schema_rank(std::string_view name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetCalcPr", 1}, {"sheetProtection", 2}, {"protectedRanges", 3},
        {"scenarios", 4}, {"autoFilter", 5}, {"sortState", 6},
        {"dataConsolidate", 7}, {"customSheetViews", 8}, {"mergeCells", 9},
        {"phoneticPr", 10}, {"conditionalFormatting", 11},
        {"dataValidations", 12}, {"hyperlinks", 13}, {"printOptions", 14},
        {"pageMargins", 15}, {"pageSetup", 16}, {"headerFooter", 17},
        {"rowBreaks", 18}, {"colBreaks", 19}, {"customProperties", 20},
        {"cellWatches", 21}, {"ignoredErrors", 22}, {"smartTags", 23},
        {"drawing", 24}, {"legacyDrawing", 25}, {"legacyDrawingHF", 26},
        {"picture", 27}, {"oleObjects", 28}, {"controls", 29},
        {"webPublishItems", 30}, {"tableParts", 31}, {"extLst", 32},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [name](const auto& value) { return value.first == name; });
    return found == std::end(ranks) ? std::nullopt
                                    : std::optional<int>(found->second);
}

enum class FrameRole {
    Generic,
    DataValidations,
    DataValidation,
    Formula1,
    Formula2,
};

struct Frame {
    std::string local_name;
    std::string prefix;
    FrameRole role = FrameRole::Generic;
};

struct ActiveValidation {
    WorksheetDataValidationView view;
    bool saw_formula1 = false;
    bool saw_formula2 = false;
};

class WorksheetDataValidationProjectionReader {
public:
    WorksheetDataValidationProjectionReader(
        const WorksheetDataValidationReadCallbacks& callbacks,
        WorksheetDataValidationReaderOptions options)
        : callbacks_(callbacks)
        , options_(options)
    {
    }

    void consume(const WorksheetEvent& event)
    {
        switch (event.kind) {
        case WorksheetEventKind::WorksheetStart:
            consume_worksheet_start(event);
            return;
        case WorksheetEventKind::WorksheetEnd:
            consume_worksheet_end(event);
            return;
        case WorksheetEventKind::SheetDataStart:
            consume_sheet_data_start(event);
            return;
        case WorksheetEventKind::SheetDataEnd:
            consume_sheet_data_end(event);
            return;
        case WorksheetEventKind::Metadata:
            if (!inside_cell_) {
                consume_metadata(event);
            }
            return;
        case WorksheetEventKind::RawText:
            if (!inside_cell_) {
                consume_raw_text(event.raw_xml);
            }
            return;
        case WorksheetEventKind::Unsupported:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet data validation contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet data validation contains unsupported nested markup");
            }
            return;
        case WorksheetEventKind::XmlDeclaration:
        case WorksheetEventKind::RowStart:
        case WorksheetEventKind::RowEnd:
        case WorksheetEventKind::CellValueMarkup:
        case WorksheetEventKind::CellValue:
            return;
        case WorksheetEventKind::CellStart:
            inside_cell_ = true;
            return;
        case WorksheetEventKind::CellEnd:
            inside_cell_ = false;
            return;
        }
    }

    [[nodiscard]] WorksheetDataValidationReadSummary finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_
            || !saw_sheet_data_end_ || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet data validation reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty() || active_.has_value()) {
            throw FastXlsxError(
                "worksheet data validation reader ended inside an open element");
        }
        return summary_;
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError(
                "worksheet data validation contains duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "worksheet" || tag.self_closing) {
            throw FastXlsxError(
                "worksheet data validation contains an invalid worksheet root");
        }
        root_prefix_ = std::string(tag.prefix);
        saw_worksheet_start_ = true;
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.self_closing || !tag.closing || tag.local_name != "worksheet"
            || tag.prefix != root_prefix_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet data validation worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet data validation contains an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData"
            || tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet data validation sheetData QName is mismatched");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet data validation contains a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet data validation sheetData QName is mismatched");
            }
        }
        saw_sheet_data_end_ = true;
    }

    void consume_raw_text(std::string_view text)
    {
        if (!stack_.empty() && stack_.back().role == FrameRole::Formula1) {
            append_formula_text(active_->view.rule.formula1, text, "formula1");
            return;
        }
        if (!stack_.empty() && stack_.back().role == FrameRole::Formula2) {
            append_formula_text(active_->view.rule.formula2, text, "formula2");
            return;
        }
        if (text.empty() || std::all_of(text.begin(), text.end(), is_xml_space)) {
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet data validation target contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet data validation contains unexpected worksheet text");
        }
        // Other worksheet metadata may legally contain text. It is outside
        // this narrow projection and is intentionally audited only by the
        // event reader's structural boundaries.
    }

    void append_formula_text(
        std::string& destination, std::string_view raw, std::string_view label)
    {
        append_decoded_xml(destination, raw, options_.max_formula_text_bytes,
            label == "formula1" ? "formula1 (max_formula_text_bytes)"
                                 : "formula2 (max_formula_text_bytes)",
            false);
        summary_.peak_formula_text_bytes = std::max(
            summary_.peak_formula_text_bytes, destination.size());
    }

    bool inside_target() const noexcept
    {
        return std::any_of(stack_.begin(), stack_.end(), [](const Frame& frame) {
            return frame.role != FrameRole::Generic;
        });
    }

    void consume_metadata(const WorksheetEvent& event)
    {
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.local_name != event.element_name) {
            throw FastXlsxError(
                "worksheet data validation element local name is mismatched");
        }
        if (tag.closing) {
            consume_metadata_close(tag);
        } else {
            consume_metadata_open(tag);
        }
    }

    void consume_metadata_close(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().local_name != tag.local_name
            || stack_.back().prefix != tag.prefix) {
            throw FastXlsxError(
                "worksheet data validation contains mismatched element QName nesting");
        }
        const Frame frame = stack_.back();
        stack_.pop_back();
        if (frame.role == FrameRole::Formula1
            || frame.role == FrameRole::Formula2) {
            return;
        }
        if (frame.role == FrameRole::DataValidation) {
            finish_active_validation();
            return;
        }
        if (frame.role == FrameRole::DataValidations) {
            if (declared_count_.has_value()
                && *declared_count_ != validation_child_count_) {
                throw FastXlsxError(
                    "worksheet data validation count does not match its direct children");
            }
        }
    }

    void enforce_top_level_schema(const ParsedTag& tag)
    {
        if (stack_.empty() && !saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet data validation has unsupported top-level metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet data validation prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet data validation top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty() && saw_sheet_data_end_) {
            const std::optional<int> rank = suffix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet data validation has unsupported top-level suffix metadata");
            }
            if (*rank < last_suffix_rank_) {
                throw FastXlsxError(
                    "worksheet data validation suffix elements are not in schema order");
            }
            last_suffix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet data validation top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet data validation appears in an invalid worksheet region");
        }
    }

    void consume_metadata_open(const ParsedTag& tag)
    {
        enforce_top_level_schema(tag);
        if (tag.prefix == root_prefix_ && tag.local_name == "dataValidations") {
            open_data_validations(tag);
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "dataValidation") {
            open_data_validation(tag);
            return;
        }
        if (tag.prefix == root_prefix_
            && (tag.local_name == "formula1" || tag.local_name == "formula2")) {
            open_formula(tag);
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet data validation contains an unsupported child element");
        }
        if (!stack_.empty() && tag.prefix != stack_.back().prefix
            && stack_.back().role == FrameRole::Generic) {
            // A foreign namespace is preserved outside this companion, but
            // its nested local names must not be mistaken for validation data.
            open_generic(tag);
            return;
        }
        open_generic(tag);
    }

    void open_data_validations(const ParsedTag& tag)
    {
        if (!stack_.empty() || saw_data_validations_) {
            throw FastXlsxError(
                "worksheet contains duplicate or nested dataValidations containers");
        }
        if (tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet dataValidations QName prefix differs from worksheet root");
        }
        saw_data_validations_ = true;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (name == "count") {
                const std::string value = decode_xml_value(
                    raw_value, 32U, "count", true);
                const std::uint64_t count = parse_unsigned_decimal(value, "count");
                if (count > options_.max_validation_count) {
                    throw FastXlsxError(
                        "worksheet data validation count exceeds max_validation_count");
                }
                declared_count_ = count;
            } else if (name == "disablePrompts") {
                const std::string value = decode_xml_value(
                    raw_value, 16U, "disablePrompts", true);
                if (parse_boolean(value, "disablePrompts")) {
                    throw FastXlsxError(
                        "worksheet data validation does not project disablePrompts=true");
                }
            } else if (name == "xWindow" || name == "yWindow") {
                const std::string value = decode_xml_value(
                    raw_value, 32U, name, true);
                if (parse_unsigned_decimal(value, name) != 0) {
                    throw FastXlsxError(
                        "worksheet data validation does not project nonzero prompt window metadata");
                }
            } else {
                throw FastXlsxError(
                    "worksheet dataValidations has an unsupported attribute");
            }
        }
        if (tag.self_closing) {
            if (declared_count_.has_value() && *declared_count_ != 0) {
                throw FastXlsxError(
                    "self-closing dataValidations must have count zero");
            }
            return;
        }
        push_frame(tag, FrameRole::DataValidations);
    }

    void open_data_validation(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::DataValidations) {
            throw FastXlsxError(
                "worksheet dataValidation is not a direct dataValidations child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet dataValidation QName prefix differs from dataValidations");
        }
        if (validation_child_count_ >= options_.max_validation_count) {
            throw FastXlsxError(
                "worksheet data validation exceeds max_validation_count");
        }

        ActiveValidation active;
        active.view.index = validation_child_count_;
        bool saw_type = false;
        bool saw_sqref = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (name == "type") {
                const std::string value = decode_xml_value(
                    raw_value, 32U, "type", true);
                active.view.rule.type = parse_validation_type(value);
                saw_type = true;
            } else if (name == "operator") {
                const std::string value = decode_xml_value(
                    raw_value, 64U, "operator", true);
                active.view.rule.operator_type = parse_validation_operator(value);
            } else if (name == "allowBlank") {
                const std::string value = decode_xml_value(
                    raw_value, 16U, "allowBlank", true);
                active.view.rule.allow_blank = parse_boolean(value, "allowBlank");
            } else if (name == "showDropDown") {
                const std::string value = decode_xml_value(
                    raw_value, 16U, "showDropDown", true);
                active.view.rule.hide_dropdown_arrow =
                    parse_boolean(value, "showDropDown");
            } else if (name == "showInputMessage") {
                const std::string value = decode_xml_value(
                    raw_value, 16U, "showInputMessage", true);
                active.view.rule.show_input_message =
                    parse_boolean(value, "showInputMessage");
            } else if (name == "showErrorMessage") {
                const std::string value = decode_xml_value(
                    raw_value, 16U, "showErrorMessage", true);
                active.view.rule.show_error_message =
                    parse_boolean(value, "showErrorMessage");
            } else if (name == "errorStyle") {
                const std::string value = decode_xml_value(
                    raw_value, 32U, "errorStyle", true);
                active.view.rule.error_style = parse_error_style(value);
            } else if (name == "errorTitle" || name == "error"
                || name == "promptTitle" || name == "prompt") {
                const std::string value = decode_xml_value(
                    raw_value, options_.max_metadata_text_bytes, name, true);
                summary_.peak_metadata_text_bytes = std::max(
                    summary_.peak_metadata_text_bytes, value.size());
                if (name == "errorTitle") {
                    active.view.rule.error_title = value;
                } else if (name == "error") {
                    active.view.rule.error = value;
                } else if (name == "promptTitle") {
                    active.view.rule.prompt_title = value;
                } else {
                    active.view.rule.prompt = value;
                }
            } else if (name == "sqref") {
                const std::string value = decode_xml_value(
                    raw_value, options_.max_sqref_bytes,
                    "sqref (max_sqref_bytes)", true);
                summary_.peak_sqref_bytes = std::max(
                    summary_.peak_sqref_bytes, value.size());
                active.view.ranges = parse_sqref(
                    value, options_.max_ranges_per_validation);
                saw_sqref = true;
            } else {
                throw FastXlsxError(
                    "worksheet dataValidation has an unsupported attribute");
            }
        }
        if (!saw_type) {
            throw FastXlsxError(
                "worksheet dataValidation requires an explicit supported type");
        }
        if (!saw_sqref) {
            throw FastXlsxError(
                "worksheet dataValidation requires sqref");
        }
        ++validation_child_count_;
        active_ = std::move(active);
        summary_.peak_ranges_per_validation = std::max(
            summary_.peak_ranges_per_validation, active_->view.ranges.size());
        if (tag.self_closing) {
            finish_active_validation();
            return;
        }
        push_frame(tag, FrameRole::DataValidation);
    }

    void open_formula(const ParsedTag& tag)
    {
        if (!active_.has_value() || stack_.empty()
            || stack_.back().role != FrameRole::DataValidation) {
            throw FastXlsxError(
                "worksheet data-validation formula is not a direct rule child");
        }
        if (tag.prefix != stack_.back().prefix || !tag.attributes.empty()) {
            throw FastXlsxError(
                "worksheet data-validation formula has an invalid QName or attribute");
        }
        if (tag.local_name == "formula1") {
            if (active_->saw_formula1 || active_->saw_formula2) {
                throw FastXlsxError(
                    "worksheet dataValidation has duplicate or out-of-order formula1");
            }
            active_->saw_formula1 = true;
        } else {
            if (!active_->saw_formula1 || active_->saw_formula2) {
                throw FastXlsxError(
                    "worksheet dataValidation has duplicate or out-of-order formula2");
            }
            active_->saw_formula2 = true;
        }
        if (!tag.self_closing) {
            push_frame(tag, tag.local_name == "formula1"
                ? FrameRole::Formula1 : FrameRole::Formula2);
        }
    }

    void open_generic(const ParsedTag& tag)
    {
        if (!tag.self_closing) {
            push_frame(tag, FrameRole::Generic);
        }
    }

    void push_frame(const ParsedTag& tag, FrameRole role)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError(
                "worksheet data validation exceeds max_xml_nesting_depth");
        }
        stack_.push_back(Frame {
            std::string(tag.local_name), std::string(tag.prefix), role});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    void finish_active_validation()
    {
        if (!active_.has_value()) {
            throw FastXlsxError(
                "worksheet dataValidation closed without an active rule");
        }
        if (active_->saw_formula2 && active_->view.rule.formula2.empty()) {
            throw FastXlsxError(
                "worksheet data validation formula2 cannot be empty");
        }
        validate_data_validation_rule(active_->view.rule);
        WorksheetDataValidationView value = std::move(active_->view);
        active_.reset();
        ++summary_.validation_count;
        if (summary_.range_count > std::numeric_limits<std::uint64_t>::max()
                - static_cast<std::uint64_t>(value.ranges.size())) {
            throw FastXlsxError(
                "worksheet data validation range count overflows summary");
        }
        summary_.range_count += static_cast<std::uint64_t>(value.ranges.size());
        if (callbacks_.on_data_validation) {
            callbacks_.on_data_validation(value);
        }
    }

    const WorksheetDataValidationReadCallbacks& callbacks_;
    WorksheetDataValidationReaderOptions options_;
    WorksheetDataValidationReadSummary summary_;
    std::vector<Frame> stack_;
    std::optional<ActiveValidation> active_;
    std::optional<std::uint64_t> declared_count_;
    std::string root_prefix_;
    std::uint64_t validation_child_count_ = 0;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool saw_data_validations_ = false;
    bool inside_cell_ = false;
};

} // namespace

WorksheetDataValidationReadSummary
read_worksheet_data_validations_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetDataValidationReadCallbacks& callbacks,
    WorksheetDataValidationReaderOptions options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_validation_count == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_validation_count");
    }
    if (options.max_ranges_per_validation == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_ranges_per_validation");
    }
    if (options.max_sqref_bytes == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_sqref_bytes");
    }
    if (options.max_formula_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_formula_text_bytes");
    }
    if (options.max_metadata_text_bytes == 0) {
        throw FastXlsxError(
            "WorksheetDataValidationReader requires nonzero max_metadata_text_bytes");
    }

    WorksheetDataValidationProjectionReader projection(callbacks, options);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&projection](const WorksheetEvent& event) { projection.consume(event); },
        event_options);
    return projection.finish();
}

} // namespace fastxlsx::detail
