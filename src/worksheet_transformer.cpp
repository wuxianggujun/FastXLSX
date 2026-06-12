#include <fastxlsx/detail/worksheet_transformer.hpp>

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/workbook.hpp>

#include <cctype>
#include <map>
#include <set>
#include <string>

namespace {

using fastxlsx::detail::WorksheetCellReplacement;
using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventKind;
using fastxlsx::detail::WorksheetOutputChunkCallback;
using fastxlsx::detail::WorksheetTransformAction;
using fastxlsx::detail::WorksheetTransformActionCallback;
using fastxlsx::detail::WorksheetTransformActionKind;
using fastxlsx::detail::WorksheetTransformSummary;

using ReplacementIndex = std::map<std::string, std::string_view, std::less<>>;

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

std::string_view parse_replacement_cell_reference(std::string_view replacement_cell_xml)
{
    std::size_t cursor = 0;
    while (cursor < replacement_cell_xml.size() && is_space(replacement_cell_xml[cursor])) {
        ++cursor;
    }

    if (cursor >= replacement_cell_xml.size() || replacement_cell_xml[cursor] != '<'
        || cursor + 1 >= replacement_cell_xml.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const char first_tag_char = replacement_cell_xml[cursor + 1];
    if (first_tag_char == '/' || first_tag_char == '?' || first_tag_char == '!') {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const std::size_t tag_end = find_start_tag_end(replacement_cell_xml, cursor);
    if (tag_end == std::string_view::npos) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML has an unterminated start tag");
    }

    std::size_t name_start = cursor + 1;
    std::size_t name_end = name_start;
    while (name_end < tag_end && !is_space(replacement_cell_xml[name_end])
           && replacement_cell_xml[name_end] != '/') {
        ++name_end;
    }
    if (name_end == name_start) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML must begin with a cell element");
    }

    const std::string_view qualified_name =
        replacement_cell_xml.substr(name_start, name_end - name_start);
    if (xml_local_name(qualified_name) != "c") {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML root must be a cell element");
    }

    bool saw_cell_reference = false;
    std::string_view cell_reference;
    cursor = name_end;
    while (cursor < tag_end) {
        while (cursor < tag_end && is_space(replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end) {
            break;
        }
        if (replacement_cell_xml[cursor] == '/') {
            ++cursor;
            while (cursor < tag_end && is_space(replacement_cell_xml[cursor])) {
                ++cursor;
            }
            if (cursor != tag_end) {
                throw fastxlsx::FastXlsxError(
                    "worksheet transformer replacement cell XML has malformed attributes");
            }
            break;
        }

        const std::size_t attribute_name_start = cursor;
        while (cursor < tag_end && !is_space(replacement_cell_xml[cursor])
               && replacement_cell_xml[cursor] != '=' && replacement_cell_xml[cursor] != '/') {
            ++cursor;
        }
        if (cursor == attribute_name_start) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has malformed attributes");
        }
        const std::string_view attribute_name =
            replacement_cell_xml.substr(attribute_name_start, cursor - attribute_name_start);

        while (cursor < tag_end && is_space(replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end || replacement_cell_xml[cursor] != '=') {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has malformed attributes");
        }
        ++cursor;
        while (cursor < tag_end && is_space(replacement_cell_xml[cursor])) {
            ++cursor;
        }
        if (cursor >= tag_end
            || (replacement_cell_xml[cursor] != '"' && replacement_cell_xml[cursor] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML attributes must be quoted");
        }

        const char quote = replacement_cell_xml[cursor];
        const std::size_t attribute_value_start = ++cursor;
        while (cursor < tag_end && replacement_cell_xml[cursor] != quote) {
            ++cursor;
        }
        if (cursor >= tag_end) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML has an unterminated attribute");
        }
        const std::string_view attribute_value =
            replacement_cell_xml.substr(attribute_value_start, cursor - attribute_value_start);
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

void validate_replacement_cell_payload(const WorksheetCellReplacement& replacement)
{
    const std::string_view replacement_cell_reference =
        parse_replacement_cell_reference(replacement.replacement_cell_xml);
    if (replacement_cell_reference != replacement.cell_reference) {
        throw fastxlsx::FastXlsxError(
            "worksheet transformer replacement cell XML r attribute must match its selector");
    }
}

ReplacementIndex build_replacement_index(std::span<const WorksheetCellReplacement> replacements)
{
    ReplacementIndex index;

    for (const WorksheetCellReplacement& replacement : replacements) {
        if (!has_non_whitespace(replacement.cell_reference)) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell reference cannot be empty");
        }
        if (!has_non_whitespace(replacement.replacement_cell_xml)) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML cannot be empty");
        }
        validate_replacement_cell_payload(replacement);

        auto [_, inserted] =
            index.emplace(std::string(replacement.cell_reference), replacement.replacement_cell_xml);
        if (!inserted) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell references must be unique");
        }
    }

    return index;
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
    const ReplacementIndex& replacement_index,
    std::set<std::string, std::less<>>& matched_replacements,
    bool& replacing_current_cell,
    const WorksheetTransformActionCallback& callback)
{
    if (event.kind == WorksheetEventKind::CellStart) {
        const auto replacement = replacement_index.find(event.cell_reference);
        if (replacement != replacement_index.end()) {
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

WorksheetTransformSummary build_summary(const ReplacementIndex& replacement_index,
    const std::set<std::string, std::less<>>& matched_replacements)
{
    WorksheetTransformSummary summary;
    summary.matched_replacement_count = matched_replacements.size();
    for (const auto& [cell_reference, _] : replacement_index) {
        if (!matched_replacements.contains(cell_reference)) {
            summary.missing_cell_references.push_back(cell_reference);
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

} // namespace

namespace fastxlsx::detail {

WorksheetTransformSummary scan_cell_replacement_actions(
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetTransformActionCallback& callback)
{
    if (!callback) {
        throw FastXlsxError("worksheet transformer requires a callback");
    }

    const ReplacementIndex replacement_index = build_replacement_index(replacements);
    std::set<std::string, std::less<>> matched_replacements;
    bool replacing_current_cell = false;

    scan_worksheet_events(worksheet_xml, [&](const WorksheetEvent& event) {
        consume_replacement_event(
            event, replacement_index, matched_replacements, replacing_current_cell, callback);
    });

    return build_summary(replacement_index, matched_replacements);
}

WorksheetTransformSummary scan_cell_replacement_actions_from_chunks(
    std::span<const std::string_view> worksheet_xml_chunks,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetTransformActionCallback& callback,
    WorksheetEventReaderOptions reader_options)
{
    if (!callback) {
        throw FastXlsxError("worksheet transformer requires a callback");
    }

    const ReplacementIndex replacement_index = build_replacement_index(replacements);
    std::set<std::string, std::less<>> matched_replacements;
    bool replacing_current_cell = false;

    scan_worksheet_events_from_chunks(
        worksheet_xml_chunks,
        [&](const WorksheetEvent& event) {
            consume_replacement_event(
                event, replacement_index, matched_replacements, replacing_current_cell, callback);
        },
        reader_options);

    return build_summary(replacement_index, matched_replacements);
}

WorksheetTransformSummary emit_cell_replacement_worksheet(
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetOutputChunkCallback& callback)
{
    if (!callback) {
        throw FastXlsxError("worksheet transformer output emitter requires a callback");
    }

    return scan_cell_replacement_actions(
        worksheet_xml, replacements, [&](const WorksheetTransformAction& action) {
            if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
                emit_chunk(callback, action.replacement_cell_xml);
                return;
            }
            emit_chunk(callback, action.raw_xml);
        });
}

WorksheetTransformSummary emit_cell_replacement_worksheet_from_chunks(
    std::span<const std::string_view> worksheet_xml_chunks,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetOutputChunkCallback& callback,
    WorksheetEventReaderOptions reader_options)
{
    if (!callback) {
        throw FastXlsxError("worksheet transformer output emitter requires a callback");
    }

    return scan_cell_replacement_actions_from_chunks(
        worksheet_xml_chunks,
        replacements,
        [&](const WorksheetTransformAction& action) {
            if (action.kind == WorksheetTransformActionKind::ReplaceCell) {
                emit_chunk(callback, action.replacement_cell_xml);
                return;
            }
            emit_chunk(callback, action.raw_xml);
        },
        reader_options);
}

} // namespace fastxlsx::detail
