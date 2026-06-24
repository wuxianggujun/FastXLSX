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

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::size_t max_excel_columns = 16384U;

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

void WorksheetEditor::set_cells(std::initializer_list<WorksheetCellUpdate> cells)
{
    set_cells(std::span<const WorksheetCellUpdate>(cells.begin(), cells.size()));
}

void WorksheetEditor::append_row(std::span<const CellValue> values)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        if (values.size() > max_excel_columns) {
            throw FastXlsxError(
                "WorksheetEditor::append_row() cannot append more than 16384 cells");
        }
        for (const CellValue& value : values) {
            if (has_non_default_style(value)) {
                throw FastXlsxError(
                    "WorksheetEditor::append_row() does not support non-default StyleId values");
            }
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }
        if (values.empty()) {
            state.clear_last_edit_error();
            return;
        }

        const detail::CellStore& current_store = session->store();
        std::uint32_t append_row = 1;
        if (!current_store.empty()) {
            const std::uint32_t last_row = current_store.records().rbegin()->first.row;
            if (last_row >= max_excel_rows) {
                throw FastXlsxError(
                    "WorksheetEditor::append_row() cannot append past Excel row 1048576");
            }
            append_row = last_row + 1U;
        }

        detail::CellStore staged_store = current_store;
        for (std::size_t index = 0; index < values.size(); ++index) {
            staged_store.set_cell(
                append_row, static_cast<std::uint32_t>(index + 1U), values[index]);
        }

        session->replace_store(std::move(staged_store));
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::append_row(std::initializer_list<CellValue> values)
{
    append_row(std::span<const CellValue>(values.begin(), values.size()));
}

void WorksheetEditor::set_row(std::uint32_t row, std::span<const CellValue> values)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, 1);
        if (values.size() > max_excel_columns) {
            throw FastXlsxError(
                "WorksheetEditor::set_row() cannot set more than 16384 cells");
        }
        for (const CellValue& value : values) {
            if (has_non_default_style(value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_row() does not support non-default StyleId values");
            }
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        const detail::CellStore& current_store = session->store();
        std::vector<detail::CellPosition> row_positions;
        for (const auto& [position, record] : current_store.records()) {
            (void)record;
            if (position.row == row) {
                row_positions.push_back(position);
            }
        }
        if (values.empty() && row_positions.empty()) {
            state.clear_last_edit_error();
            return;
        }

        detail::CellStore staged_store = current_store;
        for (const detail::CellPosition& position : row_positions) {
            staged_store.erase_cell(position.row, position.column);
        }
        for (std::size_t index = 0; index < values.size(); ++index) {
            staged_store.set_cell(
                row, static_cast<std::uint32_t>(index + 1U), values[index]);
        }

        session->replace_store(std::move(staged_store));
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_row(std::uint32_t row, std::initializer_list<CellValue> values)
{
    set_row(row, std::span<const CellValue>(values.begin(), values.size()));
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

void WorksheetEditor::set_cell_values(std::span<const WorksheetCellUpdate> cells)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        for (const WorksheetCellUpdate& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(
                cell.reference.row, cell.reference.column);
            if (has_non_default_style(cell.value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_cell_values() does not support caller-supplied non-default StyleId values");
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
            const detail::CellRecord* existing =
                staged_store.try_cell(cell.reference.row, cell.reference.column);
            staged_store.set_cell(cell.reference.row, cell.reference.column,
                with_preserved_source_style(cell.value, existing));
        }

        session->replace_store(std::move(staged_store));
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_cell_values(std::initializer_list<WorksheetCellUpdate> cells)
{
    set_cell_values(std::span<const WorksheetCellUpdate>(cells.begin(), cells.size()));
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

void WorksheetEditor::clear_cell_values(std::span<const WorksheetCellReference> cells)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        for (const WorksheetCellReference& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(cell.row, cell.column);
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
        bool cleared_any_cell = false;
        for (const WorksheetCellReference& cell : cells) {
            const detail::CellRecord* existing = staged_store.try_cell(cell.row, cell.column);
            if (existing == nullptr) {
                continue;
            }

            CellValue value = CellValue::blank();
            if (existing->style_id.has_value()) {
                value = value.with_style(*existing->style_id);
            }
            staged_store.set_cell(cell.row, cell.column, value);
            cleared_any_cell = true;
        }

        if (cleared_any_cell) {
            session->replace_store(std::move(staged_store));
        }
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_values(std::initializer_list<WorksheetCellReference> cells)
{
    clear_cell_values(std::span<const WorksheetCellReference>(cells.begin(), cells.size()));
}

void WorksheetEditor::erase_cells(CellRange range)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(range);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->erase_cells(range);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::erase_cells(std::span<const WorksheetCellReference> cells)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        for (const WorksheetCellReference& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(cell.row, cell.column);
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
        bool erased_any_cell = false;
        for (const WorksheetCellReference& cell : cells) {
            if (staged_store.try_cell(cell.row, cell.column) == nullptr) {
                continue;
            }

            staged_store.erase_cell(cell.row, cell.column);
            erased_any_cell = true;
        }

        if (erased_any_cell) {
            session->replace_store(std::move(staged_store));
        }
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::erase_cells(std::initializer_list<WorksheetCellReference> cells)
{
    erase_cells(std::span<const WorksheetCellReference>(cells.begin(), cells.size()));
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
