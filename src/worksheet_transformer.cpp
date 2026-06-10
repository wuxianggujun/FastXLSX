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

std::map<std::string, std::string_view, std::less<>> build_replacement_index(
    std::span<const WorksheetCellReplacement> replacements)
{
    std::map<std::string, std::string_view, std::less<>> index;

    for (const WorksheetCellReplacement& replacement : replacements) {
        if (!has_non_whitespace(replacement.cell_reference)) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell reference cannot be empty");
        }
        if (!has_non_whitespace(replacement.replacement_cell_xml)) {
            throw fastxlsx::FastXlsxError(
                "worksheet transformer replacement cell XML cannot be empty");
        }

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
        event.raw_xml,
        event.row_number,
        event.cell_reference,
        {} });
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

    const std::map<std::string, std::string_view, std::less<>> replacement_index =
        build_replacement_index(replacements);
    std::set<std::string, std::less<>> matched_replacements;
    bool replacing_current_cell = false;

    scan_worksheet_events(worksheet_xml, [&](const WorksheetEvent& event) {
        if (event.kind == WorksheetEventKind::CellStart) {
            const auto replacement = replacement_index.find(event.cell_reference);
            if (replacement != replacement_index.end()) {
                callback(WorksheetTransformAction { WorksheetTransformActionKind::ReplaceCell,
                    event.raw_xml,
                    event.row_number,
                    event.cell_reference,
                    replacement->second });
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
    });

    WorksheetTransformSummary summary;
    summary.matched_replacement_count = matched_replacements.size();
    for (const auto& [cell_reference, _] : replacement_index) {
        if (!matched_replacements.contains(cell_reference)) {
            summary.missing_cell_references.push_back(cell_reference);
        }
    }
    return summary;
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

} // namespace fastxlsx::detail
