#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

struct WorkbookEditorPendingSheetDataPayloadDiagnostic {
    std::size_t cell_count = 0;
    std::size_t estimated_memory_usage = 0;
};

class WorkbookEditorPendingSheetDataPayloads {
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(std::string_view sheet_name) const noexcept;
    [[nodiscard]] const WorkbookEditorPendingSheetDataPayloadDiagnostic* find(
        std::string_view sheet_name) const noexcept;

    [[nodiscard]] std::size_t cell_count() const noexcept;
    [[nodiscard]] std::size_t estimated_memory_usage() const noexcept;
    [[nodiscard]] std::vector<std::string> worksheet_names(
        const std::vector<std::string>& current_catalog_names) const;

    void record(
        std::string sheet_name,
        std::size_t cell_count,
        std::size_t estimated_memory_usage);
    void migrate(std::string_view old_name, std::string_view new_name);

    void swap(WorkbookEditorPendingSheetDataPayloads& other) noexcept
    {
        payloads_.swap(other.payloads_);
    }

    friend void swap(
        WorkbookEditorPendingSheetDataPayloads& left,
        WorkbookEditorPendingSheetDataPayloads& right) noexcept
    {
        left.swap(right);
    }

private:
    std::map<std::string, WorkbookEditorPendingSheetDataPayloadDiagnostic> payloads_;
};

} // namespace fastxlsx::detail
