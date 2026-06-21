#include <fastxlsx/workbook_editor.hpp>

#include "workbook_editor_state.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
namespace detail {

void testing_workbook_editor_materialize_source_sheet(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::string_view source_sheet_name)
{
    if (editor.impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }
    if (editor.impl_->has_pending_sheet_data_payload(planned_name)) {
        throw FastXlsxError(
            "cannot materialize planned worksheet session after replacing sheet data");
    }

    editor.impl_->materialized_sessions.materialize_from_workbook_sheet(
        editor.impl_->editor.reader(),
        std::string(planned_name),
        source_sheet_name,
        workbook_editor_cell_store_options_from_editor_options(editor.impl_->options));
}

void testing_workbook_editor_set_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column,
    const CellValue& value)
{
    if (editor.impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    MaterializedWorksheetSession* session =
        editor.impl_->materialized_sessions.try_session(planned_name);
    if (session == nullptr) {
        throw FastXlsxError("materialized worksheet session is missing");
    }
    session->set_cell(row, column, value);
}

void testing_workbook_editor_erase_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column)
{
    if (editor.impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    MaterializedWorksheetSession* session =
        editor.impl_->materialized_sessions.try_session(planned_name);
    if (session == nullptr) {
        throw FastXlsxError("materialized worksheet session is missing");
    }
    session->erase_cell(row, column);
}

void testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
    WorkbookEditor& editor)
{
    if (editor.impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    editor.impl_->flush_dirty_materialized_sessions_to_patch_plan();
}

std::size_t testing_workbook_editor_materialized_session_count(
    const WorkbookEditor& editor) noexcept
{
    return editor.impl_ == nullptr
        ? 0
        : editor.impl_->materialized_sessions.session_count();
}

std::size_t testing_workbook_editor_dirty_materialized_session_count(
    const WorkbookEditor& editor) noexcept
{
    return editor.impl_ == nullptr
        ? 0
        : editor.impl_->materialized_sessions.dirty_session_count();
}

bool testing_workbook_editor_has_materialized_session(
    const WorkbookEditor& editor, std::string_view planned_name) noexcept
{
    return editor.impl_ != nullptr &&
        editor.impl_->materialized_sessions.contains(planned_name);
}

std::vector<std::string> testing_workbook_editor_dirty_materialized_session_names(
    const WorkbookEditor& editor)
{
    if (editor.impl_ == nullptr) {
        return {};
    }

    std::vector<std::string> names;
    for (std::string_view name : editor.impl_->materialized_sessions.dirty_session_names()) {
        names.emplace_back(name);
    }
    return names;
}

} // namespace detail
#endif

} // namespace fastxlsx
