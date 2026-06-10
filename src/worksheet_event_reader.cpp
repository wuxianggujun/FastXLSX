#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <fastxlsx/workbook.hpp>

#include <cctype>
#include <string_view>

namespace {

using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventCallback;
using fastxlsx::detail::WorksheetEventKind;

bool starts_with_at(std::string_view text, std::size_t position, std::string_view prefix)
{
    return position <= text.size() && text.substr(position, prefix.size()) == prefix;
}

bool is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
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

std::string_view local_name(std::string_view name)
{
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return name;
    }
    return name.substr(colon + 1);
}

std::size_t find_markup_end(std::string_view xml, std::size_t open)
{
    char quote = '\0';
    for (std::size_t index = open + 1; index < xml.size(); ++index) {
        const char ch = xml[index];
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
            return index;
        }
    }

    throw fastxlsx::FastXlsxError("worksheet event reader found unterminated markup");
}

std::string_view tag_body(std::string_view raw_tag)
{
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

std::string_view element_name(std::string_view raw_tag)
{
    std::string_view body = tag_body(raw_tag);
    if (body.empty()) {
        throw fastxlsx::FastXlsxError("worksheet event reader found an empty XML tag");
    }

    std::size_t end = 0;
    while (end < body.size() && !is_space(body[end]) && body[end] != '/' && body[end] != '?') {
        ++end;
    }
    return local_name(body.substr(0, end));
}

bool is_closing_tag(std::string_view raw_tag)
{
    return raw_tag.size() > 2 && raw_tag[1] == '/';
}

bool is_self_closing_tag(std::string_view raw_tag)
{
    if (is_closing_tag(raw_tag)) {
        return false;
    }

    std::size_t index = raw_tag.size() - 2;
    while (index > 0 && is_space(raw_tag[index])) {
        --index;
    }
    return raw_tag[index] == '/';
}

std::string_view unqualified_attribute_value(
    std::string_view raw_tag, std::string_view attribute_name)
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
            return {};
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
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found an unquoted attribute value");
        }

        const char quote = body[position];
        ++position;
        const std::size_t value_begin = position;
        while (position < body.size() && body[position] != quote) {
            ++position;
        }
        if (position >= body.size()) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found an unterminated attribute value");
        }

        if (name == attribute_name) {
            return body.substr(value_begin, position - value_begin);
        }
        ++position;
    }

    return {};
}

bool has_non_whitespace(std::string_view value)
{
    for (const char ch : value) {
        if (!is_space(ch)) {
            return true;
        }
    }
    return false;
}

bool is_value_element(std::string_view name)
{
    return name == "v" || name == "t" || name == "f";
}

void emit(const WorksheetEventCallback& callback, WorksheetEvent event)
{
    callback(event);
}

void emit_text_segment(const WorksheetEventCallback& callback,
    std::string_view text,
    std::string_view current_row,
    std::string_view current_cell,
    bool in_cell_value)
{
    if (text.empty()) {
        return;
    }

    const WorksheetEventKind kind =
        in_cell_value ? WorksheetEventKind::CellValue : WorksheetEventKind::RawText;
    emit(callback,
        WorksheetEvent { kind,
            text,
            {},
            current_row,
            current_cell,
            in_cell_value ? text : std::string_view {},
            false });
}

} // namespace

namespace fastxlsx::detail {

void scan_worksheet_events(
    std::string_view worksheet_xml, const WorksheetEventCallback& callback)
{
    if (!callback) {
        throw FastXlsxError("worksheet event reader requires a callback");
    }

    bool seen_worksheet_start = false;
    bool seen_worksheet_end = false;
    bool in_sheet_data = false;
    bool in_row = false;
    bool in_cell = false;
    bool in_cell_value = false;

    std::string_view current_row;
    std::string_view current_cell;

    std::size_t position = 0;
    while (position < worksheet_xml.size()) {
        const std::size_t open = worksheet_xml.find('<', position);
        if (open == std::string_view::npos) {
            emit_text_segment(callback,
                worksheet_xml.substr(position),
                current_row,
                current_cell,
                in_cell_value);
            break;
        }

        if (open > position) {
            emit_text_segment(callback,
                worksheet_xml.substr(position, open - position),
                current_row,
                current_cell,
                in_cell_value);
        }

        if (starts_with_at(worksheet_xml, open, "<!--")) {
            const std::size_t end = worksheet_xml.find("-->", open + 4);
            if (end == std::string_view::npos) {
                throw FastXlsxError("worksheet event reader found an unterminated XML comment");
            }
            const std::string_view raw = worksheet_xml.substr(open, end + 3 - open);
            emit(callback,
                WorksheetEvent { WorksheetEventKind::Comment, raw, {}, current_row, current_cell });
            position = end + 3;
            continue;
        }

        if (starts_with_at(worksheet_xml, open, "<?")) {
            const std::size_t end = worksheet_xml.find("?>", open + 2);
            if (end == std::string_view::npos) {
                throw FastXlsxError(
                    "worksheet event reader found an unterminated processing instruction");
            }
            const std::string_view raw = worksheet_xml.substr(open, end + 2 - open);
            const WorksheetEventKind kind =
                starts_with_at(raw, 0, "<?xml") ? WorksheetEventKind::XmlDeclaration
                                                : WorksheetEventKind::ProcessingInstruction;
            emit(callback, WorksheetEvent { kind, raw, {}, current_row, current_cell });
            position = end + 2;
            continue;
        }

        if (starts_with_at(worksheet_xml, open, "<!")) {
            const std::size_t end = find_markup_end(worksheet_xml, open);
            const std::string_view raw = worksheet_xml.substr(open, end + 1 - open);
            emit(callback,
                WorksheetEvent { WorksheetEventKind::Unsupported,
                    raw,
                    {},
                    current_row,
                    current_cell,
                    {},
                    false });
            position = end + 1;
            continue;
        }

        const std::size_t close = find_markup_end(worksheet_xml, open);
        const std::string_view raw = worksheet_xml.substr(open, close + 1 - open);
        const std::string_view name = element_name(raw);
        const bool closing = is_closing_tag(raw);
        const bool self_closing = is_self_closing_tag(raw);

        if (closing) {
            if (is_value_element(name) && in_cell) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                        raw,
                        name,
                        current_row,
                        current_cell });
                in_cell_value = false;
            } else if (name == "c") {
                if (!in_cell) {
                    throw FastXlsxError("worksheet event reader found a closing cell without a start");
                }
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::CellEnd,
                        raw,
                        name,
                        current_row,
                        current_cell });
                in_cell = false;
                current_cell = {};
            } else if (name == "row") {
                if (!in_row || in_cell) {
                    throw FastXlsxError("worksheet event reader found an invalid row boundary");
                }
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::RowEnd,
                        raw,
                        name,
                        current_row,
                        {},
                        {},
                        false });
                in_row = false;
                current_row = {};
            } else if (name == "sheetData") {
                if (!in_sheet_data || in_row || in_cell) {
                    throw FastXlsxError("worksheet event reader found an invalid sheetData boundary");
                }
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::SheetDataEnd, raw, name });
                in_sheet_data = false;
            } else if (name == "worksheet") {
                if (in_sheet_data || in_row || in_cell) {
                    throw FastXlsxError("worksheet event reader found an invalid worksheet boundary");
                }
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::WorksheetEnd, raw, name });
                seen_worksheet_end = true;
            } else if (seen_worksheet_start && !seen_worksheet_end && !in_sheet_data) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::Metadata, raw, name });
            }

            position = close + 1;
            continue;
        }

        if (name == "worksheet") {
            if (seen_worksheet_start) {
                throw FastXlsxError("worksheet event reader found duplicate worksheet root");
            }
            seen_worksheet_start = true;
            emit(callback,
                WorksheetEvent { WorksheetEventKind::WorksheetStart,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    self_closing });
            if (self_closing) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::WorksheetEnd,
                        raw,
                        name,
                        {},
                        {},
                        {},
                        true });
                seen_worksheet_end = true;
            }
        } else if (name == "sheetData") {
            in_sheet_data = true;
            emit(callback,
                WorksheetEvent { WorksheetEventKind::SheetDataStart,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    self_closing });
            if (self_closing) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::SheetDataEnd,
                        raw,
                        name,
                        {},
                        {},
                        {},
                        true });
                in_sheet_data = false;
            }
        } else if (name == "row") {
            if (!in_sheet_data) {
                throw FastXlsxError("worksheet event reader found row outside sheetData");
            }
            current_row = unqualified_attribute_value(raw, "r");
            in_row = true;
            emit(callback,
                WorksheetEvent { WorksheetEventKind::RowStart,
                    raw,
                    name,
                    current_row,
                    {},
                    {},
                    self_closing });
            if (self_closing) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::RowEnd,
                        raw,
                        name,
                        current_row,
                        {},
                        {},
                        true });
                in_row = false;
                current_row = {};
            }
        } else if (name == "c") {
            if (!in_row) {
                throw FastXlsxError("worksheet event reader found cell outside row");
            }
            current_cell = unqualified_attribute_value(raw, "r");
            in_cell = true;
            emit(callback,
                WorksheetEvent { WorksheetEventKind::CellStart,
                    raw,
                    name,
                    current_row,
                    current_cell,
                    {},
                    self_closing });
            if (self_closing) {
                emit(callback,
                    WorksheetEvent { WorksheetEventKind::CellEnd,
                        raw,
                        name,
                        current_row,
                        current_cell,
                        {},
                        true });
                in_cell = false;
                current_cell = {};
            }
        } else if (is_value_element(name) && in_cell) {
            emit(callback,
                WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                    raw,
                    name,
                    current_row,
                    current_cell,
                    {},
                    self_closing });
            in_cell_value = !self_closing;
        } else if (seen_worksheet_start && !seen_worksheet_end && !in_sheet_data) {
            emit(callback,
                WorksheetEvent { WorksheetEventKind::Metadata,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    self_closing });
        }

        position = close + 1;
    }

    if (!seen_worksheet_start) {
        throw FastXlsxError("worksheet event reader requires a worksheet root");
    }
    if (!seen_worksheet_end) {
        throw FastXlsxError("worksheet event reader requires a closing worksheet root");
    }
    if (in_sheet_data || in_row || in_cell || in_cell_value) {
        throw FastXlsxError("worksheet event reader ended inside an open worksheet element");
    }
}

} // namespace fastxlsx::detail
