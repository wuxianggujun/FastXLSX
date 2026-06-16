#include <fastxlsx/detail/cell_store.hpp>
#include "package_reader.hpp"

#include <fastxlsx/detail/xml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fastxlsx::detail {

bool operator<(const CellPosition& left, const CellPosition& right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

namespace {

void validate_position(std::uint32_t row, std::uint32_t column)
{
    (void)cell_reference(row, column);
}

std::size_t record_memory_usage(const CellRecord& record) noexcept
{
    return sizeof(CellRecord) + record.text_value.capacity();
}

std::size_t entry_memory_usage(const CellPosition& position, const CellRecord& record) noexcept
{
    return sizeof(position) + record_memory_usage(record) + (sizeof(void*) * 3);
}

bool is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool is_ascii_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

void append_utf8(std::string& output, std::uint32_t code_point)
{
    if (code_point <= 0x7Fu) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFu) {
        output.push_back(static_cast<char>(0xC0u | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point <= 0xFFFFu) {
        output.push_back(static_cast<char>(0xE0u | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else if (code_point <= 0x10FFFFu) {
        output.push_back(static_cast<char>(0xF0u | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 12) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3Fu)));
    } else {
        throw FastXlsxError("CellStore worksheet loader found an invalid Unicode code point");
    }
}

std::uint32_t parse_xml_character_reference(std::string_view entity)
{
    if (entity.size() < 2 || entity.front() != '#') {
        throw FastXlsxError("CellStore worksheet loader found an unknown XML entity reference");
    }

    std::size_t offset = 1;
    int base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw FastXlsxError("CellStore worksheet loader found an invalid XML character reference");
    }

    std::uint32_t value = 0;
    for (; offset < entity.size(); ++offset) {
        const char ch = entity[offset];
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            throw FastXlsxError(
                "CellStore worksheet loader found an invalid XML character reference");
        }
        if (value > (0x10FFFFu - static_cast<std::uint32_t>(digit))
                / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError(
                "CellStore worksheet loader XML character reference exceeds Unicode range");
        }
        value = value * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(digit);
    }

    return value;
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
            throw FastXlsxError("CellStore worksheet loader found an unterminated XML entity");
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
            append_utf8(output, parse_xml_character_reference(entity));
        }
        offset = semicolon;
    }

    return output;
}

std::uint32_t uppercase_column_value(char ch)
{
    const char upper =
        static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return static_cast<std::uint32_t>(upper - 'A' + 1);
}

std::string_view trim(std::string_view value)
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
        throw FastXlsxError("CellStore worksheet loader found invalid XML markup");
    }

    std::string_view body = raw_tag.substr(1, raw_tag.size() - 2);
    body = trim(body);
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

std::optional<std::string_view> unqualified_attribute_value(
    std::string_view raw_tag, std::string_view attribute_name)
{
    std::string_view body = tag_body(raw_tag);
    std::optional<std::string_view> value;
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return value;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position]) && body[position] != '='
            && body[position] != '/' && body[position] != '?') {
            ++position;
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            continue;
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unterminated attribute value");
        }

        if (name == attribute_name) {
            if (value.has_value()) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate attributes");
            }
            value = body.substr(value_begin, position - value_begin);
        }
        ++position;
    }

    return value;
}

bool raw_tag_has_attributes(std::string_view raw_tag)
{
    const std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }
    while (position < body.size() && is_space(body[position])) {
        ++position;
    }
    return position < body.size() && body[position] != '/' && body[position] != '?';
}

bool is_allowed_attribute_name(
    std::string_view name, std::string_view allowed_first, std::optional<std::string_view> allowed_second)
{
    return name == allowed_first || (allowed_second.has_value() && name == *allowed_second);
}

bool raw_tag_has_unsupported_attributes(
    std::string_view raw_tag,
    std::string_view allowed_first,
    std::optional<std::string_view> allowed_second = std::nullopt)
{
    std::string_view body = tag_body(raw_tag);
    std::size_t position = 0;
    while (position < body.size() && !is_space(body[position])) {
        ++position;
    }

    while (position < body.size()) {
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] == '/' || body[position] == '?') {
            return false;
        }

        const std::size_t name_begin = position;
        while (position < body.size() && !is_space(body[position]) && body[position] != '='
            && body[position] != '/' && body[position] != '?') {
            ++position;
        }
        const std::string_view name = body.substr(name_begin, position - name_begin);
        if (!is_allowed_attribute_name(name, allowed_first, allowed_second)) {
            return true;
        }

        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || body[position] != '=') {
            return true;
        }
        ++position;
        while (position < body.size() && is_space(body[position])) {
            ++position;
        }
        if (position >= body.size() || (body[position] != '"' && body[position] != '\'')) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw FastXlsxError(
                "CellStore worksheet loader found an unterminated attribute value");
        }
        ++position;
    }

    return false;
}

struct ParsedCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::uint32_t parse_row_number(std::string_view value, std::string_view context)
{
    if (value.empty()) {
        throw FastXlsxError(std::string(context) + " requires a row number");
    }

    std::uint64_t row = 0;
    for (const char ch : value) {
        if (!is_ascii_digit(ch)) {
            throw FastXlsxError(std::string(context) + " found an invalid row number");
        }
        row = row * 10U + static_cast<std::uint32_t>(ch - '0');
        if (row > 1048576U) {
            throw FastXlsxError(std::string(context) + " row exceeds Excel limits");
        }
    }
    if (row == 0) {
        throw FastXlsxError(std::string(context) + " row must be one-based");
    }
    return static_cast<std::uint32_t>(row);
}

ParsedCellReference parse_cell_reference_for_load(std::string_view reference)
{
    if (reference.empty()) {
        throw FastXlsxError("CellStore worksheet loader requires explicit cell references");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > 16384U) {
            throw FastXlsxError(
                "CellStore worksheet loader cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw FastXlsxError("CellStore worksheet loader found an invalid cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > 1048576U) {
            throw FastXlsxError(
                "CellStore worksheet loader cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw FastXlsxError("CellStore worksheet loader found an invalid cell reference");
    }

    return ParsedCellReference {
        static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)};
}

bool needs_space_preserve(std::string_view value)
{
    return !value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n'
            || value.front() == '\r' || value.back() == ' ' || value.back() == '\t'
            || value.back() == '\n' || value.back() == '\r');
}

void append_text_element(std::string& xml, std::string_view value)
{
    if (needs_space_preserve(value)) {
        xml += "<t xml:space=\"preserve\">";
    } else {
        xml += "<t>";
    }
    append_escaped_xml_text(xml, value);
    xml += "</t>";
}

void append_style_attribute(std::string& xml, const CellRecord& record)
{
    if (!record.style_id.has_value() || record.style_id->value() == 0) {
        return;
    }

    xml += " s=\"";
    append_unsigned_decimal(xml, record.style_id->value());
    xml += "\"";
}

void append_cell_xml(std::string& xml, const CellPosition& position, const CellRecord& record)
{
    xml += "<c r=\"";
    append_cell_reference(xml, position.row, position.column);
    xml += "\"";
    append_style_attribute(xml, record);

    switch (record.kind) {
    case CellValueKind::Blank:
        xml += "/>";
        break;
    case CellValueKind::Number:
        xml += "><v>";
        append_number(xml, record.number_value);
        xml += "</v></c>";
        break;
    case CellValueKind::Text:
        xml += " t=\"inlineStr\"><is>";
        append_text_element(xml, record.text_value);
        xml += "</is></c>";
        break;
    case CellValueKind::Boolean:
        xml += " t=\"b\"><v>";
        xml += record.boolean_value ? "1" : "0";
        xml += "</v></c>";
        break;
    case CellValueKind::Formula:
        xml += "><f>";
        append_escaped_xml_text(xml, record.text_value);
        xml += "</f></c>";
        break;
    }
}

class CellStoreSheetDataChunkSourceState {
public:
    explicit CellStoreSheetDataChunkSourceState(const CellStore& store)
        : current_(store.records().begin())
        , end_(store.records().end())
    {
    }

    bool read(std::string& chunk)
    {
        chunk.clear();
        if (!emitted_start_) {
            emitted_start_ = true;
            chunk = "<sheetData>";
            return true;
        }

        if (current_ != end_) {
            if (!row_open_) {
                return emit_row_start(chunk);
            }
            if (current_->first.row != current_row_) {
                row_open_ = false;
                chunk = "</row>";
                return true;
            }

            append_cell_xml(chunk, current_->first, current_->second);
            ++current_;
            return true;
        }

        if (row_open_) {
            row_open_ = false;
            chunk = "</row>";
            return true;
        }

        if (!emitted_end_) {
            emitted_end_ = true;
            chunk = "</sheetData>";
            return true;
        }

        return false;
    }

private:
    using RecordIterator = std::map<CellPosition, CellRecord>::const_iterator;

    bool emit_row_start(std::string& chunk)
    {
        current_row_ = current_->first.row;
        chunk = "<row r=\"";
        append_unsigned_decimal(chunk, current_row_);
        chunk += "\">";
        row_open_ = true;
        return true;
    }

    RecordIterator current_;
    RecordIterator end_;
    std::uint32_t current_row_ = 0;
    bool emitted_start_ = false;
    bool emitted_end_ = false;
    bool row_open_ = false;
};

class CellStoreWorksheetChunkSourceState {
public:
    explicit CellStoreWorksheetChunkSourceState(const CellStore& store)
        : sheet_data_source_(cell_store_sheet_data_chunk_source(store))
        , dimension_reference_(cell_store_dimension_reference(store))
    {
    }

    bool read(std::string& chunk)
    {
        chunk.clear();
        switch (phase_) {
        case Phase::XmlDeclaration:
            phase_ = Phase::WorksheetStart;
            chunk = R"(<?xml version="1.0" encoding="UTF-8"?>)";
            return true;
        case Phase::WorksheetStart:
            phase_ = Phase::Dimension;
            chunk = R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
            return true;
        case Phase::Dimension:
            phase_ = Phase::SheetData;
            chunk = R"(<dimension ref=")";
            chunk += dimension_reference_;
            chunk += R"("/>)";
            return true;
        case Phase::SheetData:
            if (sheet_data_source_(chunk)) {
                return true;
            }
            phase_ = Phase::WorksheetEnd;
            [[fallthrough]];
        case Phase::WorksheetEnd:
            phase_ = Phase::Done;
            chunk = "</worksheet>";
            return true;
        case Phase::Done:
            return false;
        }

        return false;
    }

private:
    enum class Phase {
        XmlDeclaration,
        WorksheetStart,
        Dimension,
        SheetData,
        WorksheetEnd,
        Done,
    };

    WorksheetInputChunkCallback sheet_data_source_;
    std::string dimension_reference_;
    Phase phase_ = Phase::XmlDeclaration;
};

enum class SourceCellType {
    Number,
    Boolean,
    InlineString,
};

SourceCellType source_cell_type_from_raw_tag(std::string_view raw_xml)
{
    const std::optional<std::string_view> type = unqualified_attribute_value(raw_xml, "t");
    if (!type.has_value() || *type == "n") {
        return SourceCellType::Number;
    }
    if (*type == "b") {
        return SourceCellType::Boolean;
    }
    if (*type == "inlineStr") {
        return SourceCellType::InlineString;
    }
    if (*type == "s") {
        throw FastXlsxError(
            "CellStore worksheet loader does not load shared string indexes");
    }

    std::string message = "CellStore worksheet loader found unsupported cell type";
    if (!type->empty()) {
        message += ": ";
        message += std::string(*type);
    }
    throw FastXlsxError(message);
}

bool is_closing_raw_tag(std::string_view raw_xml)
{
    return raw_xml.size() > 2 && raw_xml.front() == '<' && raw_xml[1] == '/';
}

struct ActiveSourceCell {
    CellPosition position;
    SourceCellType type = SourceCellType::Number;
    std::string current_value_element;
    std::string scalar_text;
    std::string inline_text;
    std::string formula_text;
    bool saw_scalar_value_element = false;
    bool saw_inline_text_element = false;
    bool saw_formula_element = false;
};

double parse_cell_number(std::string_view value)
{
    double number = 0.0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto result = std::from_chars(begin, end, number);
    if (result.ec != std::errc {} || result.ptr != end || !std::isfinite(number)) {
        throw FastXlsxError("CellStore worksheet loader found an invalid numeric cell value");
    }
    return number;
}

bool parse_cell_boolean(std::string_view value)
{
    if (value == "1") {
        return true;
    }
    if (value == "0") {
        return false;
    }
    throw FastXlsxError("CellStore worksheet loader found an invalid boolean cell value");
}

CellValue materialize_cell_value(const ActiveSourceCell& cell)
{
    if (cell.saw_formula_element) {
        if (cell.formula_text.empty()) {
            throw FastXlsxError("CellStore worksheet loader found an empty formula text");
        }
        return CellValue::formula(cell.formula_text);
    }

    switch (cell.type) {
    case SourceCellType::Number:
        if (!cell.saw_scalar_value_element || cell.scalar_text.empty()) {
            return CellValue::blank();
        }
        return CellValue::number(parse_cell_number(cell.scalar_text));
    case SourceCellType::Boolean:
        if (!cell.saw_scalar_value_element || cell.scalar_text.empty()) {
            return CellValue::blank();
        }
        return CellValue::boolean(parse_cell_boolean(cell.scalar_text));
    case SourceCellType::InlineString:
        if (!cell.saw_inline_text_element) {
            return CellValue::blank();
        }
        return CellValue::text(cell.inline_text);
    }

    throw FastXlsxError("CellStore worksheet loader found an unsupported internal cell state");
}

void reject_unsupported_value_shape(const ActiveSourceCell& cell, std::string_view element_name)
{
    if (element_name == "f") {
        if (cell.type != SourceCellType::Number) {
            throw FastXlsxError(
                "CellStore worksheet loader found a formula in a non-numeric cell");
        }
        return;
    }

    if (element_name == "t") {
        if (cell.type != SourceCellType::InlineString) {
            throw FastXlsxError(
                "CellStore worksheet loader found inline text in a non-inline string cell");
        }
        return;
    }

    if (cell.type == SourceCellType::InlineString) {
        throw FastXlsxError(
            "CellStore worksheet loader found a non-inline value in an inline string cell");
    }
}

class CellStoreWorksheetLoader {
public:
    explicit CellStoreWorksheetLoader(CellStoreOptions options)
        : store_(std::move(options))
    {
    }

    void consume(const WorksheetEvent& event)
    {
        switch (event.kind) {
        case WorksheetEventKind::RowStart:
            if (inside_row_) {
                throw FastXlsxError("CellStore worksheet loader found nested rows");
            }
            inside_row_ = true;
            if (raw_tag_has_unsupported_attributes(event.raw_xml, "r")) {
                throw FastXlsxError(
                    "CellStore worksheet loader does not load row metadata attributes");
            }
            if (const std::optional<std::string_view> row_attribute =
                    unqualified_attribute_value(event.raw_xml, "r")) {
                const std::uint32_t row_number = parse_row_number(
                    *row_attribute, "CellStore worksheet loader row");
                if (!seen_rows_.insert(row_number).second) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found duplicate row numbers");
                }
                if (last_explicit_row_.has_value() && row_number < *last_explicit_row_) {
                    throw FastXlsxError(
                        "CellStore worksheet loader found out-of-order row numbers");
                }
                last_explicit_row_ = row_number;
            }
            break;
        case WorksheetEventKind::RowEnd:
            if (!inside_row_) {
                throw FastXlsxError("CellStore worksheet loader found a row end without a row");
            }
            inside_row_ = false;
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
        case WorksheetEventKind::ProcessingInstruction:
        case WorksheetEventKind::Comment:
        case WorksheetEventKind::Unsupported:
            if (active_cell_.has_value()) {
                throw FastXlsxError(
                    "CellStore worksheet loader does not load cell comments, processing instructions, or unsupported markup");
            }
            break;
        default:
            break;
        }
    }

    [[nodiscard]] CellStore finish()
    {
        if (active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader ended inside an open cell");
        }
        if (inside_row_) {
            throw FastXlsxError("CellStore worksheet loader ended inside an open row");
        }
        return std::move(store_);
    }

private:
    void begin_cell(const WorksheetEvent& event)
    {
        if (active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found nested cells");
        }
        if (!inside_row_) {
            throw FastXlsxError("CellStore worksheet loader found a cell outside a row");
        }
        if (unqualified_attribute_value(event.raw_xml, "s").has_value()) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load style id references");
        }
        if (raw_tag_has_unsupported_attributes(event.raw_xml, "r", std::string_view {"t"})) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load cell metadata attributes");
        }

        const std::optional<std::string_view> cell_reference =
            unqualified_attribute_value(event.raw_xml, "r");
        const ParsedCellReference parsed_reference =
            parse_cell_reference_for_load(cell_reference.value_or(event.cell_reference));
        if (!event.row_number.empty()) {
            const std::uint32_t row_number = parse_row_number(
                event.row_number, "CellStore worksheet loader row");
            if (row_number != parsed_reference.row) {
                throw FastXlsxError(
                    "CellStore worksheet loader row and cell reference do not match");
            }
        }

        ActiveSourceCell cell;
        cell.position = CellPosition {parsed_reference.row, parsed_reference.column};
        if (last_source_cell_.has_value() && !(*last_source_cell_ < cell.position)) {
            throw FastXlsxError(
                "CellStore worksheet loader found out-of-order cell references");
        }
        last_source_cell_ = cell.position;
        cell.type = source_cell_type_from_raw_tag(event.raw_xml);
        active_cell_ = std::move(cell);
    }

    void consume_value_markup(const WorksheetEvent& event)
    {
        if (!active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found cell value outside a cell");
        }

        ActiveSourceCell& cell = *active_cell_;
        if (is_closing_raw_tag(event.raw_xml)) {
            cell.current_value_element.clear();
            return;
        }

        reject_unsupported_value_shape(cell, event.element_name);
        if (event.element_name == "f" && raw_tag_has_attributes(event.raw_xml)) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load formula attributes");
        }
        if (event.element_name == "v" && raw_tag_has_attributes(event.raw_xml)) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load scalar value attributes");
        }
        if (event.element_name == "t"
            && raw_tag_has_unsupported_attributes(event.raw_xml, "xml:space")) {
            throw FastXlsxError(
                "CellStore worksheet loader does not load inline text attributes");
        }
        remember_value_element(cell, event.element_name);
        if (!event.self_closing) {
            cell.current_value_element = std::string(event.element_name);
        }
    }

    void consume_cell_metadata(const WorksheetEvent& event)
    {
        ActiveSourceCell& cell = *active_cell_;
        if (event.element_name == "is") {
            if (cell.type != SourceCellType::InlineString) {
                throw FastXlsxError(
                    "CellStore worksheet loader found inline-string metadata in a non-inline string cell");
            }
            return;
        }

        throw FastXlsxError(
            "CellStore worksheet loader does not load inline rich text or phonetic metadata");
    }

    static void remember_value_element(ActiveSourceCell& cell, std::string_view element_name)
    {
        if (element_name == "f") {
            if (cell.saw_formula_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate formula elements");
            }
            cell.saw_formula_element = true;
        } else if (element_name == "v") {
            if (cell.saw_scalar_value_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate scalar value elements");
            }
            cell.saw_scalar_value_element = true;
        } else if (element_name == "t") {
            if (cell.saw_inline_text_element) {
                throw FastXlsxError(
                    "CellStore worksheet loader found duplicate inline text elements");
            }
            cell.saw_inline_text_element = true;
        }
    }

    void consume_value_text(const WorksheetEvent& event)
    {
        if (!active_cell_.has_value() || active_cell_->current_value_element.empty()) {
            throw FastXlsxError("CellStore worksheet loader found value text without a value tag");
        }

        ActiveSourceCell& cell = *active_cell_;
        const std::string decoded_text = unescape_xml_text(event.text);
        if (cell.current_value_element == "f") {
            cell.formula_text += decoded_text;
        } else if (cell.current_value_element == "v") {
            cell.scalar_text += decoded_text;
        } else if (cell.current_value_element == "t") {
            cell.inline_text += decoded_text;
        }
    }

    void end_cell()
    {
        if (!active_cell_.has_value()) {
            throw FastXlsxError("CellStore worksheet loader found a cell end without a cell");
        }

        const ActiveSourceCell cell = std::move(*active_cell_);
        active_cell_.reset();
        if (store_.try_cell(cell.position.row, cell.position.column) != nullptr) {
            throw FastXlsxError(
                "CellStore worksheet loader found duplicate cell references");
        }
        store_.set_cell(cell.position.row, cell.position.column, materialize_cell_value(cell));
    }

    CellStore store_;
    std::set<std::uint32_t> seen_rows_;
    std::optional<std::uint32_t> last_explicit_row_;
    std::optional<CellPosition> last_source_cell_;
    bool inside_row_ = false;
    std::optional<ActiveSourceCell> active_cell_;
};

} // namespace

CellRecord CellRecord::from_value(const CellValue& value)
{
    CellRecord record;
    record.kind = value.kind();
    record.number_value = value.number_value();
    record.boolean_value = value.boolean_value();
    record.text_value = value.text_value();
    if (value.has_style()) {
        record.style_id = value.style_id();
    }
    return record;
}

CellValue CellRecord::to_value() const
{
    CellValue value = CellValue::blank();
    switch (kind) {
    case CellValueKind::Blank:
        value = CellValue::blank();
        break;
    case CellValueKind::Number:
        value = CellValue::number(number_value);
        break;
    case CellValueKind::Text:
        value = CellValue::text(text_value);
        break;
    case CellValueKind::Boolean:
        value = CellValue::boolean(boolean_value);
        break;
    case CellValueKind::Formula:
        value = CellValue::formula(text_value);
        break;
    }

    if (style_id.has_value()) {
        value = value.with_style(*style_id);
    }
    return value;
}

WorksheetInputChunkCallback cell_store_sheet_data_chunk_source(const CellStore& store)
{
    auto state = std::make_shared<CellStoreSheetDataChunkSourceState>(store);
    return [state = std::move(state)](std::string& output_chunk) mutable {
        return state->read(output_chunk);
    };
}

WorksheetInputChunkCallback cell_store_worksheet_chunk_source(const CellStore& store)
{
    auto state = std::make_shared<CellStoreWorksheetChunkSourceState>(store);
    return [state = std::move(state)](std::string& output_chunk) mutable {
        return state->read(output_chunk);
    };
}

std::string cell_store_dimension_reference(const CellStore& store)
{
    if (store.empty()) {
        return "A1";
    }

    std::uint32_t first_row = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t first_column = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t last_row = 0;
    std::uint32_t last_column = 0;

    for (const auto& [position, record] : store.records()) {
        (void)record;
        first_row = std::min(first_row, position.row);
        first_column = std::min(first_column, position.column);
        last_row = std::max(last_row, position.row);
        last_column = std::max(last_column, position.column);
    }

    return range_reference(first_row, first_column, last_row, last_column);
}

CellStore load_cell_store_from_worksheet_chunks(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    CellStoreWorksheetLoader loader(std::move(options));
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&loader](const WorksheetEvent& event) {
            loader.consume(event);
        },
        reader_options);
    return loader.finish();
}

CellStore load_cell_store_from_worksheet_xml(
    std::string_view worksheet_xml,
    CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    std::size_t offset = 0;
    const WorksheetInputChunkCallback read_next_chunk =
        [worksheet_xml, offset](std::string& output_chunk) mutable {
            if (offset >= worksheet_xml.size()) {
                output_chunk.clear();
                return false;
            }
            output_chunk.assign(worksheet_xml.data() + offset, worksheet_xml.size() - offset);
            offset = worksheet_xml.size();
            return true;
        };
    return load_cell_store_from_worksheet_chunks(
        read_next_chunk, std::move(options), reader_options);
}

CellStore load_cell_store_from_workbook_sheet(
    const PackageReader& reader, std::string_view sheet_name, CellStoreOptions options,
    WorksheetEventReaderOptions reader_options)
{
    const PartName worksheet_part = [&reader, sheet_name] {
        try {
            return reader.worksheet_part_by_sheet_name(sheet_name);
        } catch (const std::exception& error) {
            throw FastXlsxError("failed to resolve workbook sheet '" + std::string(sheet_name)
                + "' for CellStore loading: " + error.what());
        }
    }();
    const std::string worksheet_zip_entry = worksheet_part.zip_path();
    try {
        return load_cell_store_from_worksheet_chunks(
            reader.entry_chunk_source(worksheet_zip_entry),
            std::move(options), reader_options);
    } catch (const std::exception& error) {
        throw FastXlsxError("failed to load CellStore from workbook sheet '"
            + std::string(sheet_name) + "' worksheet part '" + worksheet_part.value()
            + "' ZIP entry '" + worksheet_zip_entry + "': " + error.what());
    }
}

CellStore::CellStore(CellStoreOptions options)
    : options_(std::move(options))
{
}

void CellStore::set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    validate_position(row, column);
    const CellPosition position {row, column};
    CellRecord record = CellRecord::from_value(value);
    const auto existing = cells_.find(position);
    const bool inserting_new_record = existing == cells_.end();
    const std::size_t next_cell_count = cells_.size() + (inserting_new_record ? 1U : 0U);

    if (options_.max_cells.has_value() && next_cell_count > *options_.max_cells) {
        throw FastXlsxError("CellStore max_cells guardrail exceeded");
    }

    if (options_.memory_budget_bytes.has_value()) {
        std::size_t next_memory_usage = estimated_memory_usage();
        if (!inserting_new_record) {
            next_memory_usage -= entry_memory_usage(existing->first, existing->second);
        }
        next_memory_usage += entry_memory_usage(position, record);
        if (next_memory_usage > *options_.memory_budget_bytes) {
            throw FastXlsxError("CellStore memory_budget_bytes guardrail exceeded");
        }
    }

    cells_[position] = std::move(record);
}

void CellStore::erase_cell(std::uint32_t row, std::uint32_t column)
{
    validate_position(row, column);
    cells_.erase(CellPosition {row, column});
}

const CellRecord* CellStore::try_cell(std::uint32_t row, std::uint32_t column) const
{
    validate_position(row, column);
    const auto iterator = cells_.find(CellPosition {row, column});
    if (iterator == cells_.end()) {
        return nullptr;
    }
    return &iterator->second;
}

const CellRecord* CellStore::find_cell(std::uint32_t row, std::uint32_t column) const
{
    return try_cell(row, column);
}

bool CellStore::empty() const noexcept
{
    return cells_.empty();
}

std::size_t CellStore::cell_count() const noexcept
{
    return cells_.size();
}

std::size_t CellStore::estimated_memory_usage() const noexcept
{
    std::size_t total = sizeof(CellStore);
    for (const auto& [position, record] : cells_) {
        total += entry_memory_usage(position, record);
    }
    return total;
}

const CellStoreOptions& CellStore::options() const noexcept
{
    return options_;
}

const std::map<CellPosition, CellRecord>& CellStore::records() const noexcept
{
    return cells_;
}

} // namespace fastxlsx::detail
