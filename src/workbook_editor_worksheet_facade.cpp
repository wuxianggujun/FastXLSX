#include <fastxlsx/workbook_editor.hpp>

#include "workbook_editor_state.hpp"
#include "workbook_editor_worksheet_access.hpp"

#include <fastxlsx/detail/materialized_worksheet_session.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx {

namespace {

[[nodiscard]] bool has_non_default_style(const CellValue& value) noexcept
{
    return value.has_style() && value.style_id().value() != 0;
}

[[nodiscard]] CellValue with_preserved_source_style(
    const CellValue& value, const detail::CellRecord* existing)
{
    if (existing == nullptr || !existing->style_id.has_value()) {
        return value;
    }
    return value.with_style(*existing->style_id);
}

} // namespace

WorksheetEditor::WorksheetEditor(
    WorkbookEditor* owner, std::string planned_name, std::uint64_t owner_generation)
    : owner_(owner)
    , planned_name_(std::move(planned_name))
    , owner_generation_(owner_generation)
{
}

std::string_view WorksheetEditor::name() const noexcept
{
    return planned_name_;
}

const WorkbookEditor& WorksheetEditor::owner() const
{
    if (owner_ == nullptr || owner_->handle_generation_ != owner_generation_) {
        throw FastXlsxError("WorksheetEditor is no longer attached to the current WorkbookEditor state");
    }
    if (owner_->impl_ == nullptr) {
        throw FastXlsxError("WorksheetEditor is not attached to an open WorkbookEditor");
    }
    if (owner_->impl_->materialized_sessions.try_session(planned_name_) == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return *owner_;
}

WorkbookEditor& WorksheetEditor::owner()
{
    if (owner_ == nullptr || owner_->handle_generation_ != owner_generation_) {
        throw FastXlsxError("WorksheetEditor is no longer attached to the current WorkbookEditor state");
    }
    if (owner_->impl_ == nullptr) {
        throw FastXlsxError("WorksheetEditor is not attached to an open WorkbookEditor");
    }
    if (owner_->impl_->materialized_sessions.try_session(planned_name_) == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return *owner_;
}

std::optional<CellValue> WorksheetEditor::try_cell(
    std::uint32_t row, std::uint32_t column) const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    detail::validate_worksheet_editor_cell_coordinate(row, column);
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }

    const detail::CellRecord* record = session->try_cell(row, column);
    if (record == nullptr) {
        return std::nullopt;
    }
    return record->to_value();
}

std::optional<CellValue> WorksheetEditor::try_cell(std::string_view cell_reference) const
{
    const detail::WorksheetEditorCellCoordinate coordinate =
        detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
    return try_cell(coordinate.row, coordinate.column);
}

CellValue WorksheetEditor::get_cell(std::uint32_t row, std::uint32_t column) const
{
    std::optional<CellValue> value = try_cell(row, column);
    if (!value.has_value()) {
        throw FastXlsxError("WorksheetEditor cell is not present in materialized worksheet");
    }
    return *value;
}

CellValue WorksheetEditor::get_cell(std::string_view cell_reference) const
{
    const detail::WorksheetEditorCellCoordinate coordinate =
        detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
    return get_cell(coordinate.row, coordinate.column);
}

void WorksheetEditor::set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, column);
        if (has_non_default_style(value)) {
            throw FastXlsxError(
                "WorksheetEditor::set_cell() does not support non-default StyleId values");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }
        session->set_cell(row, column, value);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_cells(std::span<const WorksheetCellUpdate> cells)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        for (const WorksheetCellUpdate& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(
                cell.reference.row, cell.reference.column);
            if (has_non_default_style(cell.value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_cells() does not support non-default StyleId values");
            }
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }
        if (cells.empty()) {
            state.clear_last_edit_error();
            return;
        }

        detail::CellStore staged_store = session->store();
        for (const WorksheetCellUpdate& cell : cells) {
            staged_store.set_cell(cell.reference.row, cell.reference.column, cell.value);
        }

        session->replace_store(std::move(staged_store));
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_cell_value(
    std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, column);
        if (has_non_default_style(value)) {
            throw FastXlsxError(
                "WorksheetEditor::set_cell_value() does not support caller-supplied non-default StyleId values");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        const detail::CellRecord* existing = session->try_cell(row, column);
        session->set_cell(row, column, with_preserved_source_style(value, existing));
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_cell(std::string_view cell_reference, const CellValue& value)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        const detail::WorksheetEditorCellCoordinate coordinate =
            detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
        set_cell(coordinate.row, coordinate.column, value);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_cell_value(std::string_view cell_reference, const CellValue& value)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        const detail::WorksheetEditorCellCoordinate coordinate =
            detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
        set_cell_value(coordinate.row, coordinate.column, value);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_value(std::uint32_t row, std::uint32_t column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_value(row, column);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_value(std::string_view cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        const detail::WorksheetEditorCellCoordinate coordinate =
            detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
        clear_cell_value(coordinate.row, coordinate.column);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_values(CellRange range)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(range);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_values(range);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::erase_cell(std::uint32_t row, std::uint32_t column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, column);
        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }
        session->erase_cell(row, column);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::erase_cell(std::string_view cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        const detail::WorksheetEditorCellCoordinate coordinate =
            detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
        erase_cell(coordinate.row, coordinate.column);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

bool WorksheetEditor::has_pending_changes() const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->dirty();
}

std::size_t WorksheetEditor::cell_count() const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->cell_count();
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::sparse_cells() const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }

    const std::vector<detail::MaterializedCellSnapshot> internal_snapshots =
        session->sparse_cell_snapshots();
    return detail::public_snapshots_from_materialized_cells(internal_snapshots);
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::sparse_cells(CellRange range) const
{
    detail::validate_worksheet_editor_cell_range(range);

    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }

    const std::vector<detail::MaterializedCellSnapshot> internal_snapshots =
        session->sparse_cell_snapshots(range);
    return detail::public_snapshots_from_materialized_cells(internal_snapshots);
}

std::size_t WorksheetEditor::estimated_memory_usage() const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->estimated_memory_usage();
}

} // namespace fastxlsx
