#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/workbook.hpp>

#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using fastxlsx::detail::WorksheetCellReplacement;
using fastxlsx::detail::WorksheetCellReplacementPayload;
using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventKind;
using fastxlsx::detail::WorksheetCellReplacementPlan;
using fastxlsx::detail::WorksheetOutputChunkCallback;
using fastxlsx::detail::WorksheetTransformAction;
using fastxlsx::detail::WorksheetTransformActionCallback;
using fastxlsx::detail::WorksheetTransformActionKind;
using fastxlsx::detail::WorksheetTransformSummary;

bool is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
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

void emit_pass_through(const WorksheetTransformActionCallback& callback, const WorksheetEvent& event)
{
    callback(WorksheetTransformAction { WorksheetTransformActionKind::PassThrough,
        event.kind,
        event.raw_xml,
        event.element_name,
        event.row_number,
        event.cell_reference,
        {},
        event.self_closing });
}

void consume_replacement_event(const WorksheetEvent& event,
    const WorksheetCellReplacementPlan& replacement_plan,
    std::set<std::string_view, std::less<>>& matched_replacements,
    bool& replacing_current_cell,
    const WorksheetTransformActionCallback& callback)
{
    if (event.kind == WorksheetEventKind::CellStart) {
        const auto replacement =
            replacement_plan.replacement_payloads_by_reference.find(event.cell_reference);
        if (replacement != replacement_plan.replacement_payloads_by_reference.end()) {
            callback(WorksheetTransformAction { WorksheetTransformActionKind::ReplaceCell,
                event.kind,
                event.raw_xml,
                event.element_name,
                event.row_number,
                event.cell_reference,
                replacement->second,
                event.self_closing });
            matched_replacements.insert(replacement->first);
            replacing_current_cell = true;
            return;
        }
    }

    if (replacing_current_cell) {
        if (event.kind == WorksheetEventKind::CellEnd) {
            replacing_current_cell = false;
        }
        return;
    }

    emit_pass_through(callback, event);
}

WorksheetTransformSummary build_summary(const WorksheetCellReplacementPlan& replacement_plan,
    const std::set<std::string_view, std::less<>>& matched_replacements)
{
    WorksheetTransformSummary summary;
    summary.matched_replacement_count = matched_replacements.size();
    for (const auto& [cell_reference, _] :
        replacement_plan.replacement_payloads_by_reference) {
        if (!matched_replacements.contains(cell_reference)) {
            summary.missing_cell_references.emplace_back(cell_reference);
        }
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

    for (const WorksheetCellReplacement& replacement : replacements) {
        if (!has_non_whitespace(replacement.cell_reference)) {
            throw FastXlsxError(
                "worksheet transformer replacement cell reference cannot be empty");
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
    }

    return plan;
}

WorksheetTransformSummary scan_cell_replacement_actions_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetTransformActionCallback& callback,
    WorksheetEventReaderOptions reader_options)
{
    if (!read_next_chunk) {
        throw FastXlsxError("worksheet transformer requires a chunk source");
    }
    if (!callback) {
        throw FastXlsxError("worksheet transformer requires a callback");
    }

    std::set<std::string_view, std::less<>> matched_replacements;
    bool replacing_current_cell = false;

    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            consume_replacement_event(
                event, replacement_plan, matched_replacements, replacing_current_cell, callback);
        },
        reader_options);

    return build_summary(replacement_plan, matched_replacements);
}

WorksheetTransformSummary emit_cell_replacement_worksheet_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback,
    WorksheetEventReaderOptions reader_options)
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
            if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
                action.replacement_payload.for_each_chunk(
                    [&](std::string_view chunk) { emit_chunk(callback, chunk); });
                return;
            }
            if (is_self_closing_synthetic_end_event(action.event_kind, action.self_closing)) {
                return;
            }
            emit_chunk(callback, action.raw_xml);
        },
        reader_options);
}

} // namespace fastxlsx::detail
