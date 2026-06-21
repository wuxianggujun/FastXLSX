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
    [[nodiscard]] std::optional<std::string> source_name_for_current(
        std::string_view sheet_name) const;

    void record_rename(std::string_view old_current_name, std::string_view new_current_name);

private:
    std::vector<std::string> source_names_;
    std::map<std::string, std::string> planned_names_by_source_;
};

} // namespace fastxlsx::detail
