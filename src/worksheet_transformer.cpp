#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using fastxlsx::detail::WorksheetCellReplacement;
using fastxlsx::detail::WorksheetCellReplacementCoordinate;
using fastxlsx::detail::WorksheetCellReplacementPayload;
using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventKind;
using fastxlsx::detail::WorksheetCellReplacementPlan;
using fastxlsx::detail::WorksheetCellReplacementMode;
using fastxlsx::detail::WorksheetCellReplacementTarget;
using fastxlsx::detail::WorksheetOutputChunkCallback;
using fastxlsx::detail::WorksheetTransformAction;
using fastxlsx::detail::WorksheetTransformActionCallback;
using fastxlsx::detail::WorksheetTransformActionKind;
using fastxlsx::detail::WorksheetTransformSummary;

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;

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

std::uint32_t uppercase_column_value(char ch)
{
    const char upper = ch >= 'a' && ch <= 'z'
        ? static_cast<char>(ch - ('a' - 'A'))
        : ch;
    return static_cast<std::uint32_t>(upper - 'A' + 1);
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

WorksheetCellReplacementCoordinate parse_cell_reference_coordinate(
    std::string_view reference, std::string_view context)
{
    if (reference.empty()) {
        throw fastxlsx::FastXlsxError(
            std::string(context) + " requires a cell reference");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > max_excel_columns) {
            throw fastxlsx::FastXlsxError(
                std::string(context) + " cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw fastxlsx::FastXlsxError(
            std::string(context) + " found an invalid cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > max_excel_rows) {
            throw fastxlsx::FastXlsxError(
                std::string(context) + " cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError(
            std::string(context) + " found an invalid cell reference");
    }

    return WorksheetCellReplacementCoordinate {
        static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column),
    };
}

std::optional<std::uint32_t> parse_row_number(std::string_view row_number)
{
    if (row_number.empty()) {
        return std::nullopt;
    }

    std::uint64_t row = 0;
    for (const char ch : row_number) {
        if (!is_ascii_digit(ch)) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer upsert found an invalid row reference");
        }
        row = row * 10U + static_cast<std::uint32_t>(ch - '0');
        if (row > max_excel_rows) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer upsert row exceeds Excel limits");
        }
    }
    if (row == 0) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer upsert found an invalid row reference");
    }
    return static_cast<std::uint32_t>(row);
}

void require_replacement_cell_payload_size(const fastxlsx::detail::WorksheetCellReplacementPayload& payload)
{
    if (payload.byte_size()
        <= fastxlsx::detail::worksheet_replacement_cell_xml_materialization_byte_limit) {
        return;
    }

    throw fastxlsx::FastXlsxError(
        "worksheet transformer replacement cell XML exceeds the single-cell materialized "
        "payload limit");
}

std::size_t find_start_tag_end(std::string_view xml, std::size_t start)
{
    char quote = '\0';
    for (std::size_t index = start + 1; index < xml.size(); ++index) {
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

std::string_view xml_local_name(std::string_view qualified_name)
{
    const std::size_t colon = qualified_name.rfind(':');
    if (colon == std::string_view::npos) {
        return qualified_name;
    }
    return qualified_name.substr(colon + 1);
}

std::string_view parse_tag_name(
    std::string_view xml,
    std::size_t name_start,
    std::size_t tag_end)
{
    while (name_start < tag_end && is_space(xml[name_start])) {
        ++name_start;
    }
    std::size_t name_end = name_start;
    while (name_end < tag_end && !is_space(xml[name_end])
           && xml[name_end] != '/' && xml[name_end] != '>') {
        ++name_end;
    }
    if (name_end == name_start) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML tag name is missing");
    }
    return xml.substr(name_start, name_end - name_start);
}

std::string_view qualified_element_name(std::string_view raw_xml)
{
    if (raw_xml.empty() || raw_xml.front() != '<') {
        return {};
    }

    std::size_t position = 1;
    if (position < raw_xml.size() && raw_xml[position] == '/') {
        ++position;
    }
    const std::size_t name_begin = position;
    while (position < raw_xml.size() && !is_space(raw_xml[position])
           && raw_xml[position] != '/' && raw_xml[position] != '>'
           && raw_xml[position] != '?') {
        ++position;
    }
    return raw_xml.substr(name_begin, position - name_begin);
}

std::string element_prefix(std::string_view raw_xml)
{
    const std::string_view name = qualified_element_name(raw_xml);
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return {};
    }
    return std::string(name.substr(0, colon));
}

std::string self_closing_tag_as_open_tag(std::string_view raw_xml)
{
    std::string open(raw_xml);
    const std::size_t close = open.rfind('>');
    if (close == std::string::npos) {
        return open;
    }
    std::size_t slash = close;
    while (slash > 0 && is_space(open[slash - 1])) {
        --slash;
    }
    if (slash > 0 && open[slash - 1] == '/') {
        open.erase(slash - 1, 1);
    }
    return open;
}

std::string closing_tag_from_start_tag(std::string_view raw_xml)
{
    std::string close = "</";
    close += qualified_element_name(raw_xml);
    close += '>';
    return close;
}

std::string prefixed_name(std::string_view prefix, std::string_view local_name)
{
    std::string name;
    if (!prefix.empty()) {
        name += prefix;
        name += ':';
    }
    name += local_name;
    return name;
}

std::string synthetic_row_start_tag(std::string_view prefix, std::uint32_t row)
{
    std::string tag = "<";
    tag += prefixed_name(prefix, "row");
    tag += " r=\"";
    tag += std::to_string(row);
    tag += "\">";
    return tag;
}

std::string synthetic_start_tag(std::string_view prefix, std::string_view local_name)
{
    std::string tag = "<";
    tag += prefixed_name(prefix, local_name);
    tag += '>';
    return tag;
}

std::string synthetic_end_tag(std::string_view prefix, std::string_view local_name)
{
    std::string tag = "</";
    tag += prefixed_name(prefix, local_name);
    tag += '>';
    return tag;
}

bool start_tag_is_self_closing(std::string_view xml, std::size_t tag_end)
{
    std::size_t cursor = tag_end;
    while (cursor > 0 && is_space(xml[cursor - 1])) {
        --cursor;
    }
    return cursor > 0 && xml[cursor - 1] == '/';
}

void validate_no_non_whitespace_outside_root(std::string_view text)
{
    if (has_non_whitespace(text)) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML has text outside the root element");
    }
}

void validate_replacement_cell_xml_structure(std::string_view xml)
{
    std::vector<std::string_view> open_elements;
    bool saw_root = false;
    bool root_closed = false;
    std::size_t cursor = 0;

    while (cursor < xml.size()) {
        const std::size_t open = xml.find('<', cursor);
        if (open == std::string_view::npos) {
            if (open_elements.empty()) {
                validate_no_non_whitespace_outside_root(xml.substr(cursor));
            }
            break;
        }
        if (open > cursor && open_elements.empty()) {
            validate_no_non_whitespace_outside_root(xml.substr(cursor, open - cursor));
        }
        cursor = open;

        if (xml.substr(cursor, 4) == "<!--") {
            const std::size_t close = xml.find("-->", cursor + 4);
            if (close == std::string_view::npos) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML comment is not closed");
            }
            cursor = close + 3;
            continue;
        }
        if (xml.substr(cursor, 9) == "<![CDATA[") {
            if (open_elements.empty()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML has CDATA outside the root element");
            }
            const std::size_t close = xml.find("]]>", cursor + 9);
            if (close == std::string_view::npos) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML CDATA is not closed");
            }
            cursor = close + 3;
            continue;
        }
        if (xml.substr(cursor, 2) == "<?") {
            const std::size_t close = xml.find("?>", cursor + 2);
            if (close == std::string_view::npos) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML processing instruction is not closed");
            }
            cursor = close + 2;
            continue;
        }
        if (xml.substr(cursor, 2) == "<!") {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML contains unsupported markup");
        }

        const std::size_t tag_end = find_start_tag_end(xml, cursor);
        if (tag_end == std::string_view::npos) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML tag is truncated");
        }

        if (cursor + 1 < xml.size() && xml[cursor + 1] == '/') {
            const std::string_view closing_name =
                parse_tag_name(xml, cursor + 2, tag_end);
            if (open_elements.empty()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML closing tag has no matching start tag");
            }
            if (xml_local_name(open_elements.back()) != xml_local_name(closing_name)) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML closing tag does not match start tag");
            }
            open_elements.pop_back();
            if (open_elements.empty()) {
                root_closed = true;
            }
            cursor = tag_end + 1;
            continue;
        }

        if (root_closed) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has multiple root elements");
        }

        const std::string_view start_name = parse_tag_name(xml, cursor + 1, tag_end);
        if (!saw_root) {
            if (xml_local_name(start_name) != "c") {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML root must be a cell element");
            }
            saw_root = true;
        }

        if (start_tag_is_self_closing(xml, tag_end)) {
            if (open_elements.empty()) {
                root_closed = true;
            }
        } else {
            open_elements.push_back(start_name);
        }
        cursor = tag_end + 1;
    }

    if (!saw_root) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }
    if (!open_elements.empty()) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML closing tag is missing");
    }
}

std::string_view parse_replacement_cell_reference(
    std::string_view materialized_replacement_cell_xml)
{
    std::size_t cursor = 0;
    while (cursor < materialized_replacement_cell_xml.size()
        && is_space(materialized_replacement_cell_xml[cursor])) {
        ++cursor;
    }

    if (cursor >= materialized_replacement_cell_xml.size()
        || materialized_replacement_cell_xml[cursor] != '<'
        || cursor + 1 >= materialized_replacement_cell_xml.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const char first_tag_char = materialized_replacement_cell_xml[cursor + 1];
    if (first_tag_char == '/' || first_tag_char == '?' || first_tag_char == '!') {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const std::size_t tag_end =
        find_start_tag_end(materialized_replacement_cell_xml, cursor);
    if (tag_end == std::string_view::npos) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML has an unterminated start tag");
    }

    std::size_t name_start = cursor + 1;
    std::size_t name_end = name_start;
    while (name_end < tag_end && !is_space(materialized_replacement_cell_xml[name_end])
           && materialized_replacement_cell_xml[name_end] != '/') {
        ++name_end;
    }
    if (name_end == name_start) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const std::string_view qualified_name =
        materialized_replacement_cell_xml.substr(name_start, name_end - name_start);
    if (xml_local_name(qualified_name) != "c") {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML root must be a cell element");
    }

    bool saw_cell_reference = false;
    std::string_view cell_reference;
    cursor = name_end;
    while (cursor < tag_end) {
        while (cursor < tag_end && is_space(materialized_replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end) {
            break;
        }
        if (materialized_replacement_cell_xml[cursor] == '/') {
            ++cursor;
            while (cursor < tag_end && is_space(materialized_replacement_cell_xml[cursor])) {
                ++cursor;
            }
            if (cursor != tag_end) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML has malformed attributes");
            }
            break;
        }

        const std::size_t attribute_name_start = cursor;
        while (cursor < tag_end && !is_space(materialized_replacement_cell_xml[cursor])
               && materialized_replacement_cell_xml[cursor] != '='
               && materialized_replacement_cell_xml[cursor] != '/') {
            ++cursor;
        }
        if (cursor == attribute_name_start) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has malformed attributes");
        }
        const std::string_view attribute_name =
            materialized_replacement_cell_xml.substr(
                attribute_name_start, cursor - attribute_name_start);

        while (cursor < tag_end && is_space(materialized_replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end || materialized_replacement_cell_xml[cursor] != '=') {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has malformed attributes");
        }
        ++cursor;
        while (cursor < tag_end && is_space(materialized_replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end
            || (materialized_replacement_cell_xml[cursor] != '"'
                && materialized_replacement_cell_xml[cursor] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML attributes must be quoted");
        }

        const char quote = materialized_replacement_cell_xml[cursor];
        const std::size_t attribute_value_start = ++cursor;
        while (cursor < tag_end && materialized_replacement_cell_xml[cursor] != quote) {
            ++cursor;
        }
        if (cursor >= tag_end) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has an unterminated attribute");
        }
        const std::string_view attribute_value =
            materialized_replacement_cell_xml.substr(
                attribute_value_start, cursor - attribute_value_start);
        ++cursor;

        if (attribute_name == "r") {
            if (saw_cell_reference) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML has duplicate r attributes");
            }
            saw_cell_reference = true;
            cell_reference = attribute_value;
        }
    }

    if (!saw_cell_reference) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must include an r attribute");
    }
    return cell_reference;
}

void validate_replacement_cell_payload(
    const WorksheetCellReplacement& replacement,
    std::string_view materialized_replacement_cell_xml)
{
    const std::string_view replacement_cell_reference =
        parse_replacement_cell_reference(materialized_replacement_cell_xml);
    if (replacement_cell_reference != replacement.cell_reference) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML r attribute must match its selector");
    }
    validate_replacement_cell_xml_structure(materialized_replacement_cell_xml);
}

void emit_pass_through(const WorksheetTransformActionCallback& callback,
    const WorksheetEvent& event,
    std::optional<WorksheetCellReplacementCoordinate> source_coordinate = std::nullopt)
{
    WorksheetTransformAction action { WorksheetTransformActionKind::PassThrough,
        event.kind,
        event.raw_xml,
        event.element_name,
        event.row_number,
        event.cell_reference,
        {},
        event.self_closing,
        event.raw_xml_offset,
        source_coordinate.has_value() ? source_coordinate->row : 0,
        source_coordinate.has_value() ? source_coordinate->column : 0,
        event.complete_cell,
        event.contains_formula };
    if (event.kind == WorksheetEventKind::CellStart) {
        action.source_cell_count = 1;
        if (source_coordinate.has_value()) {
            action.source_cell_min_row = source_coordinate->row;
            action.source_cell_min_column = source_coordinate->column;
            action.source_cell_max_row = source_coordinate->row;
            action.source_cell_max_column = source_coordinate->column;
        }
        action.uses_shared_string_index = event.uses_shared_string_index;
        action.has_style_reference = event.has_style_reference;
    }
    callback(action);
}

void emit_synthetic_pass_through(const WorksheetTransformActionCallback& callback,
    WorksheetEventKind event_kind,
    std::string_view raw_xml,
    std::string_view element_name,
    std::string_view row_number = {},
    std::string_view cell_reference = {},
    bool self_closing = false)
{
    callback(WorksheetTransformAction { WorksheetTransformActionKind::PassThrough,
        event_kind,
        raw_xml,
        element_name,
        row_number,
        cell_reference,
        {},
        self_closing,
        0 });
}

bool can_coalesce_pass_through_event(const WorksheetEvent& event) noexcept
{
    if (event.self_closing) {
        return false;
    }
    if (event.kind == WorksheetEventKind::RawText
        || event.kind == WorksheetEventKind::RowStart
        || event.kind == WorksheetEventKind::RowEnd
        || event.kind == WorksheetEventKind::CellValue
        || event.kind == WorksheetEventKind::CellEnd) {
        return true;
    }
    if (event.kind == WorksheetEventKind::CellStart) {
        return event.complete_cell;
    }
    return event.kind == WorksheetEventKind::CellValueMarkup
        && event.element_name != "f";
}

class WorksheetPassThroughActionEmitter {
public:
    WorksheetPassThroughActionEmitter(const WorksheetTransformActionCallback& callback,
        bool coalesce_pass_through_events)
        : callback_(callback)
        , coalesce_pass_through_events_(coalesce_pass_through_events)
    {
    }

    void emit_pass_through(const WorksheetEvent& event,
        std::optional<WorksheetCellReplacementCoordinate> source_coordinate = std::nullopt)
    {
        if (coalesce_pass_through_events_ && can_coalesce_pass_through_event(event)) {
            append_coalesced(event, source_coordinate);
            return;
        }
        flush();
        ::emit_pass_through(callback_, event, source_coordinate);
    }

    void emit_action(WorksheetTransformAction action)
    {
        flush();
        callback_(action);
    }

    void emit_synthetic_pass_through(WorksheetEventKind event_kind,
        std::string_view raw_xml,
        std::string_view element_name,
        std::string_view row_number = {},
        std::string_view cell_reference = {},
        bool self_closing = false)
    {
        flush();
        ::emit_synthetic_pass_through(callback_, event_kind, raw_xml, element_name,
            row_number, cell_reference, self_closing);
    }

    void flush()
    {
        if (coalesced_raw_xml_.empty()) {
            return;
        }
        WorksheetTransformAction action { WorksheetTransformActionKind::PassThrough,
            WorksheetEventKind::RawText,
            coalesced_raw_xml_,
            {},
            {},
            {},
            {},
            false,
            coalesced_raw_xml_offset_ };
        action.contains_formula = coalesced_contains_formula_;
        action.source_cell_count = coalesced_source_cell_count_;
        action.source_cell_min_row = coalesced_source_cell_min_row_;
        action.source_cell_min_column = coalesced_source_cell_min_column_;
        action.source_cell_max_row = coalesced_source_cell_max_row_;
        action.source_cell_max_column = coalesced_source_cell_max_column_;
        action.uses_shared_string_index = coalesced_uses_shared_string_index_;
        action.has_style_reference = coalesced_has_style_reference_;
        callback_(action);
        ++pass_through_batch_count_;
        pass_through_batched_cell_count_ += coalesced_source_cell_count_;
        pass_through_batched_bytes_ +=
            static_cast<std::uint64_t>(coalesced_raw_xml_.size());
        pass_through_batch_peak_cell_count_ = std::max(
            pass_through_batch_peak_cell_count_, coalesced_source_cell_count_);
        coalesced_raw_xml_ = {};
        coalesced_raw_xml_offset_ = 0;
        coalesced_source_cell_count_ = 0;
        coalesced_source_cell_min_row_ = 0;
        coalesced_source_cell_min_column_ = 0;
        coalesced_source_cell_max_row_ = 0;
        coalesced_source_cell_max_column_ = 0;
        coalesced_contains_formula_ = false;
        coalesced_uses_shared_string_index_ = false;
        coalesced_has_style_reference_ = false;
    }

    void apply_telemetry(WorksheetTransformSummary& summary) const noexcept
    {
        summary.pass_through_batch_count = pass_through_batch_count_;
        summary.pass_through_batched_cell_count = pass_through_batched_cell_count_;
        summary.pass_through_batched_bytes = pass_through_batched_bytes_;
        summary.pass_through_batch_peak_cell_count =
            pass_through_batch_peak_cell_count_;
    }

private:
    void append_coalesced(const WorksheetEvent& event,
        std::optional<WorksheetCellReplacementCoordinate> source_coordinate)
    {
        if (!coalesced_raw_xml_.empty()) {
            const char* expected = coalesced_raw_xml_.data() + coalesced_raw_xml_.size();
            if (event.raw_xml.data() != expected
                || event.raw_xml_offset
                    != coalesced_raw_xml_offset_ + coalesced_raw_xml_.size()) {
                flush();
            }
        }
        if (coalesced_raw_xml_.empty()) {
            coalesced_raw_xml_ = event.raw_xml;
            coalesced_raw_xml_offset_ = event.raw_xml_offset;
        } else {
            coalesced_raw_xml_ = std::string_view(coalesced_raw_xml_.data(),
                coalesced_raw_xml_.size() + event.raw_xml.size());
        }
        if (event.kind == WorksheetEventKind::CellStart) {
            if (!source_coordinate.has_value()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer pass-through cell has an invalid reference");
            }
            ++coalesced_source_cell_count_;
            if (coalesced_source_cell_count_ == 1) {
                coalesced_source_cell_min_row_ = source_coordinate->row;
                coalesced_source_cell_min_column_ = source_coordinate->column;
                coalesced_source_cell_max_row_ = source_coordinate->row;
                coalesced_source_cell_max_column_ = source_coordinate->column;
            } else {
                coalesced_source_cell_min_row_ =
                    std::min(coalesced_source_cell_min_row_, source_coordinate->row);
                coalesced_source_cell_min_column_ =
                    std::min(coalesced_source_cell_min_column_, source_coordinate->column);
                coalesced_source_cell_max_row_ =
                    std::max(coalesced_source_cell_max_row_, source_coordinate->row);
                coalesced_source_cell_max_column_ =
                    std::max(coalesced_source_cell_max_column_, source_coordinate->column);
            }
            coalesced_contains_formula_ =
                coalesced_contains_formula_ || event.contains_formula;
            coalesced_uses_shared_string_index_ =
                coalesced_uses_shared_string_index_ || event.uses_shared_string_index;
            coalesced_has_style_reference_ =
                coalesced_has_style_reference_ || event.has_style_reference;
        }
    }

    const WorksheetTransformActionCallback& callback_;
    bool coalesce_pass_through_events_ = false;
    std::string_view coalesced_raw_xml_;
    std::uint64_t coalesced_raw_xml_offset_ = 0;
    std::uint64_t coalesced_source_cell_count_ = 0;
    std::uint32_t coalesced_source_cell_min_row_ = 0;
    std::uint32_t coalesced_source_cell_min_column_ = 0;
    std::uint32_t coalesced_source_cell_max_row_ = 0;
    std::uint32_t coalesced_source_cell_max_column_ = 0;
    bool coalesced_contains_formula_ = false;
    bool coalesced_uses_shared_string_index_ = false;
    bool coalesced_has_style_reference_ = false;
    std::uint64_t pass_through_batch_count_ = 0;
    std::uint64_t pass_through_batched_cell_count_ = 0;
    std::uint64_t pass_through_batched_bytes_ = 0;
    std::uint64_t pass_through_batch_peak_cell_count_ = 0;
};

bool all_replacements_matched(const WorksheetCellReplacementPlan& replacement_plan,
    const std::set<std::string_view, std::less<>>& matched_replacements) noexcept
{
    return matched_replacements.size()
        >= replacement_plan.replacement_payloads_by_reference.size();
}

bool coordinate_after(WorksheetCellReplacementCoordinate lhs,
    WorksheetCellReplacementCoordinate rhs) noexcept
{
    return lhs.row > rhs.row || (lhs.row == rhs.row && lhs.column > rhs.column);
}

bool same_coordinate(WorksheetCellReplacementCoordinate lhs,
    WorksheetCellReplacementCoordinate rhs) noexcept
{
    return lhs.row == rhs.row && lhs.column == rhs.column;
}

std::optional<WorksheetCellReplacementCoordinate> try_parse_source_cell_coordinate(
    std::string_view reference) noexcept
{
    try {
        return parse_cell_reference_coordinate(
            reference, "worksheet transformer replacement source cell");
    } catch (...) {
        return std::nullopt;
    }
}

struct ReplacementScanState {
    std::set<std::string_view, std::less<>> matched_replacements;
    bool skip_completed_tail_lookup = false;
};

bool should_skip_completed_replacement_lookup(const WorksheetEvent& event,
    const WorksheetCellReplacementPlan& replacement_plan,
    ReplacementScanState& state)
{
    if (state.skip_completed_tail_lookup) {
        return true;
    }
    if (!all_replacements_matched(replacement_plan, state.matched_replacements)) {
        return false;
    }
    if (replacement_plan.targets_by_position.empty()) {
        state.skip_completed_tail_lookup = true;
        return true;
    }

    const std::optional<WorksheetCellReplacementCoordinate> coordinate =
        try_parse_source_cell_coordinate(event.cell_reference);
    if (!coordinate.has_value()) {
        return false;
    }
    const WorksheetCellReplacementCoordinate last_target_coordinate =
        replacement_plan.targets_by_position.back().coordinate;
    if (!coordinate_after(*coordinate, last_target_coordinate)) {
        return false;
    }

    state.skip_completed_tail_lookup = true;
    return true;
}

void consume_replacement_event(const WorksheetEvent& event,
    const WorksheetCellReplacementPlan& replacement_plan,
    ReplacementScanState& scan_state,
    bool& replacing_current_cell,
    WorksheetPassThroughActionEmitter& action_emitter)
{
    std::optional<WorksheetCellReplacementCoordinate> source_coordinate;
    if (event.kind == WorksheetEventKind::CellStart) {
        source_coordinate = try_parse_source_cell_coordinate(event.cell_reference);
    }
    if (event.kind == WorksheetEventKind::CellStart
        && !should_skip_completed_replacement_lookup(event, replacement_plan, scan_state)) {
        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(event.cell_reference);
        if (replacement != replacement_plan.replacement_payloads_by_reference.end()) {
            action_emitter.emit_action(WorksheetTransformAction { WorksheetTransformActionKind::ReplaceCell,
                event.kind,
                event.raw_xml,
                event.element_name,
                event.row_number,
                event.cell_reference,
                replacement->second,
                event.self_closing,
                event.raw_xml_offset,
                source_coordinate.has_value() ? source_coordinate->row : 0,
                source_coordinate.has_value() ? source_coordinate->column : 0,
                event.complete_cell,
                event.contains_formula });
            scan_state.matched_replacements.insert(replacement->first);
            replacing_current_cell = !event.complete_cell;
            return;
        }
    }

    if (replacing_current_cell) {
        if (event.kind == WorksheetEventKind::CellEnd) {
            replacing_current_cell = false;
        }
        return;
    }

    action_emitter.emit_pass_through(event, source_coordinate);
}

WorksheetTransformSummary build_summary(const WorksheetCellReplacementPlan& replacement_plan,
    const std::set<std::string_view, std::less<>>& matched_replacements,
    const std::set<std::string_view, std::less<>>& inserted_replacements,
    WorksheetCellReplacementMode mode);

class WorksheetCellUpsertActionEmitter {
public:
    WorksheetCellUpsertActionEmitter(const WorksheetCellReplacementPlan& replacement_plan,
        const WorksheetTransformActionCallback& callback,
        bool coalesce_cell_value_events)
        : replacement_plan_(replacement_plan)
        , action_emitter_(callback, coalesce_cell_value_events)
    {
    }

    void consume_event(const WorksheetEvent& event)
    {
        if (replacing_current_cell_) {
            if (event.kind == WorksheetEventKind::CellEnd) {
                replacing_current_cell_ = false;
            }
            return;
        }

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
        case WorksheetEventKind::RowStart:
            consume_row_start(event);
            return;
        case WorksheetEventKind::RowEnd:
            consume_row_end(event);
            return;
        case WorksheetEventKind::CellStart:
            consume_cell_start(event);
            return;
        default:
            action_emitter_.emit_pass_through(event);
            return;
        }
    }

    [[nodiscard]] WorksheetTransformSummary summary() const
    {
        WorksheetTransformSummary result = build_summary(replacement_plan_, matched_replacements_,
            inserted_replacements_, WorksheetCellReplacementMode::ReplaceOrInsert);
        action_emitter_.apply_telemetry(result);
        return result;
    }

    void flush_window()
    {
        action_emitter_.flush();
    }

private:
    [[nodiscard]] bool target_was_emitted(std::string_view cell_reference) const
    {
        return matched_replacements_.contains(cell_reference)
            || inserted_replacements_.contains(cell_reference);
    }

    [[nodiscard]] bool has_unemitted_targets() const noexcept
    {
        return matched_replacements_.size() + inserted_replacements_.size()
            < replacement_plan_.targets_by_position.size();
    }

    void advance_out_of_order_emitted_targets()
    {
        while (!out_of_order_emitted_targets_.empty()
            && next_target_index_ < replacement_plan_.targets_by_position.size()) {
            const std::string_view reference =
                replacement_plan_.targets_by_position[next_target_index_].cell_reference;
            const auto emitted = out_of_order_emitted_targets_.find(reference);
            if (emitted == out_of_order_emitted_targets_.end()) {
                return;
            }
            out_of_order_emitted_targets_.erase(emitted);
            ++next_target_index_;
        }
    }

    void mark_target_emitted(std::string_view cell_reference)
    {
        if (next_target_index_ < replacement_plan_.targets_by_position.size()
            && replacement_plan_.targets_by_position[next_target_index_].cell_reference
                == cell_reference) {
            ++next_target_index_;
            advance_out_of_order_emitted_targets();
            return;
        }
        out_of_order_emitted_targets_.insert(cell_reference);
    }

    [[nodiscard]] const WorksheetCellReplacementTarget* next_pending_target()
    {
        if (next_target_index_ >= replacement_plan_.targets_by_position.size()) {
            return nullptr;
        }
        return &replacement_plan_.targets_by_position[next_target_index_];
    }

    [[nodiscard]] bool has_remaining_targets()
    {
        return next_pending_target() != nullptr;
    }

    [[nodiscard]] bool has_pending_targets_in_row(std::uint32_t row)
    {
        const WorksheetCellReplacementTarget* target = next_pending_target();
        return target != nullptr && target->coordinate.row == row;
    }

    [[nodiscard]] std::string_view synthetic_row_prefix() const noexcept
    {
        if (!current_row_prefix_.empty()) {
            return current_row_prefix_;
        }
        if (!row_prefix_.empty()) {
            return row_prefix_;
        }
        if (!sheet_data_prefix_.empty()) {
            return sheet_data_prefix_;
        }
        return worksheet_prefix_;
    }

    [[nodiscard]] std::string_view synthetic_sheet_data_prefix() const noexcept
    {
        if (!sheet_data_prefix_.empty()) {
            return sheet_data_prefix_;
        }
        return worksheet_prefix_;
    }

    void emit_insert_cell(const WorksheetCellReplacementTarget& target)
    {
        const std::string row_number = std::to_string(target.coordinate.row);
        action_emitter_.emit_action(WorksheetTransformAction { WorksheetTransformActionKind::InsertCell,
            WorksheetEventKind::CellStart,
            {},
            "c",
            row_number,
            target.cell_reference,
            target.replacement_payload,
            false,
            0,
            target.coordinate.row,
            target.coordinate.column });
        inserted_replacements_.insert(target.cell_reference);
        mark_target_emitted(target.cell_reference);
    }

    void emit_pending_cells_before_column(std::uint32_t row, std::uint32_t column)
    {
        while (const WorksheetCellReplacementTarget* target = next_pending_target()) {
            if (target->coordinate.row != row || target->coordinate.column >= column) {
                return;
            }
            emit_insert_cell(*target);
        }
    }

    void emit_pending_cells_for_row(std::uint32_t row)
    {
        while (const WorksheetCellReplacementTarget* target = next_pending_target()) {
            if (target->coordinate.row != row) {
                return;
            }
            emit_insert_cell(*target);
        }
    }

    void emit_synthetic_row(std::uint32_t row)
    {
        const std::string row_number = std::to_string(row);
        const std::string start = synthetic_row_start_tag(synthetic_row_prefix(), row);
        action_emitter_.emit_synthetic_pass_through(
            WorksheetEventKind::RowStart, start, "row", row_number);
        emit_pending_cells_for_row(row);
        const std::string end = synthetic_end_tag(synthetic_row_prefix(), "row");
        action_emitter_.emit_synthetic_pass_through(
            WorksheetEventKind::RowEnd, end, "row", row_number);
    }

    void emit_pending_rows_before(std::uint32_t row)
    {
        while (const WorksheetCellReplacementTarget* target = next_pending_target()) {
            if (target->coordinate.row >= row) {
                return;
            }
            emit_synthetic_row(target->coordinate.row);
        }
    }

    void emit_all_remaining_rows()
    {
        while (const WorksheetCellReplacementTarget* target = next_pending_target()) {
            emit_synthetic_row(target->coordinate.row);
        }
    }

    void emit_synthetic_sheet_data_with_remaining_rows()
    {
        if (!has_remaining_targets()) {
            return;
        }
        const std::string start = synthetic_start_tag(synthetic_sheet_data_prefix(), "sheetData");
        action_emitter_.emit_synthetic_pass_through(
            WorksheetEventKind::SheetDataStart, start, "sheetData");
        emit_all_remaining_rows();
        const std::string end = synthetic_end_tag(synthetic_sheet_data_prefix(), "sheetData");
        action_emitter_.emit_synthetic_pass_through(
            WorksheetEventKind::SheetDataEnd, end, "sheetData");
    }

    void consume_worksheet_start(const WorksheetEvent& event)
    {
        worksheet_prefix_ = element_prefix(event.raw_xml);
        if (event.self_closing && has_remaining_targets()) {
            const std::string start = self_closing_tag_as_open_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::WorksheetStart,
                start, event.element_name);
            emit_synthetic_sheet_data_with_remaining_rows();
            const std::string end = closing_tag_from_start_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::WorksheetEnd,
                end, event.element_name);
            suppress_self_closing_worksheet_end_ = true;
            return;
        }
        action_emitter_.emit_pass_through(event);
    }

    void consume_worksheet_end(const WorksheetEvent& event)
    {
        if (suppress_self_closing_worksheet_end_) {
            suppress_self_closing_worksheet_end_ = false;
            return;
        }
        if (!seen_sheet_data_) {
            emit_synthetic_sheet_data_with_remaining_rows();
        }
        action_emitter_.emit_pass_through(event);
    }

    void consume_sheet_data_start(const WorksheetEvent& event)
    {
        seen_sheet_data_ = true;
        sheet_data_prefix_ = element_prefix(event.raw_xml);
        if (event.self_closing && has_remaining_targets()) {
            const std::string start = self_closing_tag_as_open_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::SheetDataStart,
                start, event.element_name);
            emit_all_remaining_rows();
            const std::string end = closing_tag_from_start_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::SheetDataEnd,
                end, event.element_name);
            suppress_self_closing_sheet_data_end_ = true;
            return;
        }
        action_emitter_.emit_pass_through(event);
    }

    void consume_sheet_data_end(const WorksheetEvent& event)
    {
        if (suppress_self_closing_sheet_data_end_) {
            suppress_self_closing_sheet_data_end_ = false;
            return;
        }
        if (!event.self_closing) {
            emit_all_remaining_rows();
        }
        action_emitter_.emit_pass_through(event);
    }

    void consume_row_start(const WorksheetEvent& event)
    {
        current_row_ = parse_row_number(event.row_number);
        current_row_prefix_ = element_prefix(event.raw_xml);
        if (!current_row_prefix_.empty()) {
            row_prefix_ = current_row_prefix_;
        }
        if (current_row_.has_value()) {
            emit_pending_rows_before(*current_row_);
        }

        if (event.self_closing && current_row_.has_value()
            && has_pending_targets_in_row(*current_row_)) {
            const std::string start = self_closing_tag_as_open_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::RowStart,
                start, event.element_name, event.row_number);
            emit_pending_cells_for_row(*current_row_);
            const std::string end = closing_tag_from_start_tag(event.raw_xml);
            action_emitter_.emit_synthetic_pass_through(WorksheetEventKind::RowEnd,
                end, event.element_name, event.row_number);
            suppress_self_closing_row_end_ = true;
            current_row_.reset();
            current_row_prefix_.clear();
            return;
        }

        action_emitter_.emit_pass_through(event);
    }

    void consume_row_end(const WorksheetEvent& event)
    {
        if (suppress_self_closing_row_end_) {
            suppress_self_closing_row_end_ = false;
            return;
        }
        if (current_row_.has_value()) {
            emit_pending_cells_for_row(*current_row_);
        }
        action_emitter_.emit_pass_through(event);
        current_row_.reset();
        current_row_prefix_.clear();
    }

    void consume_cell_start(const WorksheetEvent& event)
    {
        std::optional<WorksheetCellReplacementCoordinate> source_coordinate;
        if (!event.cell_reference.empty()) {
            source_coordinate = parse_cell_reference_coordinate(event.cell_reference,
                "worksheet transformer upsert source cell");
        }
        const bool ordered_source_coordinate = source_coordinate.has_value()
            && current_row_.has_value()
            && *current_row_ == source_coordinate->row;
        if (ordered_source_coordinate && has_unemitted_targets()) {
            emit_pending_cells_before_column(
                source_coordinate->row, source_coordinate->column);
            const WorksheetCellReplacementTarget* target = next_pending_target();
            if (target != nullptr
                && same_coordinate(target->coordinate, *source_coordinate)
                && target->cell_reference == event.cell_reference) {
                emit_replace_cell(event, *source_coordinate, target->cell_reference,
                    target->replacement_payload);
                return;
            }
        } else if (has_unemitted_targets()) {
            const auto replacement =
                replacement_plan_.replacement_payloads_by_reference.find(event.cell_reference);
            if (replacement != replacement_plan_.replacement_payloads_by_reference.end()
                && !target_was_emitted(replacement->first)) {
                emit_replace_cell(event, source_coordinate.value_or(
                    WorksheetCellReplacementCoordinate {}), replacement->first,
                    replacement->second);
                return;
            }
        }

        action_emitter_.emit_pass_through(event, source_coordinate);
    }

    void emit_replace_cell(const WorksheetEvent& event,
        WorksheetCellReplacementCoordinate source_coordinate,
        std::string_view matched_reference,
        const WorksheetCellReplacementPayload& replacement_payload)
    {
        action_emitter_.emit_action(WorksheetTransformAction {
            WorksheetTransformActionKind::ReplaceCell,
            event.kind,
            event.raw_xml,
            event.element_name,
            event.row_number,
            event.cell_reference,
            replacement_payload,
            event.self_closing,
            event.raw_xml_offset,
            source_coordinate.row,
            source_coordinate.column,
            event.complete_cell,
            event.contains_formula });
        matched_replacements_.insert(matched_reference);
        mark_target_emitted(matched_reference);
        replacing_current_cell_ = !event.complete_cell;
    }

    const WorksheetCellReplacementPlan& replacement_plan_;
    WorksheetPassThroughActionEmitter action_emitter_;
    std::set<std::string_view, std::less<>> matched_replacements_;
    std::set<std::string_view, std::less<>> inserted_replacements_;
    std::set<std::string_view, std::less<>> out_of_order_emitted_targets_;
    std::size_t next_target_index_ = 0;
    bool replacing_current_cell_ = false;
    bool seen_sheet_data_ = false;
    bool suppress_self_closing_worksheet_end_ = false;
    bool suppress_self_closing_sheet_data_end_ = false;
    bool suppress_self_closing_row_end_ = false;
    std::optional<std::uint32_t> current_row_;
    std::string worksheet_prefix_;
    std::string sheet_data_prefix_;
    std::string row_prefix_;
    std::string current_row_prefix_;
};

WorksheetTransformSummary build_summary(const WorksheetCellReplacementPlan& replacement_plan,
    const std::set<std::string_view, std::less<>>& matched_replacements,
    const std::set<std::string_view, std::less<>>& inserted_replacements,
    WorksheetCellReplacementMode mode)
{
    WorksheetTransformSummary summary;
    summary.matched_replacement_count = matched_replacements.size();
    summary.inserted_cell_count = inserted_replacements.size();
    for (const auto& [cell_reference, _] :
         replacement_plan.replacement_payloads_by_reference) {
        if (matched_replacements.contains(cell_reference)) {
            continue;
        }
        if (inserted_replacements.contains(cell_reference)
            && mode == WorksheetCellReplacementMode::ReplaceOrInsert) {
            continue;
        }
        summary.missing_cell_references.emplace_back(cell_reference);
    }
    return summary;
}

void emit_chunk(const WorksheetOutputChunkCallback& callback, std::string_view chunk)
{
    if (!chunk.empty()) {
        callback(chunk);
    }
}

bool is_self_closing_synthetic_end_event(
    WorksheetEventKind event_kind, bool self_closing) noexcept
{
    if (!self_closing) {
        return false;
    }

    return event_kind == WorksheetEventKind::WorksheetEnd
        || event_kind == WorksheetEventKind::SheetDataEnd
        || event_kind == WorksheetEventKind::RowEnd
        || event_kind == WorksheetEventKind::CellEnd;
}

} // namespace

namespace fastxlsx::detail {

WorksheetCellReplacementPayload WorksheetCellReplacementPayload::from_chunks(
    std::span<const std::string_view> chunks)
{
    WorksheetCellReplacementPayload payload;
    payload.chunks_.reserve(chunks.size());

    for (const std::string_view chunk : chunks) {
        if (chunk.size() > std::numeric_limits<std::size_t>::max() - payload.byte_size_) {
            throw FastXlsxError(
                "worksheet transformer replacement cell XML byte size overflow");
        }
        payload.chunks_.push_back(chunk);
        payload.byte_size_ += chunk.size();
    }

    return payload;
}

std::string WorksheetCellReplacementPayload::materialize_for_preflight() const
{
    require_replacement_cell_payload_size(*this);

    std::string materialized;
    materialized.reserve(byte_size_);
    for_each_chunk([&](std::string_view chunk) { materialized += chunk; });
    return materialized;
}

WorksheetCellReplacementPlan make_worksheet_cell_replacement_plan(
    std::span<const WorksheetCellReplacement> replacements)
{
    WorksheetCellReplacementPlan plan;
    std::set<std::pair<std::uint32_t, std::uint32_t>> target_coordinates;

    for (const WorksheetCellReplacement& replacement : replacements) {
        if (!has_non_whitespace(replacement.cell_reference)) {
            throw FastXlsxError(
                "worksheet transformer replacement cell reference cannot be empty");
        }
        const WorksheetCellReplacementCoordinate coordinate =
            parse_cell_reference_coordinate(replacement.cell_reference,
                "worksheet transformer replacement selector");
        if (!target_coordinates.insert({coordinate.row, coordinate.column}).second) {
            throw FastXlsxError(
                "worksheet transformer replacement cell references must be unique");
        }
        require_replacement_cell_payload_size(replacement.replacement_payload);
        const std::string materialized_replacement_cell_xml =
            replacement.replacement_payload.materialize_for_preflight();
        if (!has_non_whitespace(materialized_replacement_cell_xml)) {
            throw FastXlsxError(
                "worksheet transformer replacement cell XML cannot be empty");
        }
        validate_replacement_cell_payload(replacement, materialized_replacement_cell_xml);

        auto [_, inserted] =
            plan.replacement_payloads_by_reference.emplace(replacement.cell_reference,
                replacement.replacement_payload);
        if (!inserted) {
            throw FastXlsxError(
                "worksheet transformer replacement cell references must be unique");
        }
        plan.targets_by_position.push_back(WorksheetCellReplacementTarget {
            replacement.cell_reference,
            coordinate,
            replacement.replacement_payload,
        });
    }

    std::sort(plan.targets_by_position.begin(), plan.targets_by_position.end(),
        [](const WorksheetCellReplacementTarget& left,
            const WorksheetCellReplacementTarget& right) {
            if (left.coordinate.row != right.coordinate.row) {
                return left.coordinate.row < right.coordinate.row;
            }
            if (left.coordinate.column != right.coordinate.column) {
                return left.coordinate.column < right.coordinate.column;
            }
            return left.cell_reference < right.cell_reference;
        });
    return plan;
}

WorksheetTransformSummary scan_cell_replacement_actions_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetTransformActionCallback& callback,
    WorksheetEventReaderOptions reader_options,
    WorksheetCellReplacementMode mode)
{
    if (!read_next_chunk) {
        throw FastXlsxError("worksheet transformer requires a chunk source");
    }
    if (!callback) {
        throw FastXlsxError("worksheet transformer requires a callback");
    }

    if (mode == WorksheetCellReplacementMode::ReplaceOrInsert) {
        WorksheetCellUpsertActionEmitter emitter(
            replacement_plan, callback,
            reader_options.coalesce_cell_value_events
                || reader_options.coalesce_complete_cell_events);
        scan_worksheet_events_from_chunk_source(
            read_next_chunk,
            [&](const WorksheetEvent& event) {
                emitter.consume_event(event);
            },
            reader_options,
            [&]() { emitter.flush_window(); });
        emitter.flush_window();
        return emitter.summary();
    }

    ReplacementScanState scan_state;
    std::set<std::string_view, std::less<>> inserted_replacements;
    bool replacing_current_cell = false;
    WorksheetPassThroughActionEmitter action_emitter(
        callback, reader_options.coalesce_cell_value_events
            || reader_options.coalesce_complete_cell_events);

    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            consume_replacement_event(
                event, replacement_plan, scan_state, replacing_current_cell, action_emitter);
        },
        reader_options,
        [&]() { action_emitter.flush(); });
    action_emitter.flush();

    WorksheetTransformSummary summary = build_summary(
        replacement_plan, scan_state.matched_replacements, inserted_replacements, mode);
    action_emitter.apply_telemetry(summary);
    return summary;
}

WorksheetTransformSummary emit_cell_replacement_worksheet_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback,
    WorksheetEventReaderOptions reader_options,
    WorksheetCellReplacementMode mode)
{
    if (!read_next_chunk) {
        throw FastXlsxError("worksheet transformer output emitter requires a chunk source");
    }
    if (!callback) {
        throw FastXlsxError("worksheet transformer output emitter requires a callback");
    }

    return scan_cell_replacement_actions_from_chunk_source(
        read_next_chunk,
        replacement_plan,
        [&](const WorksheetTransformAction& action) {
            if (action.kind == WorksheetTransformActionKind::ReplaceCell
                || action.kind == WorksheetTransformActionKind::InsertCell) {
                action.replacement_payload.for_each_chunk(
                    [&](std::string_view chunk) { emit_chunk(callback, chunk); });
                return;
            }
            if (is_self_closing_synthetic_end_event(action.event_kind, action.self_closing)) {
                return;
            }
            emit_chunk(callback, action.raw_xml);
        },
        reader_options,
        mode);
}

WorksheetTransformSummary emit_indexed_cell_replacement_worksheet(
    std::string_view worksheet_xml,
    const WorksheetCellIndex& index,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback)
{
    if (!callback) {
        throw FastXlsxError("indexed worksheet replacement output emitter requires a callback");
    }

    std::vector<std::string_view> target_references;
    target_references.reserve(replacement_plan.replacement_payloads_by_reference.size());
    for (const auto& replacement : replacement_plan.replacement_payloads_by_reference) {
        target_references.push_back(replacement.first);
    }

    const std::vector<WorksheetIndexedCellRewrite> rewrites =
        plan_indexed_cell_rewrites(index, target_references);

    std::uint64_t cursor = 0;
    for (const WorksheetIndexedCellRewrite& rewrite : rewrites) {
        (void)worksheet_cell_range_xml(worksheet_xml, rewrite.source_range);
        if (rewrite.source_range.start_offset < cursor) {
            throw FastXlsxError("indexed worksheet replacement source ranges are not ordered");
        }

        const std::uint64_t pass_through_size = rewrite.source_range.start_offset - cursor;
        if (pass_through_size > 0) {
            emit_chunk(callback,
                worksheet_xml.substr(static_cast<std::size_t>(cursor),
                    static_cast<std::size_t>(pass_through_size)));
        }

        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(
                std::string_view(rewrite.cell_reference));
        if (replacement == replacement_plan.replacement_payloads_by_reference.end()) {
            throw FastXlsxError(
                "indexed worksheet replacement plan lost target cell: "
                + rewrite.cell_reference);
        }
        replacement->second.for_each_chunk(
            [&](std::string_view chunk) { emit_chunk(callback, chunk); });
        cursor = rewrite.source_range.end_offset;
    }

    if (cursor > static_cast<std::uint64_t>(worksheet_xml.size())) {
        throw FastXlsxError("indexed worksheet replacement source cursor overflow");
    }
    emit_chunk(callback,
        worksheet_xml.substr(static_cast<std::size_t>(cursor)));

    WorksheetTransformSummary summary;
    summary.matched_replacement_count = rewrites.size();
    return summary;
}

} // namespace fastxlsx::detail
