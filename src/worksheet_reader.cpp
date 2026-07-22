#include <fastxlsx/worksheet_reader.hpp>

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include "package_reader.hpp"
#include "shared_strings_reader.hpp"
#include "styles_reader.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

using fastxlsx::WorksheetCellValueKind;
using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventKind;

constexpr std::string_view shared_strings_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings";
constexpr std::string_view styles_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles";
constexpr std::string_view shared_strings_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml";
constexpr std::string_view styles_content_type =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml";

bool is_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

bool has_non_whitespace(std::string_view value) noexcept
{
    return std::any_of(value.begin(), value.end(), [](char ch) { return !is_space(ch); });
}

std::string_view trim(std::string_view value) noexcept
{
    while (!value.empty() && is_space(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_space(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::string_view tag_body(std::string_view raw_tag)
{
    if (raw_tag.size() < 2 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw fastxlsx::FastXlsxError("worksheet reader found invalid XML markup");
    }

    std::string_view body = trim(raw_tag.substr(1, raw_tag.size() - 2));
    if (!body.empty() && body.front() == '/') {
        body.remove_prefix(1);
        body = trim(body);
    }
    if (!body.empty() && body.back() == '/') {
        body.remove_suffix(1);
        body = trim(body);
    }
    return body;
}

template <typename Visitor>
void visit_attributes(std::string_view raw_tag, Visitor&& visitor)
{
    const std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position])
            && body[position] != '=' && body[position] != '/'
            && body[position] != '?') {
            ++position;
        }
        if (position == name_begin) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid attribute name");
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an attribute without a value");
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size()
            || (body[position] != '"' && body[position] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an unquoted attribute value");
        }

        const char quote = body[position++];
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an unterminated attribute value");
        }

        visitor(name, body.substr(value_begin, position - value_begin));
        ++position;
        if (position < body.size() && !is_space(body[position])) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader requires whitespace between attributes");
        }
    }
}

bool is_ascii_alpha(char ch) noexcept
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

std::uint32_t parse_decimal_u32(std::string_view value, std::string_view context)
{
    std::uint64_t parsed = 0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (value.empty() || result.ec != std::errc {} || result.ptr != end
        || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid " + std::string(context));
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint32_t parse_row_number(std::string_view value)
{
    const std::uint32_t row = parse_decimal_u32(value, "row number");
    if (row == 0 || row > 1048576U) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader row number exceeds Excel limits");
    }
    return row;
}

struct ParsedCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

ParsedCellReference parse_cell_reference(std::string_view reference)
{
    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        const char ch = reference[position];
        const std::uint32_t digit = ch >= 'a' && ch <= 'z'
            ? static_cast<std::uint32_t>(ch - 'a' + 1)
            : static_cast<std::uint32_t>(ch - 'A' + 1);
        column = column * 26U + digit;
        if (column > 16384U) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > 1048576U) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid cell reference");
    }

    return ParsedCellReference {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

bool is_valid_xml_code_point(std::uint32_t code_point) noexcept
{
    return code_point == 0x09U || code_point == 0x0AU || code_point == 0x0DU
        || (code_point >= 0x20U && code_point <= 0xD7FFU)
        || (code_point >= 0xE000U && code_point <= 0xFFFDU)
        || (code_point >= 0x10000U && code_point <= 0x10FFFFU);
}

void append_utf8(std::string& output, std::uint32_t code_point)
{
    if (!is_valid_xml_code_point(code_point)) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid XML character reference");
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

std::uint32_t parse_character_reference(std::string_view entity)
{
    if (entity.size() < 2 || entity.front() != '#') {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an unknown XML entity reference");
    }

    std::size_t offset = 1;
    std::uint32_t base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid XML character reference");
    }

    std::uint32_t value = 0;
    for (; offset < entity.size(); ++offset) {
        const char ch = entity[offset];
        std::uint32_t digit = base;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<std::uint32_t>(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = static_cast<std::uint32_t>(ch - 'a' + 10);
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = static_cast<std::uint32_t>(ch - 'A' + 10);
        }
        if (digit >= base || value > (0x10FFFFU - digit) / base) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid XML character reference");
        }
        value = value * base + digit;
    }
    return value;
}

std::string decode_xml_text(std::string_view value)
{
    std::string output;
    output.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        if (value[offset] != '&') {
            output.push_back(value[offset]);
            continue;
        }

        const std::size_t semicolon = value.find(';', offset + 1);
        if (semicolon == std::string_view::npos) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an unterminated XML entity reference");
        }
        const std::string_view entity =
            value.substr(offset + 1, semicolon - offset - 1);
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
            append_utf8(output, parse_character_reference(entity));
        }
        offset = semicolon;
    }
    return output;
}

bool is_closing_tag(std::string_view raw_tag) noexcept
{
    return raw_tag.size() > 2 && raw_tag[0] == '<' && raw_tag[1] == '/';
}

enum class SourceCellType {
    Number,
    Text,
    Boolean,
    InlineString,
    SharedStringIndex,
    Error,
    DateToken,
};

struct ActiveCell {
    ParsedCellReference coordinate;
    std::string reference;
    SourceCellType type = SourceCellType::Number;
    std::uint32_t style_index = 0;
    std::string current_value_element;
    std::string scalar_text;
    std::string inline_text;
    std::string formula_text;
    bool has_style = false;
    bool saw_formula = false;
    bool saw_scalar = false;
    bool saw_inline_container = false;
    bool inside_inline_container = false;
    bool saw_inline_text = false;
};

class WorksheetProjectionReader {
public:
    WorksheetProjectionReader(const fastxlsx::WorksheetReadCallbacks& callbacks,
        fastxlsx::WorksheetReaderOptions options,
        bool has_shared_strings_relationship,
        bool has_styles_relationship)
        : callbacks_(callbacks)
        , options_(options)
        , has_shared_strings_relationship_(has_shared_strings_relationship)
        , has_styles_relationship_(has_styles_relationship)
    {
    }

    void consume(const WorksheetEvent& event)
    {
        switch (event.kind) {
        case WorksheetEventKind::SheetDataStart:
            begin_sheet_data();
            break;
        case WorksheetEventKind::SheetDataEnd:
            end_sheet_data();
            break;
        case WorksheetEventKind::RowStart:
            begin_row(event);
            break;
        case WorksheetEventKind::RowEnd:
            end_row();
            break;
        case WorksheetEventKind::CellStart:
            begin_cell(event);
            break;
        case WorksheetEventKind::CellValueMarkup:
            consume_value_markup(event);
            break;
        case WorksheetEventKind::CellValue:
            consume_value_text(event);
            break;
        case WorksheetEventKind::CellEnd:
            end_cell();
            break;
        case WorksheetEventKind::Metadata:
            if (active_cell_.has_value()) {
                consume_cell_metadata(event);
            }
            break;
        case WorksheetEventKind::RawText:
            consume_raw_text(event.raw_xml);
            break;
        case WorksheetEventKind::ProcessingInstruction:
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::Unsupported:
            if (active_cell_.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found unsupported markup inside a cell");
            }
            break;
        default:
            break;
        }
    }

    [[nodiscard]] fastxlsx::WorksheetReadSummary finish() const
    {
        if (!saw_sheet_data_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader requires a sheetData element");
        }
        if (inside_sheet_data_ || active_row_.has_value() || active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader ended inside an open worksheet element");
        }
        return summary_;
    }

private:
    void begin_sheet_data()
    {
        if (inside_sheet_data_ || saw_sheet_data_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid sheetData boundary");
        }
        saw_sheet_data_ = true;
        inside_sheet_data_ = true;
    }

    void end_sheet_data()
    {
        if (!inside_sheet_data_ || active_row_.has_value() || active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid sheetData boundary");
        }
        inside_sheet_data_ = false;
    }

    void begin_row(const WorksheetEvent& event)
    {
        if (!inside_sheet_data_ || active_row_.has_value() || active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid row boundary");
        }

        std::optional<std::string_view> row_attribute;
        visit_attributes(event.raw_xml,
            [&](std::string_view name, std::string_view value) {
                if (name == "r") {
                    if (row_attribute.has_value()) {
                        throw fastxlsx::FastXlsxError(
                            "worksheet reader found a duplicate row number attribute");
                    }
                    row_attribute = value;
                }
            });
        if (!row_attribute.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader requires explicit row numbers");
        }

        const std::uint32_t row = parse_row_number(*row_attribute);
        if (last_row_.has_value() && row <= *last_row_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found duplicate or out-of-order rows");
        }
        last_row_ = row;
        last_column_ = 0;
        active_row_ = row;
        if (callbacks_.on_row_start) {
            callbacks_.on_row_start(fastxlsx::WorksheetRowView {row});
        }
        ++summary_.row_count;
    }

    void end_row()
    {
        if (!active_row_.has_value() || active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found a row end without an active row");
        }
        const std::uint32_t row = *active_row_;
        if (callbacks_.on_row_end) {
            callbacks_.on_row_end(fastxlsx::WorksheetRowView {row});
        }
        active_row_.reset();
    }

    void begin_cell(const WorksheetEvent& event)
    {
        if (!active_row_.has_value() || active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found a cell outside an active row");
        }

        std::optional<std::string_view> reference;
        std::optional<std::string_view> type;
        std::optional<std::string_view> style;
        visit_attributes(event.raw_xml,
            [&](std::string_view name, std::string_view value) {
                std::optional<std::string_view>* destination = nullptr;
                if (name == "r") {
                    destination = &reference;
                } else if (name == "t") {
                    destination = &type;
                } else if (name == "s") {
                    destination = &style;
                } else {
                    throw fastxlsx::FastXlsxError(
                        "worksheet reader found an unsupported cell attribute: "
                        + std::string(name));
                }
                if (destination->has_value()) {
                    throw fastxlsx::FastXlsxError(
                        "worksheet reader found a duplicate cell attribute: "
                        + std::string(name));
                }
                *destination = value;
            });
        if (!reference.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader requires explicit cell references");
        }

        ActiveCell cell;
        cell.reference = std::string(*reference);
        cell.coordinate = parse_cell_reference(cell.reference);
        if (cell.coordinate.row != *active_row_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader row and cell reference do not match");
        }
        if (cell.coordinate.column <= last_column_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found duplicate or out-of-order cells");
        }
        last_column_ = cell.coordinate.column;

        if (type.has_value()) {
            if (*type == "n") {
                cell.type = SourceCellType::Number;
            } else if (*type == "str") {
                cell.type = SourceCellType::Text;
            } else if (*type == "b") {
                cell.type = SourceCellType::Boolean;
            } else if (*type == "inlineStr") {
                cell.type = SourceCellType::InlineString;
            } else if (*type == "s") {
                if (!has_shared_strings_relationship_) {
                    throw fastxlsx::FastXlsxError(
                        "worksheet reader found a shared-string cell without a workbook sharedStrings relationship");
                }
                cell.type = SourceCellType::SharedStringIndex;
            } else if (*type == "e") {
                cell.type = SourceCellType::Error;
            } else if (*type == "d") {
                cell.type = SourceCellType::DateToken;
            } else {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found an unsupported cell type: "
                    + std::string(*type));
            }
        }
        if (style.has_value()) {
            cell.style_index = parse_decimal_u32(*style, "style index");
            if (!has_styles_relationship_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found a style reference without a workbook styles relationship");
            }
            cell.has_style = true;
        }
        active_cell_ = std::move(cell);
    }

    void consume_value_markup(const WorksheetEvent& event)
    {
        if (!active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found cell value markup outside a cell");
        }
        ActiveCell& cell = *active_cell_;
        if (is_closing_tag(event.raw_xml)) {
            cell.current_value_element.clear();
            return;
        }

        if (event.element_name == "f") {
            if (cell.saw_formula) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found duplicate formula elements");
            }
            if (cell.saw_scalar) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found formula markup after its cached value");
            }
            visit_attributes(event.raw_xml,
                [](std::string_view name, std::string_view) {
                    throw fastxlsx::FastXlsxError(
                        "worksheet reader does not project formula metadata attribute: "
                        + std::string(name));
                });
            if (cell.type == SourceCellType::InlineString
                || cell.type == SourceCellType::SharedStringIndex) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found a formula with an unsupported cell type");
            }
            cell.saw_formula = true;
        } else if (event.element_name == "v") {
            if (cell.saw_scalar) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found duplicate scalar value elements");
            }
            visit_attributes(event.raw_xml,
                [](std::string_view name, std::string_view) {
                    throw fastxlsx::FastXlsxError(
                        "worksheet reader does not project scalar value attribute: "
                        + std::string(name));
                });
            if (cell.type == SourceCellType::InlineString) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found a scalar value in an inline string cell");
            }
            cell.saw_scalar = true;
        } else if (event.element_name == "t") {
            if (cell.type != SourceCellType::InlineString
                || !cell.inside_inline_container) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found inline text outside an inline string container");
            }
            if (cell.saw_inline_text) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found duplicate inline text elements");
            }
            bool saw_space = false;
            visit_attributes(event.raw_xml,
                [&](std::string_view name, std::string_view value) {
                    if (name != "xml:space" || saw_space
                        || (value != "default" && value != "preserve")) {
                        throw fastxlsx::FastXlsxError(
                            "worksheet reader found an unsupported inline text attribute");
                    }
                    saw_space = true;
                });
            cell.saw_inline_text = true;
        }

        if (!event.self_closing) {
            cell.current_value_element = std::string(event.element_name);
        }
    }

    void consume_value_text(const WorksheetEvent& event)
    {
        if (!active_cell_.has_value()
            || active_cell_->current_value_element.empty()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found value text without an active value element");
        }

        ActiveCell& cell = *active_cell_;
        std::string* destination = nullptr;
        if (cell.current_value_element == "f") {
            destination = &cell.formula_text;
        } else if (cell.current_value_element == "v") {
            destination = &cell.scalar_text;
        } else if (cell.current_value_element == "t") {
            destination = &cell.inline_text;
        } else {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an unsupported value element");
        }

        const std::string decoded = decode_xml_text(event.text);
        const std::size_t current_text_bytes = active_cell_text_bytes();
        if (current_text_bytes > options_.max_cell_text_bytes
            || decoded.size() > options_.max_cell_text_bytes - current_text_bytes) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader exceeded max_cell_text_bytes");
        }
        destination->append(decoded);
        summary_.peak_cell_text_bytes =
            std::max(summary_.peak_cell_text_bytes, active_cell_text_bytes());
    }

    void consume_cell_metadata(const WorksheetEvent& event)
    {
        ActiveCell& cell = *active_cell_;
        if (!cell.current_value_element.empty()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found nested markup inside a cell value");
        }
        if (event.element_name != "is" || cell.type != SourceCellType::InlineString) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader does not project rich or extended cell metadata: "
                + std::string(event.element_name));
        }

        if (is_closing_tag(event.raw_xml)) {
            if (!cell.inside_inline_container) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found an invalid inline string boundary");
            }
            cell.inside_inline_container = false;
            return;
        }
        if (cell.saw_inline_container || cell.inside_inline_container) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found duplicate inline string containers");
        }
        visit_attributes(event.raw_xml,
            [](std::string_view name, std::string_view) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader does not project inline string container attribute: "
                    + std::string(name));
            });
        cell.saw_inline_container = true;
        cell.inside_inline_container = !event.self_closing;
    }

    void consume_raw_text(std::string_view raw_text) const
    {
        if (!has_non_whitespace(raw_text)) {
            return;
        }
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found text outside a cell value element");
        }
        if (inside_sheet_data_) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found text directly inside sheetData");
        }
    }

    [[nodiscard]] std::size_t active_cell_text_bytes() const noexcept
    {
        if (!active_cell_.has_value()) {
            return 0;
        }
        return active_cell_->scalar_text.size() + active_cell_->inline_text.size()
            + active_cell_->formula_text.size();
    }

    static double parse_number(std::string_view value)
    {
        double number = 0.0;
        const char* const begin = value.data();
        const char* const end = begin + value.size();
        const auto result = std::from_chars(begin, end, number);
        if (value.empty() || result.ec != std::errc {} || result.ptr != end
            || !std::isfinite(number)) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an invalid numeric cell value");
        }
        return number;
    }

    static bool parse_boolean(std::string_view value)
    {
        if (value == "1") {
            return true;
        }
        if (value == "0") {
            return false;
        }
        throw fastxlsx::FastXlsxError(
            "worksheet reader found an invalid boolean cell value");
    }

    static fastxlsx::WorksheetCellView project_cell(const ActiveCell& cell)
    {
        fastxlsx::WorksheetCellView view;
        view.row = cell.coordinate.row;
        view.column = cell.coordinate.column;
        view.reference = cell.reference;
        view.has_formula = cell.saw_formula;
        view.formula_text = cell.formula_text;
        view.has_style = cell.has_style;
        view.style_index = cell.style_index;

        if (cell.saw_formula && cell.formula_text.empty()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found an empty formula without projected formula metadata");
        }

        if (cell.type == SourceCellType::InlineString) {
            if (cell.saw_formula || cell.saw_scalar || !cell.saw_inline_container
                || cell.inside_inline_container || !cell.saw_inline_text) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found an invalid simple inline string shape");
            }
            view.has_value = true;
            view.value_kind = WorksheetCellValueKind::Text;
            view.text_value = cell.inline_text;
            return view;
        }

        if (!cell.saw_scalar) {
            return view;
        }
        view.has_value = true;
        switch (cell.type) {
        case SourceCellType::Number:
            view.value_kind = WorksheetCellValueKind::Number;
            view.number_value = parse_number(cell.scalar_text);
            break;
        case SourceCellType::Text:
        case SourceCellType::DateToken:
            view.value_kind = WorksheetCellValueKind::Text;
            view.text_value = cell.scalar_text;
            break;
        case SourceCellType::Boolean:
            view.value_kind = WorksheetCellValueKind::Boolean;
            view.boolean_value = parse_boolean(cell.scalar_text);
            break;
        case SourceCellType::SharedStringIndex:
            view.value_kind = WorksheetCellValueKind::SharedStringIndex;
            view.shared_string_index =
                parse_decimal_u32(cell.scalar_text, "shared-string index");
            break;
        case SourceCellType::Error:
            if (cell.scalar_text.empty()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet reader found an empty error cell value");
            }
            view.value_kind = WorksheetCellValueKind::Error;
            view.text_value = cell.scalar_text;
            break;
        case SourceCellType::InlineString:
            break;
        }
        return view;
    }

    void end_cell()
    {
        if (!active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader found a cell end without an active cell");
        }
        if (!active_cell_->current_value_element.empty()
            || active_cell_->inside_inline_container) {
            throw fastxlsx::FastXlsxError(
                "worksheet reader ended a cell inside open value metadata");
        }

        const fastxlsx::WorksheetCellView view = project_cell(*active_cell_);
        if (callbacks_.on_cell) {
            callbacks_.on_cell(view);
        }
        ++summary_.cell_count;
        active_cell_.reset();
    }

    const fastxlsx::WorksheetReadCallbacks& callbacks_;
    fastxlsx::WorksheetReaderOptions options_;
    bool has_shared_strings_relationship_ = false;
    bool has_styles_relationship_ = false;
    bool saw_sheet_data_ = false;
    bool inside_sheet_data_ = false;
    std::optional<std::uint32_t> active_row_;
    std::optional<std::uint32_t> last_row_;
    std::uint32_t last_column_ = 0;
    std::optional<ActiveCell> active_cell_;
    fastxlsx::WorksheetReadSummary summary_;
};

struct WorkbookRelationshipPresence {
    std::optional<fastxlsx::detail::Relationship> shared_strings;
    std::optional<fastxlsx::detail::Relationship> styles;
};

WorkbookRelationshipPresence inspect_workbook_relationships(
    const fastxlsx::detail::PackageReader& reader)
{
    const fastxlsx::detail::PartName workbook_part = reader.workbook_part();
    const fastxlsx::detail::RelationshipSet* relationships =
        reader.relationships_for(workbook_part);
    if (relationships == nullptr) {
        throw fastxlsx::FastXlsxError(
            "WorkbookReader requires workbook relationships");
    }

    WorkbookRelationshipPresence presence;
    for (const fastxlsx::detail::Relationship& relationship :
        relationships->relationships()) {
        if (relationship.type == shared_strings_relationship_type) {
            if (presence.shared_strings.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "WorkbookReader found duplicate workbook relationship type: "
                    + relationship.type);
            }
            if (relationship.target_mode
                != fastxlsx::detail::Relationship::TargetMode::Internal) {
                throw fastxlsx::FastXlsxError(
                    "WorkbookReader requires internal workbook relationships");
            }
            presence.shared_strings = relationship;
            continue;
        }
        if (relationship.type != styles_relationship_type) {
            continue;
        }
        if (presence.styles.has_value()) {
            throw fastxlsx::FastXlsxError(
                "WorkbookReader found duplicate workbook relationship type: "
                + relationship.type);
        }
        if (relationship.target_mode
            != fastxlsx::detail::Relationship::TargetMode::Internal) {
            throw fastxlsx::FastXlsxError(
                "WorkbookReader requires internal workbook relationships");
        }
        presence.styles = relationship;
    }
    return presence;
}

int relationship_hex_digit(char ch) noexcept
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

std::string decode_relationship_target(
    std::string_view target, std::string_view relationship_name)
{
    std::string decoded;
    decoded.reserve(target.size());
    for (std::size_t index = 0; index < target.size(); ++index) {
        const char ch = target[index];
        if (ch != '%') {
            decoded.push_back(ch);
            continue;
        }
        if (index + 2 >= target.size()) {
            throw fastxlsx::FastXlsxError(
                std::string(relationship_name)
                + " relationship target has an incomplete percent escape");
        }
        const int high = relationship_hex_digit(target[index + 1]);
        const int low = relationship_hex_digit(target[index + 2]);
        if (high < 0 || low < 0) {
            throw fastxlsx::FastXlsxError(
                std::string(relationship_name)
                + " relationship target has an invalid percent escape");
        }
        const char decoded_char = static_cast<char>((high << 4) | low);
        if (decoded_char == '\0') {
            throw fastxlsx::FastXlsxError(
                std::string(relationship_name)
                + " relationship target contains a null byte");
        }
        decoded.push_back(decoded_char);
        index += 2;
    }
    return decoded;
}

fastxlsx::detail::PartName resolve_workbook_relationship_part(
    const fastxlsx::detail::PartName& workbook_part,
    const fastxlsx::detail::Relationship& relationship,
    std::string_view relationship_name)
{
    if (relationship.target_mode
        != fastxlsx::detail::Relationship::TargetMode::Internal) {
        throw fastxlsx::FastXlsxError(
            "WorkbookReader requires an internal " + std::string(relationship_name)
            + " relationship");
    }
    if (relationship.target.empty()
        || relationship.target.find_first_of("?#") != std::string::npos) {
        throw fastxlsx::FastXlsxError(
            std::string(relationship_name)
            + " relationship target must be a package part");
    }

    std::string target =
        decode_relationship_target(relationship.target, relationship_name);
    if (target.empty()) {
        throw fastxlsx::FastXlsxError(
            std::string(relationship_name)
            + " relationship target must be a package part");
    }
    if (target.front() == '/') {
        return fastxlsx::detail::PartName(target);
    }

    const std::string& source = workbook_part.value();
    const std::size_t slash = source.find_last_of('/');
    const std::string base = slash == std::string::npos || slash == 0
        ? std::string("/")
        : source.substr(0, slash + 1);
    return fastxlsx::detail::PartName(base + target);
}

} // namespace

namespace fastxlsx {

struct WorkbookReader::Impl {
    struct Sheet {
        std::string name;
        detail::PartName part_name;
    };

    Impl(detail::PackageReader package_reader,
        std::vector<detail::WorkbookSheetReference> workbook_sheets)
        : package(std::move(package_reader))
        , relationships(inspect_workbook_relationships(package))
    {
        sheets.reserve(workbook_sheets.size());
        names.reserve(workbook_sheets.size());
        for (detail::WorkbookSheetReference& sheet : workbook_sheets) {
            names.push_back(sheet.name);
            sheets.push_back(Sheet {std::move(sheet.name), std::move(sheet.part_name)});
        }
    }

    [[nodiscard]] const Sheet* find_sheet(std::string_view name) const noexcept
    {
        const auto match = std::find_if(sheets.begin(), sheets.end(),
            [name](const Sheet& sheet) { return sheet.name == name; });
        return match == sheets.end() ? nullptr : &*match;
    }

    detail::PackageReader package;
    WorkbookRelationshipPresence relationships;
    std::vector<Sheet> sheets;
    std::vector<std::string> names;
};

WorkbookReader::WorkbookReader(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

WorkbookReader WorkbookReader::open(std::filesystem::path path)
{
    detail::PackageReader package = detail::PackageReader::open(std::move(path));
    std::vector<detail::WorkbookSheetReference> sheets = package.workbook_sheets();
    return WorkbookReader(
        std::make_unique<Impl>(std::move(package), std::move(sheets)));
}

WorkbookReader::~WorkbookReader() = default;

WorkbookReader::WorkbookReader(WorkbookReader&&) noexcept = default;
WorkbookReader& WorkbookReader::operator=(WorkbookReader&&) noexcept = default;

const std::filesystem::path& WorkbookReader::source_path() const noexcept
{
    static const std::filesystem::path empty_path;
    return impl_ ? impl_->package.path() : empty_path;
}

const std::vector<std::string>& WorkbookReader::worksheet_names() const noexcept
{
    static const std::vector<std::string> empty_names;
    return impl_ ? impl_->names : empty_names;
}

bool WorkbookReader::has_worksheet(std::string_view sheet_name) const noexcept
{
    return impl_ != nullptr && impl_->find_sheet(sheet_name) != nullptr;
}

WorksheetReadSummary WorkbookReader::read_worksheet(
    std::string_view sheet_name,
    const WorksheetReadCallbacks& callbacks,
    WorksheetReaderOptions options) const
{
    if (!impl_) {
        throw FastXlsxError("WorkbookReader is not open");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_cell_text_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_cell_text_bytes");
    }

    const Impl::Sheet* sheet = impl_->find_sheet(sheet_name);
    if (sheet == nullptr) {
        throw FastXlsxError(
            "WorkbookReader worksheet not found: " + std::string(sheet_name));
    }

    WorksheetProjectionReader projection_reader(callbacks, options,
        impl_->relationships.shared_strings.has_value(),
        impl_->relationships.styles.has_value());
    detail::WorksheetEventReaderOptions event_options;
    event_options.max_window_bytes = options.max_xml_window_bytes;
    event_options.copy_context_attributes = true;

    detail::scan_worksheet_events_from_chunk_source(
        impl_->package.entry_chunk_source(sheet->part_name.zip_path()),
        [&projection_reader](const detail::WorksheetEvent& event) {
            projection_reader.consume(event);
        },
        event_options);
    return projection_reader.finish();
}

SharedStringReadSummary WorkbookReader::read_shared_strings(
    const SharedStringReadCallbacks& callbacks,
    SharedStringReaderOptions options) const
{
    if (!impl_) {
        throw FastXlsxError("WorkbookReader is not open");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_item_text_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_item_text_bytes");
    }
    if (!impl_->relationships.shared_strings.has_value()) {
        throw FastXlsxError(
            "WorkbookReader workbook has no sharedStrings relationship");
    }

    const detail::PartName part = resolve_workbook_relationship_part(
        impl_->package.workbook_part(), *impl_->relationships.shared_strings,
        "sharedStrings");
    const detail::PackagePart* package_part = impl_->package.part_index().find_part(part);
    if (package_part == nullptr) {
        throw FastXlsxError(
            "WorkbookReader sharedStrings relationship targets an unknown part");
    }
    if (package_part->content_type != shared_strings_content_type) {
        throw FastXlsxError(
            "WorkbookReader sharedStrings relationship target has the wrong content type");
    }

    return detail::read_shared_strings_from_chunk_source(
        impl_->package.entry_chunk_source(part.zip_path()), callbacks, options);
}

CellFormatReadSummary WorkbookReader::read_cell_formats(
    const CellFormatReadCallbacks& callbacks,
    CellFormatReaderOptions options) const
{
    if (!impl_) {
        throw FastXlsxError("WorkbookReader is not open");
    }
    if (options.max_xml_window_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_xml_window_bytes");
    }
    if (options.max_format_code_bytes == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_format_code_bytes");
    }
    if (options.max_xml_nesting_depth == 0) {
        throw FastXlsxError("WorkbookReader requires nonzero max_xml_nesting_depth");
    }
    if (options.max_custom_number_format_count == 0) {
        throw FastXlsxError(
            "WorkbookReader requires nonzero max_custom_number_format_count");
    }
    if (!impl_->relationships.styles.has_value()) {
        throw FastXlsxError("WorkbookReader workbook has no styles relationship");
    }

    const detail::PartName part = resolve_workbook_relationship_part(
        impl_->package.workbook_part(), *impl_->relationships.styles, "styles");
    const detail::PackagePart* package_part = impl_->package.part_index().find_part(part);
    if (package_part == nullptr) {
        throw FastXlsxError(
            "WorkbookReader styles relationship targets an unknown part");
    }
    if (package_part->content_type != styles_content_type) {
        throw FastXlsxError(
            "WorkbookReader styles relationship target has the wrong content type");
    }

    return detail::read_cell_formats_from_chunk_source(
        impl_->package.entry_chunk_source(part.zip_path()), callbacks, options);
}

} // namespace fastxlsx
