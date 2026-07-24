#include "worksheet_conditional_format_reader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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

struct QualifiedName {
    std::string_view prefix;
    std::string_view local_name;
};

QualifiedName parse_qualified_name(
    std::string_view name, std::string_view context)
{
    const std::size_t separator = name.find(':');
    if (separator == std::string_view::npos) {
        if (name.empty()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an empty "
                + std::string(context));
        }
        return QualifiedName {{}, name};
    }
    if (separator == 0 || separator + 1U == name.size()
        || name.find(':', separator + 1U) != std::string_view::npos) {
        throw FastXlsxError(
            "worksheet conditional formatting contains an invalid qualified "
            + std::string(context));
    }
    return QualifiedName {name.substr(0, separator),
        name.substr(separator + 1U)};
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
        throw FastXlsxError(
            "worksheet conditional formatting contains an invalid XML tag");
    }
    if (raw_xml[1] == '!' || raw_xml[1] == '?') {
        throw FastXlsxError(
            "worksheet conditional formatting received unsupported declaration markup");
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
            "worksheet conditional formatting contains an empty element name");
    }
    result.qualified_name = raw_xml.substr(name_begin, position - name_begin);
    const QualifiedName element_name = parse_qualified_name(
        result.qualified_name, "element name");
    result.local_name = element_name.local_name;
    if (!element_name.prefix.empty()) {
        result.prefix = result.qualified_name.substr(
            0, element_name.prefix.size() + 1U);
    }

    if (result.closing) {
        while (position < raw_xml.size() - 1U
            && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position != raw_xml.size() - 1U || raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet conditional formatting closing tag contains attributes");
        }
        return result;
    }

    while (position < raw_xml.size()) {
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an incomplete XML tag");
        }
        if (raw_xml[position] == '>') {
            ++position;
            if (position != raw_xml.size()) {
                throw FastXlsxError(
                    "worksheet conditional formatting contains trailing XML tag bytes");
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
                    "worksheet conditional formatting contains an invalid self-closing tag tail");
            }
            result.self_closing = true;
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
                "worksheet conditional formatting contains an empty attribute name");
        }
        const std::string_view attribute_name =
            raw_xml.substr(attribute_begin, position - attribute_begin);
        (void)parse_qualified_name(attribute_name, "attribute name");
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size() || raw_xml[position] != '=') {
            throw FastXlsxError(
                "worksheet conditional formatting contains an attribute without a value");
        }
        ++position;
        while (position < raw_xml.size() && is_xml_space(raw_xml[position])) {
            ++position;
        }
        if (position >= raw_xml.size()
            || (raw_xml[position] != '"' && raw_xml[position] != '\'')) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an unquoted attribute value");
        }
        const char quote = raw_xml[position++];
        const std::size_t value_begin = position;
        while (position < raw_xml.size() && raw_xml[position] != quote) {
            ++position;
        }
        if (position >= raw_xml.size()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an unterminated attribute value");
        }
        const std::string_view attribute_value =
            raw_xml.substr(value_begin, position - value_begin);
        ++position;
        if (position < raw_xml.size() && !is_xml_space(raw_xml[position])
            && raw_xml[position] != '/' && raw_xml[position] != '>') {
            throw FastXlsxError(
                "worksheet conditional formatting attributes are not separated by whitespace");
        }
        for (const auto& [existing_name, existing_value] : result.attributes) {
            (void)existing_value;
            if (existing_name == attribute_name) {
                throw FastXlsxError(
                    "worksheet conditional formatting contains a duplicate attribute");
            }
        }
        result.attributes.emplace_back(attribute_name, attribute_value);
    }

    throw FastXlsxError(
        "worksheet conditional formatting contains an incomplete XML tag");
}

bool is_valid_xml_code_point(std::uint32_t code_point) noexcept
{
    return code_point == 0x09U || code_point == 0x0AU || code_point == 0x0DU
        || (code_point >= 0x20U && code_point <= 0xD7FFU)
        || (code_point >= 0xE000U && code_point <= 0xFFFDU)
        || (code_point >= 0x10000U && code_point <= 0x10FFFFU);
}

void append_utf8(std::string& output, std::uint32_t code_point,
    std::size_t limit, std::string_view label)
{
    if (!is_valid_xml_code_point(code_point)) {
        throw FastXlsxError(
            "worksheet conditional formatting contains an invalid XML character in "
            + std::string(label));
    }
    std::size_t byte_count = 1;
    if (code_point > 0x7FU) {
        byte_count = code_point <= 0x7FFU ? 2U
            : (code_point <= 0xFFFFU ? 3U : 4U);
    }
    if (output.size() > limit || byte_count > limit - output.size()) {
        throw FastXlsxError(
            "worksheet conditional formatting " + std::string(label)
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
            "worksheet conditional formatting contains an unknown XML entity in "
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
            "worksheet conditional formatting contains an invalid XML entity in "
            + std::string(label));
    }
    std::uint32_t value = 0;
    for (; position < body.size(); ++position) {
        const char character = body[position];
        int digit = -1;
        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = character - 'a' + 10;
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = character - 'A' + 10;
        }
        if (digit < 0 || digit >= base
            || value > (std::numeric_limits<std::uint32_t>::max()
                            - static_cast<std::uint32_t>(digit))
                    / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an invalid XML entity in "
                + std::string(label));
        }
        value = value * static_cast<std::uint32_t>(base)
            + static_cast<std::uint32_t>(digit);
    }
    return value;
}

void append_decoded_xml(std::string& output, std::string_view raw,
    std::size_t limit, std::string_view label)
{
    std::size_t position = 0;
    while (position < raw.size()) {
        if (raw[position] != '&') {
            const char character = is_xml_space(raw[position]) ? ' ' : raw[position];
            if (output.size() >= limit) {
                throw FastXlsxError(
                    "worksheet conditional formatting " + std::string(label)
                    + " exceeds its configured text limit");
            }
            output.push_back(character);
            ++position;
            continue;
        }

        const std::size_t semicolon = raw.find(';', position + 1U);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an unterminated XML entity in "
                + std::string(label));
        }
        const std::string_view body =
            raw.substr(position + 1U, semicolon - position - 1U);
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
    std::string_view raw, std::size_t limit, std::string_view label)
{
    std::string decoded;
    decoded.reserve(std::min(raw.size(), limit));
    append_decoded_xml(decoded, raw, limit, label);
    return decoded;
}

std::uint64_t parse_unsigned_decimal(
    std::string_view value, std::string_view label)
{
    if (value.empty()) {
        throw FastXlsxError(
            "worksheet conditional formatting has an invalid "
            + std::string(label));
    }
    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            throw FastXlsxError(
                "worksheet conditional formatting has an invalid "
                + std::string(label));
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw FastXlsxError(
                "worksheet conditional formatting has an invalid "
                + std::string(label));
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

double parse_finite_double(std::string_view value, std::string_view label)
{
    double parsed = 0.0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (value.empty() || result.ec != std::errc {} || result.ptr != end
        || !std::isfinite(parsed)) {
        throw FastXlsxError(
            "worksheet conditional formatting has an invalid "
            + std::string(label));
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
        "worksheet conditional formatting has an invalid "
        + std::string(label));
}

int hex_digit_value(char character) noexcept
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::uint8_t parse_hex_byte(std::string_view value, std::size_t offset)
{
    const int high = hex_digit_value(value[offset]);
    const int low = hex_digit_value(value[offset + 1U]);
    if (high < 0 || low < 0) {
        throw FastXlsxError(
            "worksheet conditional formatting has an invalid ARGB color");
    }
    return static_cast<std::uint8_t>((high << 4) | low);
}

ArgbColor parse_argb_color(std::string_view value)
{
    if (value.size() != 8U) {
        throw FastXlsxError(
            "worksheet conditional formatting requires 8-digit ARGB colors");
    }
    return ArgbColor {
        parse_hex_byte(value, 0),
        parse_hex_byte(value, 2),
        parse_hex_byte(value, 4),
        parse_hex_byte(value, 6),
    };
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
    return A1Coordinate {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

std::optional<CellRange> parse_a1_range(std::string_view reference)
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
    return CellRange {first->row, first->column, last->row, last->column};
}

std::vector<CellRange> parse_sqref(
    std::string_view sqref, std::size_t max_ranges_per_format)
{
    std::vector<CellRange> ranges;
    std::size_t position = 0;
    while (position < sqref.size()) {
        while (position < sqref.size() && is_xml_space(sqref[position])) {
            ++position;
        }
        if (position >= sqref.size()) {
            break;
        }
        const std::size_t begin = position;
        while (position < sqref.size() && !is_xml_space(sqref[position])) {
            ++position;
        }
        const std::string_view token = sqref.substr(begin, position - begin);
        const std::optional<CellRange> range = parse_a1_range(token);
        if (!range.has_value()) {
            throw FastXlsxError(
                "worksheet conditional formatting sqref contains an invalid A1 range");
        }
        if (ranges.size() >= max_ranges_per_format) {
            throw FastXlsxError(
                "worksheet conditional formatting exceeds max_ranges_per_format");
        }
        ranges.push_back(*range);
    }
    if (ranges.empty()) {
        throw FastXlsxError(
            "worksheet conditional formatting requires a non-empty sqref");
    }
    return ranges;
}

ColorScaleValueType parse_color_scale_value_type(std::string_view value)
{
    if (value == "min") {
        return ColorScaleValueType::Minimum;
    }
    if (value == "max") {
        return ColorScaleValueType::Maximum;
    }
    if (value == "num") {
        return ColorScaleValueType::Number;
    }
    if (value == "percent") {
        return ColorScaleValueType::Percent;
    }
    if (value == "percentile") {
        return ColorScaleValueType::Percentile;
    }
    throw FastXlsxError(
        "worksheet conditional formatting has an unsupported colorScale cfvo type");
}

DataBarValueType parse_data_bar_value_type(std::string_view value)
{
    if (value == "min") {
        return DataBarValueType::Minimum;
    }
    if (value == "max") {
        return DataBarValueType::Maximum;
    }
    if (value == "num") {
        return DataBarValueType::Number;
    }
    if (value == "percent") {
        return DataBarValueType::Percent;
    }
    if (value == "percentile") {
        return DataBarValueType::Percentile;
    }
    throw FastXlsxError(
        "worksheet conditional formatting has an unsupported dataBar cfvo type");
}

IconSetValueType parse_icon_set_value_type(std::string_view value)
{
    if (value == "num") {
        return IconSetValueType::Number;
    }
    if (value == "percent") {
        return IconSetValueType::Percent;
    }
    if (value == "percentile") {
        return IconSetValueType::Percentile;
    }
    throw FastXlsxError(
        "worksheet conditional formatting has an unsupported iconSet cfvo type");
}

bool color_scale_value_type_requires_value(ColorScaleValueType type) noexcept
{
    return type == ColorScaleValueType::Number
        || type == ColorScaleValueType::Percent
        || type == ColorScaleValueType::Percentile;
}

bool data_bar_value_type_requires_value(DataBarValueType type) noexcept
{
    return type == DataBarValueType::Number
        || type == DataBarValueType::Percent
        || type == DataBarValueType::Percentile;
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
    ConditionalFormatting,
    CfRule,
    ColorScale,
    DataBar,
    IconSet,
};

struct Frame {
    std::string local_name;
    std::string prefix;
    FrameRole role = FrameRole::Generic;
};

struct ParsedCfvo {
    std::string type_token;
    double value = 0.0;
    bool has_value = false;
};

enum class ActiveRuleType {
    ColorScale,
    DataBar,
    IconSet,
};

struct ActiveContainer {
    std::vector<CellRange> ranges;
    std::uint32_t child_count = 0;
};

struct ActiveFormat {
    WorksheetConditionalFormatView view;
    ActiveRuleType rule_type = ActiveRuleType::ColorScale;
    std::uint32_t target_child_count = 0;
    bool target_finished = false;
    std::vector<ParsedCfvo> cfvos;
    std::vector<ArgbColor> colors;
    IconSetStyle icon_set_style = IconSetStyle::ThreeArrows;
    std::optional<bool> show_value;
    std::optional<bool> reverse;
};

class WorksheetConditionalFormatProjectionReader {
public:
    WorksheetConditionalFormatProjectionReader(
        const WorksheetConditionalFormatReadCallbacks& callbacks,
        WorksheetConditionalFormatReaderOptions options)
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
                    "worksheet conditional formatting contains unsupported non-element content");
            }
            return;
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::ProcessingInstruction:
            if (inside_target()) {
                throw FastXlsxError(
                    "worksheet conditional formatting contains unsupported nested markup");
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

    [[nodiscard]] WorksheetConditionalFormatReadSummary finish()
    {
        if (!saw_worksheet_start_ || !saw_sheet_data_start_
            || !saw_sheet_data_end_ || !saw_worksheet_end_) {
            throw FastXlsxError(
                "worksheet conditional formatting reader requires a worksheet root and closed sheetData");
        }
        if (!stack_.empty() || active_container_.has_value()
            || active_.has_value()) {
            throw FastXlsxError(
                "worksheet conditional formatting reader ended inside an open element");
        }
        return summary_;
    }

private:
    void consume_worksheet_start(const WorksheetEvent& event)
    {
        if (saw_worksheet_start_) {
            throw FastXlsxError(
                "worksheet conditional formatting contains duplicate worksheet roots");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "worksheet" || tag.self_closing) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an invalid worksheet root");
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
                "worksheet conditional formatting worksheet root QName is mismatched");
        }
        saw_worksheet_end_ = true;
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        if (!saw_worksheet_start_ || saw_sheet_data_start_ || !stack_.empty()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an invalid sheetData boundary");
        }
        const ParsedTag tag = parse_tag(event.raw_xml);
        if (tag.closing || tag.local_name != "sheetData"
            || tag.prefix != root_prefix_) {
            throw FastXlsxError(
                "worksheet conditional formatting sheetData QName is mismatched");
        }
        saw_sheet_data_start_ = true;
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (!saw_sheet_data_start_ || saw_sheet_data_end_) {
            throw FastXlsxError(
                "worksheet conditional formatting contains a duplicate sheetData end");
        }
        if (!event.self_closing) {
            const ParsedTag tag = parse_tag(event.raw_xml);
            if (!tag.closing || tag.local_name != "sheetData"
                || tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet conditional formatting sheetData QName is mismatched");
            }
        }
        saw_sheet_data_end_ = true;
    }

    void consume_raw_text(std::string_view text)
    {
        if (text.empty() || std::all_of(text.begin(), text.end(), is_xml_space)) {
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet conditional formatting target contains unexpected text");
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains unexpected worksheet text");
        }
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
                "worksheet conditional formatting element local name is mismatched");
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
                "worksheet conditional formatting contains mismatched element QName nesting");
        }
        const Frame frame = stack_.back();
        stack_.pop_back();
        if (frame.role == FrameRole::ColorScale
            || frame.role == FrameRole::DataBar
            || frame.role == FrameRole::IconSet) {
            finish_target(frame.role);
            return;
        }
        if (frame.role == FrameRole::CfRule) {
            finish_active_format();
            return;
        }
        if (frame.role == FrameRole::ConditionalFormatting) {
            if (!active_container_.has_value()
                || active_container_->child_count != 1U) {
                throw FastXlsxError(
                    "worksheet conditionalFormatting requires exactly one cfRule");
            }
            active_container_.reset();
        }
    }

    void enforce_top_level_schema(const ParsedTag& tag)
    {
        if (stack_.empty() && !saw_sheet_data_start_) {
            const std::optional<int> rank = prefix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet conditional formatting has unsupported top-level metadata before sheetData");
            }
            if (*rank < last_prefix_rank_) {
                throw FastXlsxError(
                    "worksheet conditional formatting prefix elements are not in schema order");
            }
            last_prefix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet conditional formatting top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty() && saw_sheet_data_end_) {
            const std::optional<int> rank = suffix_schema_rank(tag.local_name);
            if (!rank.has_value()) {
                throw FastXlsxError(
                    "worksheet conditional formatting has unsupported top-level suffix metadata");
            }
            if (*rank < last_suffix_rank_) {
                throw FastXlsxError(
                    "worksheet conditional formatting suffix elements are not in schema order");
            }
            last_suffix_rank_ = *rank;
            if (tag.prefix != root_prefix_) {
                throw FastXlsxError(
                    "worksheet conditional formatting top-level QName prefix differs from worksheet root");
            }
            return;
        }
        if (stack_.empty()) {
            throw FastXlsxError(
                "worksheet conditional formatting appears in an invalid worksheet region");
        }
    }

    void consume_metadata_open(const ParsedTag& tag)
    {
        enforce_top_level_schema(tag);
        if (tag.prefix == root_prefix_
            && tag.local_name == "conditionalFormatting") {
            open_conditional_formatting(tag);
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "cfRule") {
            open_cf_rule(tag);
            return;
        }
        if (tag.prefix == root_prefix_
            && (tag.local_name == "colorScale" || tag.local_name == "dataBar"
                || tag.local_name == "iconSet")) {
            open_rule_target(tag);
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "cfvo") {
            open_cfvo(tag);
            return;
        }
        if (tag.prefix == root_prefix_ && tag.local_name == "color") {
            open_color(tag);
            return;
        }
        if (inside_target()) {
            throw FastXlsxError(
                "worksheet conditional formatting contains an unsupported child element");
        }
        open_generic(tag);
    }

    void open_conditional_formatting(const ParsedTag& tag)
    {
        if (!stack_.empty() || active_container_.has_value()) {
            throw FastXlsxError(
                "worksheet contains nested conditionalFormatting containers");
        }
        if (tag.self_closing) {
            throw FastXlsxError(
                "worksheet conditionalFormatting requires exactly one cfRule");
        }
        std::optional<std::string> sqref;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (attribute_name.prefix.empty()
                && attribute_name.local_name == "sqref") {
                sqref = decode_xml_value(raw_value, options_.max_sqref_bytes,
                    "sqref (max_sqref_bytes)");
                summary_.peak_sqref_bytes = std::max(
                    summary_.peak_sqref_bytes, sqref->size());
            } else {
                throw FastXlsxError(
                    "worksheet conditionalFormatting has an unsupported attribute");
            }
        }
        if (!sqref.has_value()) {
            throw FastXlsxError(
                "worksheet conditionalFormatting requires sqref");
        }
        ActiveContainer container;
        container.ranges = parse_sqref(*sqref, options_.max_ranges_per_format);
        summary_.peak_ranges_per_format = std::max(
            summary_.peak_ranges_per_format, container.ranges.size());
        active_container_ = std::move(container);
        push_frame(tag, FrameRole::ConditionalFormatting);
    }

    void open_cf_rule(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::ConditionalFormatting
            || !active_container_.has_value()) {
            throw FastXlsxError(
                "worksheet cfRule is not a direct conditionalFormatting child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet cfRule QName prefix differs from conditionalFormatting");
        }
        if (active_container_->child_count != 0U) {
            throw FastXlsxError(
                "worksheet conditionalFormatting contains multiple cfRule children");
        }
        if (summary_.conditional_format_count
            >= options_.max_conditional_format_count) {
            throw FastXlsxError(
                "worksheet conditional formatting exceeds max_conditional_format_count");
        }
        if (tag.self_closing) {
            throw FastXlsxError(
                "worksheet cfRule requires one conditional-formatting target child");
        }

        ActiveFormat active;
        active.view.ranges = active_container_->ranges;
        bool saw_type = false;
        bool saw_priority = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (attribute_name.prefix.empty()
                && attribute_name.local_name == "type") {
                const std::string type = decode_xml_value(raw_value,
                    options_.max_xml_window_bytes, "cfRule type");
                if (type == "colorScale") {
                    active.rule_type = ActiveRuleType::ColorScale;
                } else if (type == "dataBar") {
                    active.rule_type = ActiveRuleType::DataBar;
                } else if (type == "iconSet") {
                    active.rule_type = ActiveRuleType::IconSet;
                } else {
                    throw FastXlsxError(
                        "worksheet cfRule has an unsupported type");
                }
                saw_type = true;
            } else if (attribute_name.prefix.empty()
                && attribute_name.local_name == "priority") {
                const std::string priority = decode_xml_value(raw_value,
                    options_.max_xml_window_bytes, "priority");
                const std::uint64_t parsed =
                    parse_unsigned_decimal(priority, "priority");
                if (parsed == 0
                    || parsed > std::numeric_limits<std::uint32_t>::max()) {
                    throw FastXlsxError(
                        "worksheet conditional formatting priority is out of range");
                }
                active.view.priority = static_cast<std::uint32_t>(parsed);
                saw_priority = true;
            } else {
                throw FastXlsxError(
                    "worksheet cfRule has an unsupported attribute");
            }
        }
        if (!saw_type) {
            throw FastXlsxError("worksheet cfRule requires type");
        }
        if (!saw_priority) {
            throw FastXlsxError("worksheet cfRule requires priority");
        }

        ++active_container_->child_count;
        active_ = std::move(active);
        push_frame(tag, FrameRole::CfRule);
    }

    void open_rule_target(const ParsedTag& tag)
    {
        if (stack_.empty() || stack_.back().role != FrameRole::CfRule
            || !active_.has_value()) {
            throw FastXlsxError(
                "worksheet conditional-formatting target is not a direct cfRule child");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet conditional-formatting target QName prefix differs from cfRule");
        }
        if (active_->target_child_count != 0U) {
            throw FastXlsxError(
                "worksheet cfRule contains multiple target children");
        }
        const FrameRole role = target_role_for_tag(tag);
        ++active_->target_child_count;
        parse_target_attributes(tag, role);
        if (tag.self_closing) {
            finish_target(role);
            return;
        }
        push_frame(tag, role);
    }

    FrameRole target_role_for_tag(const ParsedTag& tag) const
    {
        if (tag.local_name == "colorScale") {
            if (active_->rule_type != ActiveRuleType::ColorScale) {
                throw FastXlsxError(
                    "worksheet cfRule target does not match type=colorScale");
            }
            return FrameRole::ColorScale;
        }
        if (tag.local_name == "dataBar") {
            if (active_->rule_type != ActiveRuleType::DataBar) {
                throw FastXlsxError(
                    "worksheet cfRule target does not match type=dataBar");
            }
            return FrameRole::DataBar;
        }
        if (tag.local_name == "iconSet") {
            if (active_->rule_type != ActiveRuleType::IconSet) {
                throw FastXlsxError(
                    "worksheet cfRule target does not match type=iconSet");
            }
            return FrameRole::IconSet;
        }
        throw FastXlsxError(
            "worksheet cfRule target has an unsupported element");
    }

    void parse_target_attributes(const ParsedTag& tag, FrameRole role)
    {
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet conditional-formatting target has an unsupported attribute");
            }
            const std::string value = decode_xml_value(raw_value,
                options_.max_xml_window_bytes, "conditional-formatting target attribute");
            if (role == FrameRole::ColorScale) {
                throw FastXlsxError(
                    "worksheet colorScale has an unsupported attribute");
            }
            if (role == FrameRole::DataBar) {
                if (attribute_name.local_name == "showValue") {
                    active_->show_value = parse_boolean(value, "dataBar showValue");
                } else {
                    throw FastXlsxError(
                        "worksheet dataBar has an unsupported advanced attribute");
                }
                continue;
            }
            if (role == FrameRole::IconSet) {
                if (attribute_name.local_name == "iconSet") {
                    if (value != "3Arrows") {
                        throw FastXlsxError(
                            "worksheet iconSet supports only 3Arrows");
                    }
                    active_->icon_set_style = IconSetStyle::ThreeArrows;
                } else if (attribute_name.local_name == "showValue") {
                    active_->show_value = parse_boolean(value, "iconSet showValue");
                } else if (attribute_name.local_name == "reverse") {
                    active_->reverse = parse_boolean(value, "iconSet reverse");
                } else {
                    throw FastXlsxError(
                        "worksheet iconSet has an unsupported advanced attribute");
                }
            }
        }
        if (role == FrameRole::IconSet) {
            bool saw_icon_set = false;
            for (const auto& [name, raw_value] : tag.attributes) {
                (void)raw_value;
                const QualifiedName attribute_name = parse_qualified_name(
                    name, "attribute name");
                if (attribute_name.prefix.empty()
                    && attribute_name.local_name == "iconSet") {
                    saw_icon_set = true;
                }
            }
            if (!saw_icon_set) {
                throw FastXlsxError("worksheet iconSet requires iconSet=3Arrows");
            }
        }
    }

    void open_cfvo(const ParsedTag& tag)
    {
        if (stack_.empty()
            || (stack_.back().role != FrameRole::ColorScale
                && stack_.back().role != FrameRole::DataBar
                && stack_.back().role != FrameRole::IconSet)
            || !active_.has_value()) {
            throw FastXlsxError(
                "worksheet cfvo is not a direct conditional-formatting target child");
        }
        if (!tag.self_closing) {
            throw FastXlsxError(
                "worksheet cfvo must be self-closing in the narrow projection");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet cfvo QName prefix differs from its parent");
        }
        ParsedCfvo cfvo;
        bool saw_type = false;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (!attribute_name.prefix.empty()) {
                throw FastXlsxError(
                    "worksheet cfvo has an unsupported attribute");
            }
            const std::string value = decode_xml_value(raw_value,
                options_.max_xml_window_bytes, "cfvo attribute");
            if (attribute_name.local_name == "type") {
                cfvo.type_token = value;
                saw_type = true;
            } else if (attribute_name.local_name == "val") {
                cfvo.value = parse_finite_double(value, "cfvo value");
                cfvo.has_value = true;
            } else {
                throw FastXlsxError(
                    "worksheet cfvo has an unsupported attribute");
            }
        }
        if (!saw_type) {
            throw FastXlsxError("worksheet cfvo requires type");
        }
        if (!active_->colors.empty()) {
            throw FastXlsxError(
                "worksheet conditional formatting cfvo appears after color");
        }
        if (active_->cfvos.size() >= 3U) {
            throw FastXlsxError(
                "worksheet conditional formatting has too many cfvo children");
        }
        active_->cfvos.push_back(std::move(cfvo));
    }

    void open_color(const ParsedTag& tag)
    {
        if (stack_.empty()
            || (stack_.back().role != FrameRole::ColorScale
                && stack_.back().role != FrameRole::DataBar)
            || !active_.has_value()) {
            throw FastXlsxError(
                "worksheet color is not a direct colorScale/dataBar child");
        }
        if (!tag.self_closing) {
            throw FastXlsxError(
                "worksheet color must be self-closing in the narrow projection");
        }
        if (tag.prefix != stack_.back().prefix) {
            throw FastXlsxError(
                "worksheet color QName prefix differs from its parent");
        }
        std::optional<ArgbColor> color;
        for (const auto& [name, raw_value] : tag.attributes) {
            if (is_namespace_declaration(name)) {
                continue;
            }
            const QualifiedName attribute_name = parse_qualified_name(
                name, "attribute name");
            if (attribute_name.prefix.empty()
                && attribute_name.local_name == "rgb") {
                const std::string value = decode_xml_value(raw_value,
                    options_.max_xml_window_bytes, "color rgb");
                color = parse_argb_color(value);
            } else {
                throw FastXlsxError(
                    "worksheet color has an unsupported attribute");
            }
        }
        if (!color.has_value()) {
            throw FastXlsxError("worksheet color requires rgb");
        }
        if (stack_.back().role == FrameRole::ColorScale
            && active_->cfvos.size() < 2U) {
            throw FastXlsxError(
                "worksheet colorScale color appears before enough cfvo children");
        }
        if (stack_.back().role == FrameRole::DataBar
            && active_->cfvos.size() != 2U) {
            throw FastXlsxError(
                "worksheet dataBar color must follow exactly two cfvo children");
        }
        const std::size_t max_colors = stack_.back().role == FrameRole::DataBar
            ? 1U
            : 3U;
        if (active_->colors.size() >= max_colors) {
            throw FastXlsxError(
                "worksheet conditional formatting has too many color children");
        }
        active_->colors.push_back(*color);
    }

    void finish_target(FrameRole role)
    {
        if (!active_.has_value()) {
            throw FastXlsxError(
                "worksheet conditional-formatting target closed without an active rule");
        }
        if (active_->target_finished) {
            throw FastXlsxError(
                "worksheet conditional-formatting target closed more than once");
        }
        switch (role) {
        case FrameRole::ColorScale:
            finish_color_scale();
            break;
        case FrameRole::DataBar:
            finish_data_bar();
            break;
        case FrameRole::IconSet:
            finish_icon_set();
            break;
        case FrameRole::Generic:
        case FrameRole::ConditionalFormatting:
        case FrameRole::CfRule:
            throw FastXlsxError(
                "worksheet conditional formatting closed an invalid target");
        }
        active_->target_finished = true;
    }

    void finish_color_scale()
    {
        if (active_->cfvos.size() != 2U && active_->cfvos.size() != 3U) {
            throw FastXlsxError(
                "worksheet colorScale requires two or three cfvo children");
        }
        if (active_->colors.size() != active_->cfvos.size()) {
            throw FastXlsxError(
                "worksheet colorScale color count must match cfvo count");
        }
        std::vector<ColorScalePoint> points;
        for (std::size_t index = 0; index < active_->cfvos.size(); ++index) {
            const ColorScaleValueType type =
                parse_color_scale_value_type(active_->cfvos[index].type_token);
            if (color_scale_value_type_requires_value(type)
                != active_->cfvos[index].has_value) {
                throw FastXlsxError(
                    "worksheet colorScale cfvo value shape is invalid");
            }
            points.push_back(ColorScalePoint {
                type, active_->cfvos[index].value, active_->colors[index]});
        }
        if (points.front().type == ColorScaleValueType::Maximum) {
            throw FastXlsxError(
                "worksheet lower color scale endpoint cannot use maximum");
        }
        if (points.back().type == ColorScaleValueType::Minimum) {
            throw FastXlsxError(
                "worksheet upper color scale endpoint cannot use minimum");
        }
        if (points.size() == 2U) {
            active_->view.kind = WorksheetConditionalFormatKind::TwoColorScale;
            active_->view.two_color_scale =
                TwoColorScaleRule {points[0], points[1]};
            return;
        }
        if (points[1].type == ColorScaleValueType::Minimum
            || points[1].type == ColorScaleValueType::Maximum) {
            throw FastXlsxError(
                "worksheet middle color scale point must use a value-bearing type");
        }
        active_->view.kind = WorksheetConditionalFormatKind::ThreeColorScale;
        active_->view.three_color_scale =
            ThreeColorScaleRule {points[0], points[1], points[2]};
    }

    void finish_data_bar()
    {
        if (active_->cfvos.size() != 2U) {
            throw FastXlsxError(
                "worksheet dataBar requires exactly two cfvo children");
        }
        if (active_->colors.size() != 1U) {
            throw FastXlsxError("worksheet dataBar requires exactly one color");
        }
        DataBarEndpoint lower {
            parse_data_bar_value_type(active_->cfvos[0].type_token),
            active_->cfvos[0].value,
        };
        DataBarEndpoint upper {
            parse_data_bar_value_type(active_->cfvos[1].type_token),
            active_->cfvos[1].value,
        };
        if (data_bar_value_type_requires_value(lower.type)
            != active_->cfvos[0].has_value
            || data_bar_value_type_requires_value(upper.type)
                != active_->cfvos[1].has_value) {
            throw FastXlsxError(
                "worksheet dataBar cfvo value shape is invalid");
        }
        if (lower.type == DataBarValueType::Maximum) {
            throw FastXlsxError(
                "worksheet lower data bar endpoint cannot use maximum");
        }
        if (upper.type == DataBarValueType::Minimum) {
            throw FastXlsxError(
                "worksheet upper data bar endpoint cannot use minimum");
        }
        DataBarRule rule;
        rule.lower = lower;
        rule.upper = upper;
        rule.color = active_->colors[0];
        rule.show_value = active_->show_value.value_or(true);
        active_->view.kind = WorksheetConditionalFormatKind::DataBar;
        active_->view.data_bar = rule;
    }

    void finish_icon_set()
    {
        if (active_->cfvos.size() != 3U) {
            throw FastXlsxError(
                "worksheet iconSet requires exactly three cfvo children");
        }
        if (!active_->colors.empty()) {
            throw FastXlsxError("worksheet iconSet must not contain colors");
        }
        IconSetRule rule;
        rule.style = active_->icon_set_style;
        rule.show_value = active_->show_value.value_or(true);
        rule.reverse = active_->reverse.value_or(false);
        std::optional<IconSetValueType> value_type;
        for (std::size_t index = 0; index < active_->cfvos.size(); ++index) {
            if (!active_->cfvos[index].has_value) {
                throw FastXlsxError(
                    "worksheet iconSet cfvo requires finite values");
            }
            const IconSetValueType current =
                parse_icon_set_value_type(active_->cfvos[index].type_token);
            if (!value_type.has_value()) {
                value_type = current;
            } else if (*value_type != current) {
                throw FastXlsxError(
                    "worksheet iconSet cfvo types must match one public value_type");
            }
            rule.thresholds[index] = active_->cfvos[index].value;
        }
        if (!(rule.thresholds[0] < rule.thresholds[1]
                && rule.thresholds[1] < rule.thresholds[2])) {
            throw FastXlsxError(
                "worksheet iconSet thresholds must be strictly ascending");
        }
        rule.value_type = *value_type;
        active_->view.kind = WorksheetConditionalFormatKind::IconSet;
        active_->view.icon_set = rule;
    }

    void finish_active_format()
    {
        if (!active_.has_value() || active_->target_child_count != 1U
            || !active_->target_finished) {
            throw FastXlsxError(
                "worksheet cfRule requires one conditional-formatting target child");
        }
        WorksheetConditionalFormatView value = std::move(active_->view);
        active_.reset();
        value.index = summary_.conditional_format_count;
        ++summary_.conditional_format_count;
        summary_.range_count += value.ranges.size();
        switch (value.kind) {
        case WorksheetConditionalFormatKind::TwoColorScale:
        case WorksheetConditionalFormatKind::ThreeColorScale:
            ++summary_.color_scale_count;
            break;
        case WorksheetConditionalFormatKind::DataBar:
            ++summary_.data_bar_count;
            break;
        case WorksheetConditionalFormatKind::IconSet:
            ++summary_.icon_set_count;
            break;
        }
        if (callbacks_.on_conditional_format) {
            callbacks_.on_conditional_format(value);
        }
    }

    void open_generic(const ParsedTag& tag)
    {
        if (tag.self_closing) {
            return;
        }
        push_frame(tag, FrameRole::Generic);
    }

    void push_frame(const ParsedTag& tag, FrameRole role)
    {
        if (stack_.size() >= options_.max_xml_nesting_depth) {
            throw FastXlsxError(
                "worksheet conditional formatting exceeds max_xml_nesting_depth");
        }
        stack_.push_back(Frame {
            std::string(tag.local_name), std::string(tag.prefix), role});
        summary_.peak_xml_nesting_depth = std::max(
            summary_.peak_xml_nesting_depth, stack_.size());
    }

    static bool is_namespace_declaration(std::string_view name) noexcept
    {
        return name == "xmlns" || name.starts_with("xmlns:");
    }

    const WorksheetConditionalFormatReadCallbacks& callbacks_;
    WorksheetConditionalFormatReaderOptions options_;
    WorksheetConditionalFormatReadSummary summary_;
    std::vector<Frame> stack_;
    std::optional<ActiveContainer> active_container_;
    std::optional<ActiveFormat> active_;
    std::string root_prefix_;
    int last_prefix_rank_ = 0;
    int last_suffix_rank_ = 0;
    bool saw_worksheet_start_ = false;
    bool saw_sheet_data_start_ = false;
    bool saw_sheet_data_end_ = false;
    bool saw_worksheet_end_ = false;
    bool inside_cell_ = false;
};

} // namespace

WorksheetConditionalFormatReadSummary
read_worksheet_conditional_formats_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetConditionalFormatReadCallbacks& callbacks,
    WorksheetConditionalFormatReaderOptions options)
{
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError(
            "WorksheetConditionalFormatReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError(
            "WorksheetConditionalFormatReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_conditional_format_count == 0) {
        throw FastXlsxError(
            "WorksheetConditionalFormatReader requires nonzero max_conditional_format_count");
    }
    if (options.max_ranges_per_format == 0) {
        throw FastXlsxError(
            "WorksheetConditionalFormatReader requires nonzero max_ranges_per_format");
    }
    if (options.max_sqref_bytes == 0) {
        throw FastXlsxError(
            "WorksheetConditionalFormatReader requires nonzero max_sqref_bytes");
    }

    WorksheetConditionalFormatProjectionReader projection(callbacks, options);
    WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&projection](const WorksheetEvent& event) { projection.consume(event); },
        event_options);
    return projection.finish();
}

} // namespace fastxlsx::detail
