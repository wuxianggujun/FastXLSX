#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventCallback;
using fastxlsx::detail::WorksheetEventKind;
using fastxlsx::detail::WorksheetEventReaderOptions;
using fastxlsx::detail::WorksheetEventWindowCallback;

bool starts_with_at(std::string_view text, std::size_t position, std::string_view prefix)
{
    return position <= text.size() && text.substr(position, prefix.size()) == prefix;
}

bool is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool is_xml_declaration(std::string_view raw)
{
    if (!starts_with_at(raw, 0, "<?xml")) {
        return false;
    }
    if (raw.size() <= 5) {
        return false;
    }

    const char after_target = raw[5];
    return is_space(after_target) || after_target == '?';
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

enum class CellValueElementKind {
    None,
    Value,
    Text,
    Formula,
};

CellValueElementKind cell_value_element_kind(std::string_view name) noexcept
{
    if (name == "v") {
        return CellValueElementKind::Value;
    }
    if (name == "t") {
        return CellValueElementKind::Text;
    }
    if (name == "f") {
        return CellValueElementKind::Formula;
    }
    return CellValueElementKind::None;
}

class WorksheetEventState {
public:
    WorksheetEventState(const WorksheetEventCallback& callback,
        WorksheetEventReaderOptions options)
        : callback_(callback)
        , copy_context_attributes_(options.copy_context_attributes)
        , coalesce_cell_value_events_(options.coalesce_cell_value_events)
        , telemetry_(options.telemetry)
    {
    }

    void emit_comment(std::string_view raw, std::uint64_t offset)
    {
        reject_markup_after_root();
        emit(WorksheetEvent { WorksheetEventKind::Comment, raw, {}, current_row_, current_cell_ },
            offset);
    }

    void emit_processing_instruction(std::string_view raw, std::uint64_t offset)
    {
        reject_markup_after_root();
        const bool xml_declaration = is_xml_declaration(raw);
        if (xml_declaration && seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found XML declaration after worksheet root");
        }
        const WorksheetEventKind kind = xml_declaration
            ? WorksheetEventKind::XmlDeclaration
            : WorksheetEventKind::ProcessingInstruction;
        emit(WorksheetEvent { kind, raw, {}, current_row_, current_cell_ }, offset);
    }

    void emit_unsupported(std::string_view raw, std::uint64_t offset)
    {
        reject_markup_outside_root();
        emit(WorksheetEvent { WorksheetEventKind::Unsupported,
            raw,
            {},
            current_row_,
            current_cell_,
            {},
            false },
            offset);
    }

    void emit_text(std::string_view text, std::uint64_t offset)
    {
        if (text.empty()) {
            return;
        }
        if (!seen_worksheet_start_ && has_non_whitespace(text)) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found text before worksheet root");
        }
        if (seen_worksheet_end_ && has_non_whitespace(text)) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found text after worksheet root");
        }

        const WorksheetEventKind kind =
            in_cell_value_ ? WorksheetEventKind::CellValue : WorksheetEventKind::RawText;
        emit(WorksheetEvent { kind,
            text,
            {},
            current_row_,
            current_cell_,
            in_cell_value_ ? text : std::string_view {},
            false },
            offset);
    }

    void emit_tag(std::string_view raw, std::uint64_t offset)
    {
        const std::string_view name = element_name(raw);
        const bool closing = is_closing_tag(raw);
        const bool self_closing = is_self_closing_tag(raw);
        if (name != "worksheet") {
            reject_markup_outside_root();
        } else {
            reject_markup_after_root();
        }

        if (closing) {
            emit_closing_tag(raw, name, offset);
            return;
        }

        emit_opening_tag(raw, name, self_closing, offset);
    }

    void flush_coalesced_event()
    {
        if (coalesced_raw_xml_.empty()) {
            return;
        }

        WorksheetEvent event {
            WorksheetEventKind::RawText,
            coalesced_raw_xml_,
        };
        event.raw_xml_offset = coalesced_raw_xml_offset_;
        callback_(event);
        if (telemetry_ != nullptr) {
            ++telemetry_->callback_event_count;
            ++telemetry_->coalesced_output_event_count;
        }
        coalesced_raw_xml_ = {};
        coalesced_raw_xml_offset_ = 0;
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
    [[nodiscard]] bool should_coalesce(const WorksheetEvent& event) const noexcept
    {
        if (!coalesce_cell_value_events_) {
            return false;
        }
        if (event.kind == WorksheetEventKind::CellValue) {
            return true;
        }
        return event.kind == WorksheetEventKind::CellValueMarkup
            && event.element_name != "f";
    }

    void append_coalesced_event(const WorksheetEvent& event, std::uint64_t offset)
    {
        if (!coalesced_raw_xml_.empty()) {
            const char* expected = coalesced_raw_xml_.data() + coalesced_raw_xml_.size();
            if (event.raw_xml.data() != expected
                || offset != coalesced_raw_xml_offset_ + coalesced_raw_xml_.size()) {
                flush_coalesced_event();
            }
        }
        if (coalesced_raw_xml_.empty()) {
            coalesced_raw_xml_ = event.raw_xml;
            coalesced_raw_xml_offset_ = offset;
        } else {
            coalesced_raw_xml_ = std::string_view(
                coalesced_raw_xml_.data(), coalesced_raw_xml_.size() + event.raw_xml.size());
        }
        if (telemetry_ != nullptr) {
            ++telemetry_->coalesced_input_event_count;
        }
    }

    void emit(WorksheetEvent event, std::uint64_t offset)
    {
        if (telemetry_ != nullptr) {
            ++telemetry_->parsed_event_count;
        }
        if (should_coalesce(event)) {
            append_coalesced_event(event, offset);
            return;
        }
        flush_coalesced_event();
        event.raw_xml_offset = offset;
        callback_(event);
        if (telemetry_ != nullptr) {
            ++telemetry_->callback_event_count;
        }
    }

    void reject_markup_after_root() const
    {
        if (seen_worksheet_end_) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found markup after worksheet root");
        }
    }

    void reject_markup_outside_root() const
    {
        if (!seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader found markup before worksheet root");
        }
        reject_markup_after_root();
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

    void clear_transient_row_context()
    {
        if (!copy_context_attributes_) {
            current_row_ = {};
        }
    }

    void clear_transient_cell_context()
    {
        if (!copy_context_attributes_) {
            current_cell_ = {};
        }
    }

    void clear_current_cell_value()
    {
        in_cell_value_ = false;
        current_cell_value_element_ = CellValueElementKind::None;
    }

    void emit_closing_tag(
        std::string_view raw, std::string_view name, std::uint64_t offset)
    {
        if (is_value_element(name) && in_cell_) {
            if (!in_cell_value_
                || current_cell_value_element_ != cell_value_element_kind(name)) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found a mismatched cell value boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                raw,
                name,
                current_row_,
                current_cell_ },
                offset);
            clear_current_cell_value();
        } else if (name == "c") {
            if (!in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found a closing cell without a start");
            }
            if (in_cell_value_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found a cell boundary inside an open cell value");
            }
            emit(WorksheetEvent { WorksheetEventKind::CellEnd,
                raw,
                name,
                current_row_,
                current_cell_ },
                offset);
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
                false },
                offset);
            in_row_ = false;
            clear_current_row();
        } else if (name == "sheetData") {
            if (!in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid sheetData boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::SheetDataEnd, raw, name }, offset);
            in_sheet_data_ = false;
        } else if (name == "worksheet") {
            if (in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid worksheet boundary");
            }
            emit(WorksheetEvent { WorksheetEventKind::WorksheetEnd, raw, name }, offset);
            seen_worksheet_end_ = true;
        } else if (in_cell_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata,
                raw,
                name,
                current_row_,
                current_cell_ },
                offset);
        } else if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata, raw, name }, offset);
        }
    }

    void emit_opening_tag(
        std::string_view raw, std::string_view name, bool self_closing, std::uint64_t offset)
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
                self_closing },
                offset);
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::WorksheetEnd,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    true },
                    offset);
                seen_worksheet_end_ = true;
            }
        } else if (name == "sheetData") {
            if (seen_sheet_data_ || in_sheet_data_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid sheetData boundary");
            }
            seen_sheet_data_ = true;
            in_sheet_data_ = true;
            emit(WorksheetEvent { WorksheetEventKind::SheetDataStart,
                raw,
                name,
                {},
                {},
                {},
                self_closing },
                offset);
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::SheetDataEnd,
                    raw,
                    name,
                    {},
                    {},
                    {},
                    true },
                    offset);
                in_sheet_data_ = false;
            }
        } else if (name == "row") {
            if (!in_sheet_data_) {
                throw fastxlsx::FastXlsxError("worksheet event reader found row outside sheetData");
            }
            if (in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid row boundary");
            }
            set_current_row(unqualified_attribute_value(raw, "r"));
            in_row_ = true;
            emit(WorksheetEvent { WorksheetEventKind::RowStart,
                raw,
                name,
                current_row_,
                {},
                {},
                self_closing },
                offset);
            clear_transient_row_context();
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::RowEnd,
                    raw,
                    name,
                    current_row_,
                    {},
                    {},
                    true },
                    offset);
                in_row_ = false;
                clear_current_row();
            }
        } else if (name == "c") {
            if (!in_row_) {
                throw fastxlsx::FastXlsxError("worksheet event reader found cell outside row");
            }
            if (in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an invalid cell boundary");
            }
            set_current_cell(unqualified_attribute_value(raw, "r"));
            in_cell_ = true;
            emit(WorksheetEvent { WorksheetEventKind::CellStart,
                raw,
                name,
                current_row_,
                current_cell_,
                {},
                self_closing },
                offset);
            clear_transient_cell_context();
            if (self_closing) {
                emit(WorksheetEvent { WorksheetEventKind::CellEnd,
                    raw,
                    name,
                    current_row_,
                    current_cell_,
                    {},
                    true },
                    offset);
                in_cell_ = false;
                clear_current_cell();
            }
        } else if (is_value_element(name) && in_cell_) {
            if (in_cell_value_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found nested cell value markup");
            }
            emit(WorksheetEvent { WorksheetEventKind::CellValueMarkup,
                raw,
                name,
                current_row_,
                current_cell_,
                {},
                self_closing },
                offset);
            if (!self_closing) {
                in_cell_value_ = true;
                current_cell_value_element_ = cell_value_element_kind(name);
            }
        } else if (in_cell_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata,
                raw,
                name,
                current_row_,
                current_cell_,
                {},
                self_closing },
                offset);
        } else if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_) {
            emit(WorksheetEvent { WorksheetEventKind::Metadata,
                raw,
                name,
                {},
                {},
                {},
                self_closing },
                offset);
        }
    }

    const WorksheetEventCallback& callback_;
    bool copy_context_attributes_ = false;
    bool seen_worksheet_start_ = false;
    bool seen_worksheet_end_ = false;
    bool seen_sheet_data_ = false;
    bool in_sheet_data_ = false;
    bool in_row_ = false;
    bool in_cell_ = false;
    bool in_cell_value_ = false;
    std::string current_row_storage_;
    std::string current_cell_storage_;
    CellValueElementKind current_cell_value_element_ = CellValueElementKind::None;
    std::string_view current_row_;
    std::string_view current_cell_;
    bool coalesce_cell_value_events_ = false;
    fastxlsx::detail::WorksheetEventReaderTelemetry* telemetry_ = nullptr;
    std::string_view coalesced_raw_xml_;
    std::uint64_t coalesced_raw_xml_offset_ = 0;
};

std::uint64_t add_source_offset(std::uint64_t base, std::size_t relative)
{
    if (static_cast<std::uint64_t>(relative)
        > std::numeric_limits<std::uint64_t>::max() - base) {
        throw fastxlsx::FastXlsxError("worksheet event reader source offset overflow");
    }
    return base + static_cast<std::uint64_t>(relative);
}

std::size_t consume_available_events(std::string_view xml_window,
    bool final_chunk,
    WorksheetEventState& state,
    std::uint64_t window_begin_offset)
{
    std::size_t position = 0;
    while (position < xml_window.size()) {
        const std::size_t open = xml_window.find('<', position);
        if (open == std::string_view::npos) {
            if (final_chunk) {
                state.emit_text(
                    xml_window.substr(position),
                    add_source_offset(window_begin_offset, position));
                return xml_window.size();
            }
            return position;
        }

        if (open > position) {
            state.emit_text(xml_window.substr(position, open - position),
                add_source_offset(window_begin_offset, position));
            position = open;
        }

        if (starts_with_at(xml_window, position, "<!--")) {
            const std::size_t end = xml_window.find("-->", position + 4);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an unterminated XML comment");
            }
            const std::string_view raw = xml_window.substr(position, end + 3 - position);
            state.emit_comment(raw, add_source_offset(window_begin_offset, position));
            position = end + 3;
            continue;
        }

        if (starts_with_at(xml_window, position, "<?")) {
            const std::size_t end = xml_window.find("?>", position + 2);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet event reader found an unterminated processing instruction");
            }
            const std::string_view raw = xml_window.substr(position, end + 2 - position);
            state.emit_processing_instruction(raw,
                add_source_offset(window_begin_offset, position));
            position = end + 2;
            continue;
        }

        if (starts_with_at(xml_window, position, "<!")) {
            const std::size_t end = find_markup_end_or_npos(xml_window, position);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError("worksheet event reader found unterminated markup");
            }
            const std::string_view raw = xml_window.substr(position, end + 1 - position);
            state.emit_unsupported(raw, add_source_offset(window_begin_offset, position));
            position = end + 1;
            continue;
        }

        const std::size_t close = find_markup_end_or_npos(xml_window, position);
        if (close == std::string_view::npos) {
            if (!final_chunk) {
                return position;
            }
            throw fastxlsx::FastXlsxError("worksheet event reader found unterminated markup");
        }
        const std::string_view raw = xml_window.substr(position, close + 1 - position);
        state.emit_tag(raw, add_source_offset(window_begin_offset, position));
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

void process_window(
    std::string& window,
    bool final_chunk,
    WorksheetEventState& state,
    std::uint64_t& window_begin_offset,
    const WorksheetEventWindowCallback& window_consumed_callback)
{
    const std::size_t consumed =
        consume_available_events(window, final_chunk, state, window_begin_offset);
    state.flush_coalesced_event();
    if (window_consumed_callback) {
        window_consumed_callback();
    }
    erase_consumed_prefix(window, consumed);
    window_begin_offset = add_source_offset(window_begin_offset, consumed);
}

void process_source_chunk(std::string_view chunk,
    WorksheetEventReaderOptions options,
    std::string& window,
    WorksheetEventState& state,
    std::uint64_t& window_begin_offset,
    const WorksheetEventWindowCallback& window_consumed_callback)
{
    std::size_t chunk_offset = 0;
    while (chunk_offset < chunk.size()) {
        process_window(
            window, false, state, window_begin_offset, window_consumed_callback);
        if (window.size() >= options.max_window_bytes) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader exceeded bounded input window");
        }

        const std::size_t available = options.max_window_bytes - window.size();
        const std::size_t remaining = chunk.size() - chunk_offset;
        const std::size_t bytes_to_append = std::min(available, remaining);
        window.append(chunk.data() + chunk_offset, bytes_to_append);
        chunk_offset += bytes_to_append;
        process_window(
            window, false, state, window_begin_offset, window_consumed_callback);

        if (bytes_to_append == 0 && !window.empty()) {
            throw fastxlsx::FastXlsxError(
                "worksheet event reader exceeded bounded input window");
        }
    }
}

} // namespace

namespace fastxlsx::detail {

void scan_worksheet_events_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetEventCallback& callback,
    WorksheetEventReaderOptions options,
    const WorksheetEventWindowCallback& window_consumed_callback)
{
    if (!read_next_chunk) {
        throw FastXlsxError("worksheet event reader requires a chunk source");
    }
    if (!callback) {
        throw FastXlsxError("worksheet event reader requires a callback");
    }
    if (options.max_window_bytes == 0) {
        throw FastXlsxError("worksheet event reader requires a nonzero input window limit");
    }

    WorksheetEventState state(callback, options);
    std::string window;
    window.reserve(std::min<std::size_t>(options.max_window_bytes, 4096U));
    std::uint64_t window_begin_offset = 0;

    std::string chunk;
    while (read_next_chunk(chunk)) {
        process_source_chunk(chunk, options, window, state, window_begin_offset,
            window_consumed_callback);
    }

    process_window(window, true, state, window_begin_offset, window_consumed_callback);
    state.finish();
}

} // namespace fastxlsx::detail
