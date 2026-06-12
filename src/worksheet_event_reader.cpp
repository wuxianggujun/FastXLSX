#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <string>
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

std::size_t find_markup_end_or_npos(std::string_view xml, std::size_t open)
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

    return std::string_view::npos;
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

class WorksheetEventState {
public:
    WorksheetEventState(const WorksheetEventCallback& callback, bool copy_context_attributes)
        : callback_(callback)
        , copy_context_attributes_(copy_context_attributes)
    {
    }

    void emit_comment(std::string_view raw)
    {
        emit(WorksheetEvent { WorksheetEventKind::Comment, raw, {}, current_row_, current_cell_ });
    }

    void emit_processing_instruction(std::string_view raw)
    {
        const WorksheetEventKind kind = starts_with_at(raw, 0, "<?xml")
            ? WorksheetEventKind::XmlDeclaration
            : WorksheetEventKind::ProcessingInstruction;
        emit(WorksheetEvent { kind, raw, {}, current_row_, current_cell_ });
    }

    void emit_unsupported(std::string_view raw)
    {
        emit(WorksheetEvent { WorksheetEventKind::Unsupported,
            raw,
            {},
            current_row_,
            current_cell_,
            {},
            false });
    }

    void emit_text(std::string_view text)
    {
        if (text.empty()) {
            return;
        }

        const WorksheetEventKind kind =
            in_cell_value_ ? WorksheetEventKind::CellValue : WorksheetEventKind::RawText;
        emit(WorksheetEvent { kind,
            text,
            {},
            current_row_,
            current_cell_,
            in_cell_value_ ? text : std::string_view {},
            false });
    }

    void emit_tag(std::string_view raw)
    {
        const std::string_view name = element_name(raw);
        const bool closing = is_closing_tag(raw);
        const bool self_closing = is_self_closing_tag(raw);

        if (closing) {
            emit_closing_tag(raw, name);
            return;
        }

        emit_opening_tag(raw, name, self_closing);
    }

    void finish() const
    {
        if (!seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError("worksheet event reader requires a worksheet root");
        }
        if (!seen_worksheet_end_) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader requires a closing worksheet root");
        }
        if (in_sheet_data_ || in_row_ || in_cell_ || in_cell_value_) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader ended inside an open worksheet element");
        }
    }

private:
    void emit(WorksheetEvent event) const
    {
        callback_(event);
    }

    void set_current_row(std::string_view row_number)
    {
        if (copy_context_attributes_) {
            current_row_storage_ = std::string(row_number);
            current_row_ = current_row_storage_;
            return;
        }
        current_row_ = row_number;
    }

    void clear_current_row()
    {
        current_row_ = {};
        current_row_storage_.clear();
    }

    void set_current_cell(std::string_view cell_reference)
    {
        if (copy_context_attributes_) {
            current_cell_storage_ = std::string(cell_reference);
            current_cell_ = current_cell_storage_;
            return;
        }
        current_cell_ = cell_reference;
    }

    void clear_current_cell()
    {
        current_cell_ = {};
        current_cell_storage_.clear();
    }

    void emit_closing_tag(std::string_view raw, std::string_view name)
    {
        if (is_value_element(name) && in_cell_) {
            emit(WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                raw,
                name,
                current_row_,
                current_cell_ });
            in_cell_value_ = false;
        } else if (name == "c") {
            if (!in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found a closing cell without a start");
            }
            emit(WorksheetEvent { WorksheetEventKind::CellEnd,
                raw,
                name,
                current_row_,
                current_cell_ });
            in_cell_ = false;
            clear_current_cell();
        } else if (name == "row") {
            if (!in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid row boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::RowEnd,
                raw,
                name,
                current_row_,
                {},
                {},
                false });
            in_row_ = false;
            clear_current_row();
        } else if (name == "sheetData") {
            if (!in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid sheetData boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::SheetDataEnd, raw, name });
            in_sheet_data_ = false;
        } else if (name == "worksheet") {
            if (in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid worksheet boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::WorksheetEnd, raw, name });
            seen_worksheet_end_ = true;
        } else if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata, raw, name });
        }
    }

    void emit_opening_tag(std::string_view raw, std::string_view name, bool self_closing)
    {
        if (name == "worksheet") {
            if (seen_worksheet_start_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found duplicate worksheet root");
            }
            seen_worksheet_start_ = true;
            emit(WorksheetEvent { WorksheetEventKind::WorksheetStart,
                raw,
                name,
                {},
                {},
                {},
                self_closing });
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::WorksheetEnd,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    true });
                seen_worksheet_end_ = true;
            }
        } else if (name == "sheetData") {
            in_sheet_data_ = true;
            emit(WorksheetEvent { WorksheetEventKind::SheetDataStart,
                raw,
                name,
                {},
                {},
                {},
                self_closing });
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::SheetDataEnd,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    true });
                in_sheet_data_ = false;
            }
        } else if (name == "row") {
            if (!in_sheet_data_) {
                throw fastxlsx::FastXlsxError("worksheet event reader found row outside sheetData");
            }
            set_current_row(unqualified_attribute_value(raw, "r"));
            in_row_ = true;
            emit(WorksheetEvent { WorksheetEventKind::RowStart,
                raw,
                name,
                current_row_,
                {},
                {},
                self_closing });
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::RowEnd,
                    raw,
                    name,
                    current_row_,
                    {},
                    {},
                    true });
                in_row_ = false;
                clear_current_row();
            }
        } else if (name == "c") {
            if (!in_row_) {
                throw fastxlsx::FastXlsxError("worksheet event reader found cell outside row");
            }
            set_current_cell(unqualified_attribute_value(raw, "r"));
            in_cell_ = true;
            emit(WorksheetEvent { WorksheetEventKind::CellStart,
                raw,
                name,
                current_row_,
                current_cell_,
                {},
                self_closing });
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::CellEnd,
                    raw,
                    name,
                    current_row_,
                    current_cell_,
                    {},
                    true });
                in_cell_ = false;
                clear_current_cell();
            }
        } else if (is_value_element(name) && in_cell_) {
            emit(WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                raw,
                name,
                current_row_,
                current_cell_,
                {},
                self_closing });
            in_cell_value_ = !self_closing;
        } else if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata,
                raw,
                name,
                {},
                {},
                {},
                self_closing });
        }
    }

    const WorksheetEventCallback& callback_;
    bool copy_context_attributes_ = false;
    bool seen_worksheet_start_ = false;
    bool seen_worksheet_end_ = false;
    bool in_sheet_data_ = false;
    bool in_row_ = false;
    bool in_cell_ = false;
    bool in_cell_value_ = false;
    std::string current_row_storage_;
    std::string current_cell_storage_;
    std::string_view current_row_;
    std::string_view current_cell_;
};

std::size_t consume_available_events(
    std::string_view worksheet_xml, bool final_chunk, WorksheetEventState& state)
{
    std::size_t position = 0;
    while (position < worksheet_xml.size()) {
        const std::size_t open = worksheet_xml.find('<', position);
        if (open == std::string_view::npos) {
            if (final_chunk) {
                state.emit_text(worksheet_xml.substr(position));
                return worksheet_xml.size();
            }
            return position;
        }

        if (open > position) {
            state.emit_text(worksheet_xml.substr(position, open - position));
            position = open;
        }

        if (starts_with_at(worksheet_xml, position, "<!--")) {
            const std::size_t end = worksheet_xml.find("-->", position + 4);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an unterminated XML comment");
            }
            const std::string_view raw = worksheet_xml.substr(position, end + 3 - position);
            state.emit_comment(raw);
            position = end + 3;
            continue;
        }

        if (starts_with_at(worksheet_xml, position, "<?")) {
            const std::size_t end = worksheet_xml.find("?>", position + 2);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an unterminated processing instruction");
            }
            const std::string_view raw = worksheet_xml.substr(position, end + 2 - position);
            state.emit_processing_instruction(raw);
            position = end + 2;
            continue;
        }

        if (starts_with_at(worksheet_xml, position, "<!")) {
            const std::size_t end = find_markup_end_or_npos(worksheet_xml, position);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError("worksheet event reader found unterminated markup");
            }
            const std::string_view raw = worksheet_xml.substr(position, end + 1 - position);
            state.emit_unsupported(raw);
            position = end + 1;
            continue;
        }

        const std::size_t close = find_markup_end_or_npos(worksheet_xml, position);
        if (close == std::string_view::npos) {
            if (!final_chunk) {
                return position;
            }
            throw fastxlsx::FastXlsxError("worksheet event reader found unterminated markup");
        }
        const std::string_view raw = worksheet_xml.substr(position, close + 1 - position);
        state.emit_tag(raw);
        position = close + 1;
    }

    return position;
}

void erase_consumed_prefix(std::string& window, std::size_t consumed)
{
    if (consumed > 0) {
        window.erase(0, consumed);
    }
}

void process_window(std::string& window, bool final_chunk, WorksheetEventState& state)
{
    erase_consumed_prefix(window, consume_available_events(window, final_chunk, state));
}

} // namespace

namespace fastxlsx::detail {

void scan_worksheet_events(
    std::string_view worksheet_xml, const WorksheetEventCallback& callback)
{
    if (!callback) {
        throw FastXlsxError("worksheet event reader requires a callback");
    }

    WorksheetEventState state(callback, false);
    (void)consume_available_events(worksheet_xml, true, state);
    state.finish();
}

void scan_worksheet_events_from_chunks(
    std::span<const std::string_view> worksheet_xml_chunks,
    const WorksheetEventCallback& callback,
    WorksheetEventReaderOptions options)
{
    if (!callback) {
        throw FastXlsxError("worksheet event reader requires a callback");
    }
    if (options.max_window_bytes == 0) {
        throw FastXlsxError("worksheet event reader requires a nonzero input window limit");
    }

    WorksheetEventState state(callback, true);
    std::string window;
    window.reserve(std::min<std::size_t>(options.max_window_bytes, 4096U));

    for (std::string_view chunk : worksheet_xml_chunks) {
        std::size_t chunk_offset = 0;
        while (chunk_offset < chunk.size()) {
            process_window(window, false, state);
            if (window.size() >= options.max_window_bytes) {
                throw FastXlsxError("worksheet event reader exceeded bounded input window");
            }

            const std::size_t available = options.max_window_bytes - window.size();
            const std::size_t remaining = chunk.size() - chunk_offset;
            const std::size_t bytes_to_append = std::min(available, remaining);
            window.append(chunk.data() + chunk_offset, bytes_to_append);
            chunk_offset += bytes_to_append;
            process_window(window, false, state);

            if (bytes_to_append == 0 && !window.empty()) {
                throw FastXlsxError("worksheet event reader exceeded bounded input window");
            }
        }
    }

    process_window(window, true, state);
    state.finish();
}

} // namespace fastxlsx::detail
