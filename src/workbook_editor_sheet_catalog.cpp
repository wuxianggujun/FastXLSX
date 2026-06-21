#include "workbook_editor_sheet_catalog.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

std::string workbook_editor_missing_planned_sheet_message(std::string_view sheet_name)
{
    std::string message = "WorkbookEditor worksheet is not present in current planned catalog";
    if (!sheet_name.empty()) {
        message += ": ";
        message += sheet_name;
    }
    return message;
}

WorkbookEditorSheetCatalogPlan::WorkbookEditorSheetCatalogPlan(
    std::vector<std::string> source_names)
    : source_names_(std::move(source_names))
{
}

std::vector<std::string> WorkbookEditorSheetCatalogPlan::source_names() const
{
    return source_names_;
}

std::vector<std::string> WorkbookEditorSheetCatalogPlan::current_names() const
{
    std::vector<std::string> names;
    names.reserve(source_names_.size());
    for (const std::string& source_name : source_names_) {
        const auto planned_name = planned_names_by_source_.find(source_name);
        names.push_back(planned_name == planned_names_by_source_.end()
                ? source_name
                : planned_name->second);
    }
    return names;
}

std::vector<WorkbookEditorSheetCatalogEntry> WorkbookEditorSheetCatalogPlan::entries() const
{
    std::vector<WorkbookEditorSheetCatalogEntry> catalog;
    catalog.reserve(source_names_.size());
    for (const std::string& source_name : source_names_) {
        const auto planned_name = planned_names_by_source_.find(source_name);
        const std::string& current_name = planned_name == planned_names_by_source_.end()
            ? source_name
            : planned_name->second;
        catalog.push_back(WorkbookEditorSheetCatalogEntry {
            source_name,
            current_name,
            current_name != source_name,
        });
    }
    return catalog;
}

bool WorkbookEditorSheetCatalogPlan::has_source(std::string_view sheet_name) const
{
    return std::find(source_names_.begin(), source_names_.end(), sheet_name)
        != source_names_.end();
}

bool WorkbookEditorSheetCatalogPlan::has_current(std::string_view sheet_name) const
{
    for (const WorkbookEditorSheetCatalogEntry& entry : entries()) {
        if (entry.planned_name == sheet_name) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> WorkbookEditorSheetCatalogPlan::source_name_for_current(
    std::string_view sheet_name) const
{
    for (const WorkbookEditorSheetCatalogEntry& entry : entries()) {
        if (entry.planned_name == sheet_name) {
            return entry.source_name;
        }
    }
    return std::nullopt;
}

void WorkbookEditorSheetCatalogPlan::record_rename(
    std::string_view old_current_name,
    std::string_view new_current_name)
{
    for (const WorkbookEditorSheetCatalogEntry& entry : entries()) {
        if (entry.planned_name != old_current_name) {
            continue;
        }

        if (new_current_name == entry.source_name) {
            planned_names_by_source_.erase(entry.source_name);
        } else {
            planned_names_by_source_[entry.source_name] = std::string(new_current_name);
        }
        return;
    }

    // Preserve the old facade behavior if package metadata and this local plan
    // ever diverge; valid public rename paths should always return above.
    planned_names_by_source_[std::string(old_current_name)] = std::string(new_current_name);
}

} // namespace fastxlsx::detail
