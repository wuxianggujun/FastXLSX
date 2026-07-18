#pragma once

#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

[[nodiscard]] std::string workbook_editor_missing_planned_sheet_message(
    std::string_view sheet_name);

struct WorkbookEditorSheetCatalogEntry {
    std::string source_name;
    std::string planned_name;
    bool renamed = false;
    bool added = false;
};

class WorkbookEditorSheetCatalogPlan {
public:
    WorkbookEditorSheetCatalogPlan() = default;
    explicit WorkbookEditorSheetCatalogPlan(std::vector<std::string> source_names);

    [[nodiscard]] std::vector<std::string> source_names() const;
    [[nodiscard]] std::vector<std::string> current_names() const;
    [[nodiscard]] std::vector<WorkbookEditorSheetCatalogEntry> entries() const;

    [[nodiscard]] bool has_source(std::string_view sheet_name) const;
    [[nodiscard]] bool has_current(std::string_view sheet_name) const;
    [[nodiscard]] bool is_added_current(std::string_view sheet_name) const;
    [[nodiscard]] std::optional<std::string> source_name_for_current(
        std::string_view sheet_name) const;

    void record_add(std::string name);
    void record_rename(std::string_view old_current_name, std::string_view new_current_name);

    void swap(WorkbookEditorSheetCatalogPlan& other) noexcept
    {
        source_names_.swap(other.source_names_);
        planned_names_by_source_.swap(other.planned_names_by_source_);
        added_names_.swap(other.added_names_);
    }

    friend void swap(
        WorkbookEditorSheetCatalogPlan& left,
        WorkbookEditorSheetCatalogPlan& right) noexcept
    {
        left.swap(right);
    }

private:
    std::vector<std::string> source_names_;
    std::map<std::string, std::string> planned_names_by_source_;
    std::vector<std::string> added_names_;
};

} // namespace fastxlsx::detail
