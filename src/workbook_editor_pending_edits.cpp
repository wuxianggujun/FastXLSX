#include "workbook_editor_pending_edits.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

bool WorkbookEditorPendingSheetDataPayloads::empty() const noexcept
{
    return payloads_.empty();
}

bool WorkbookEditorPendingSheetDataPayloads::contains(std::string_view sheet_name)
    const noexcept
{
    return find(sheet_name) != nullptr;
}

const WorkbookEditorPendingSheetDataPayloadDiagnostic*
WorkbookEditorPendingSheetDataPayloads::find(std::string_view sheet_name) const noexcept
{
    for (const auto& [pending_sheet_name, diagnostic] : payloads_) {
        if (pending_sheet_name == sheet_name) {
            return &diagnostic;
        }
    }
    return nullptr;
}

std::size_t WorkbookEditorPendingSheetDataPayloads::cell_count() const noexcept
{
    std::size_t total = 0;
    for (const auto& [sheet_name, diagnostic] : payloads_) {
        (void)sheet_name;
        total += diagnostic.cell_count;
    }
    return total;
}

std::size_t WorkbookEditorPendingSheetDataPayloads::estimated_memory_usage()
    const noexcept
{
    std::size_t total = 0;
    for (const auto& [sheet_name, diagnostic] : payloads_) {
        (void)sheet_name;
        total += diagnostic.estimated_memory_usage;
    }
    return total;
}

std::vector<std::string> WorkbookEditorPendingSheetDataPayloads::worksheet_names(
    const std::vector<std::string>& current_catalog_names) const
{
    std::vector<std::string> names;
    for (const std::string& sheet_name : current_catalog_names) {
        if (contains(sheet_name)) {
            names.push_back(sheet_name);
        }
    }

    for (const auto& [sheet_name, diagnostic] : payloads_) {
        (void)diagnostic;
        if (std::find(names.begin(), names.end(), sheet_name) == names.end()) {
            names.push_back(sheet_name);
        }
    }
    return names;
}

void WorkbookEditorPendingSheetDataPayloads::record(
    std::string sheet_name,
    std::size_t cell_count,
    std::size_t estimated_memory_usage)
{
    payloads_[std::move(sheet_name)] = WorkbookEditorPendingSheetDataPayloadDiagnostic {
        cell_count,
        estimated_memory_usage,
    };
}

void WorkbookEditorPendingSheetDataPayloads::migrate(
    std::string_view old_name,
    std::string_view new_name)
{
    if (old_name == new_name) {
        return;
    }

    auto pending_payload = payloads_.extract(std::string(old_name));
    if (pending_payload.empty()) {
        return;
    }

    pending_payload.key() = std::string(new_name);
    auto insert_result = payloads_.insert(std::move(pending_payload));
    if (!insert_result.inserted && !insert_result.node.empty()) {
        insert_result.position->second = insert_result.node.mapped();
    }
}

} // namespace fastxlsx::detail
