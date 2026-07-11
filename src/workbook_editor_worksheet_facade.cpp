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

bool WorksheetEditor::contains_cell(std::uint32_t row, std::uint32_t column) const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    detail::validate_worksheet_editor_cell_coordinate(row, column);
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->try_cell(row, column) != nullptr;
}

bool WorksheetEditor::contains_cell(std::string_view cell_reference) const
{
    const detail::WorksheetEditorCellCoordinate coordinate =
        detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
    return contains_cell(coordinate.row, coordinate.column);
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
        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(cells.size());
        for (const WorksheetCellUpdate& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(
                cell.reference.row, cell.reference.column);
            if (has_non_default_style(cell.value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_cells() does not support non-default StyleId values");
            }
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {cell.reference.row, cell.reference.column},
                &cell.value,
            });
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

        session->set_cells(updates);
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

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {
                    append_row,
                    static_cast<std::uint32_t>(index + 1U),
                },
                &values[index],
            });
        }

        session->set_cells(updates);
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

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {row, static_cast<std::uint32_t>(index + 1U)},
                &values[index],
            });
        }

        session->apply_cell_edits(row_positions, updates);
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

void WorksheetEditor::set_column(std::uint32_t column, std::span<const CellValue> values)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, column);
        if (values.size() > static_cast<std::size_t>(max_excel_rows)) {
            throw FastXlsxError(
                "WorksheetEditor::set_column() cannot set more than 1048576 cells");
        }
        for (const CellValue& value : values) {
            if (has_non_default_style(value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_column() does not support non-default StyleId values");
            }
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        const detail::CellStore& current_store = session->store();
        std::vector<detail::CellPosition> column_positions;
        for (const auto& [position, record] : current_store.records()) {
            (void)record;
            if (position.column == column) {
                column_positions.push_back(position);
            }
        }
        if (values.empty() && column_positions.empty()) {
            state.clear_last_edit_error();
            return;
        }

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {static_cast<std::uint32_t>(index + 1U), column},
                &values[index],
            });
        }

        session->apply_cell_edits(column_positions, updates);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_column(std::uint32_t column, std::initializer_list<CellValue> values)
{
    set_column(column, std::span<const CellValue>(values.begin(), values.size()));
}

void WorksheetEditor::erase_row(std::uint32_t row)
{
    erase_rows(row, row);
}

void WorksheetEditor::erase_rows(std::uint32_t first_row, std::uint32_t last_row)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(first_row, 1);
        detail::validate_worksheet_editor_cell_coordinate(last_row, 1);
        if (first_row > last_row) {
            throw FastXlsxError(
                "WorksheetEditor::erase_rows() requires first_row <= last_row");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->erase_cells(CellRange {
            first_row,
            1,
            last_row,
            static_cast<std::uint32_t>(max_excel_columns),
        });
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::erase_column(std::uint32_t column)
{
    erase_columns(column, column);
}

void WorksheetEditor::erase_columns(std::uint32_t first_column, std::uint32_t last_column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, first_column);
        detail::validate_worksheet_editor_cell_coordinate(1, last_column);
        if (first_column > last_column) {
            throw FastXlsxError(
                "WorksheetEditor::erase_columns() requires first_column <= last_column");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->erase_cells(CellRange {
            1,
            first_column,
            max_excel_rows,
            last_column,
        });
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::insert_rows(std::uint32_t first_row, std::uint32_t row_count)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(first_row, 1);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->insert_rows(first_row, row_count);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::delete_rows(std::uint32_t first_row, std::uint32_t row_count)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(first_row, 1);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->delete_rows(first_row, row_count);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::insert_columns(std::uint32_t first_column, std::uint32_t column_count)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, first_column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->insert_columns(first_column, column_count);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::delete_columns(std::uint32_t first_column, std::uint32_t column_count)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, first_column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->delete_columns(first_column, column_count);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cells(CellRange source, WorksheetCellReference destination)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(source);
        detail::validate_worksheet_editor_cell_coordinate(
            destination.row, destination.column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->copy_cells(source,
            detail::CellPosition {destination.row, destination.column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cells(std::string_view source_range_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_range(source_range_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    copy_cells(source,
        WorksheetCellReference {destination.row, destination.column});
}

void WorksheetEditor::copy_cells_from(const WorksheetEditor& source_sheet,
    CellRange source, WorksheetCellReference destination)
{
    WorkbookEditor& destination_owner = owner();
    WorkbookEditor::Impl& state = *destination_owner.impl_;
    try {
        const WorkbookEditor& source_owner = source_sheet.owner();
        if (&source_owner != &destination_owner) {
            throw FastXlsxError(
                "WorksheetEditor::copy_cells_from() requires both worksheets to belong to the same WorkbookEditor");
        }
        detail::validate_worksheet_editor_cell_range(source);
        detail::validate_worksheet_editor_cell_coordinate(
            destination.row, destination.column);

        const detail::MaterializedWorksheetSession* source_session =
            state.materialized_sessions.try_session(source_sheet.planned_name_);
        detail::MaterializedWorksheetSession* destination_session =
            state.materialized_sessions.try_session(planned_name_);
        if (source_session == nullptr || destination_session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        destination_session->copy_cells_from(*source_session, source,
            detail::CellPosition {destination.row, destination.column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cells_from(const WorksheetEditor& source_sheet,
    std::string_view source_range_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_range(source_range_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    copy_cells_from(source_sheet, source,
        WorksheetCellReference {destination.row, destination.column});
}

void WorksheetEditor::move_cells(CellRange source, WorksheetCellReference destination)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(source);
        detail::validate_worksheet_editor_cell_coordinate(
            destination.row, destination.column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->move_cells(source,
            detail::CellPosition {destination.row, destination.column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::move_cells(std::string_view source_range_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_range(source_range_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    move_cells(source,
        WorksheetCellReference {destination.row, destination.column});
}

void WorksheetEditor::copy_cell_style(std::uint32_t source_row,
    std::uint32_t source_column, std::uint32_t destination_row,
    std::uint32_t destination_column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(source_row, source_column);
        detail::validate_worksheet_editor_cell_coordinate(
            destination_row, destination_column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->copy_cell_style(
            detail::CellPosition {source_row, source_column},
            detail::CellPosition {destination_row, destination_column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cell_style(std::string_view source_cell_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    detail::WorksheetEditorCellCoordinate source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_reference(source_cell_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    copy_cell_style(
        source.row, source.column, destination.row, destination.column);
}

void WorksheetEditor::copy_cell_styles(
    CellRange source, WorksheetCellReference destination)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(source);
        detail::validate_worksheet_editor_cell_coordinate(
            destination.row, destination.column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->copy_cell_styles(source,
            detail::CellPosition {destination.row, destination.column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cell_styles(std::string_view source_range_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_range(source_range_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    copy_cell_styles(source,
        WorksheetCellReference {destination.row, destination.column});
}

void WorksheetEditor::copy_cell_styles_from(const WorksheetEditor& source_sheet,
    CellRange source, WorksheetCellReference destination)
{
    WorkbookEditor& destination_owner = owner();
    WorkbookEditor::Impl& state = *destination_owner.impl_;
    try {
        const WorkbookEditor& source_owner = source_sheet.owner();
        if (&source_owner != &destination_owner) {
            throw FastXlsxError(
                "WorksheetEditor::copy_cell_styles_from() requires both worksheets to belong to the same WorkbookEditor");
        }
        detail::validate_worksheet_editor_cell_range(source);
        detail::validate_worksheet_editor_cell_coordinate(
            destination.row, destination.column);

        const detail::MaterializedWorksheetSession* source_session =
            state.materialized_sessions.try_session(source_sheet.planned_name_);
        detail::MaterializedWorksheetSession* destination_session =
            state.materialized_sessions.try_session(planned_name_);
        if (source_session == nullptr || destination_session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        destination_session->copy_cell_styles_from(*source_session, source,
            detail::CellPosition {destination.row, destination.column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::copy_cell_styles_from(const WorksheetEditor& source_sheet,
    std::string_view source_range_reference,
    std::string_view destination_cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange source {};
    detail::WorksheetEditorCellCoordinate destination {};
    try {
        source = detail::parse_worksheet_editor_a1_cell_range(source_range_reference);
        destination =
            detail::parse_worksheet_editor_a1_cell_reference(destination_cell_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    copy_cell_styles_from(source_sheet, source,
        WorksheetCellReference {destination.row, destination.column});
}

void WorksheetEditor::clear_cell_style(std::uint32_t row, std::uint32_t column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, column);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_style(detail::CellPosition {row, column});
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_style(std::string_view cell_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        const detail::WorksheetEditorCellCoordinate coordinate =
            detail::parse_worksheet_editor_a1_cell_reference(cell_reference);
        clear_cell_style(coordinate.row, coordinate.column);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_styles(CellRange range)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_range(range);

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_styles(range);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_cell_styles(std::string_view range_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange range {};
    try {
        range = detail::parse_worksheet_editor_a1_cell_range(range_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    clear_cell_styles(range);
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
        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(cells.size());
        for (const WorksheetCellUpdate& cell : cells) {
            detail::validate_worksheet_editor_cell_coordinate(
                cell.reference.row, cell.reference.column);
            if (has_non_default_style(cell.value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_cell_values() does not support caller-supplied non-default StyleId values");
            }
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {cell.reference.row, cell.reference.column},
                &cell.value,
            });
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

        session->set_cells(
            updates, detail::CellStoreBatchStylePolicy::PreserveExistingStyles);
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

void WorksheetEditor::set_row_values(std::uint32_t row, std::span<const CellValue> values)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(row, 1);
        if (values.size() > max_excel_columns) {
            throw FastXlsxError(
                "WorksheetEditor::set_row_values() cannot set more than 16384 cells");
        }
        for (const CellValue& value : values) {
            if (has_non_default_style(value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_row_values() does not support caller-supplied non-default StyleId values");
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

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const std::uint32_t column = static_cast<std::uint32_t>(index + 1U);
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {row, column},
                &values[index],
            });
        }

        session->set_cells(
            updates, detail::CellStoreBatchStylePolicy::PreserveExistingStyles);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_row_values(std::uint32_t row, std::initializer_list<CellValue> values)
{
    set_row_values(row, std::span<const CellValue>(values.begin(), values.size()));
}

void WorksheetEditor::set_column_values(std::uint32_t column, std::span<const CellValue> values)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, column);
        if (values.size() > static_cast<std::size_t>(max_excel_rows)) {
            throw FastXlsxError(
                "WorksheetEditor::set_column_values() cannot set more than 1048576 cells");
        }
        for (const CellValue& value : values) {
            if (has_non_default_style(value)) {
                throw FastXlsxError(
                    "WorksheetEditor::set_column_values() does not support caller-supplied non-default StyleId values");
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

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const std::uint32_t row = static_cast<std::uint32_t>(index + 1U);
            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {row, column},
                &values[index],
            });
        }

        session->set_cells(
            updates, detail::CellStoreBatchStylePolicy::PreserveExistingStyles);
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::set_column_values(
    std::uint32_t column, std::initializer_list<CellValue> values)
{
    set_column_values(column, std::span<const CellValue>(values.begin(), values.size()));
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

void WorksheetEditor::clear_cell_values()
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_values();
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_row(std::uint32_t row)
{
    clear_rows(row, row);
}

void WorksheetEditor::clear_rows(std::uint32_t first_row, std::uint32_t last_row)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(first_row, 1);
        detail::validate_worksheet_editor_cell_coordinate(last_row, 1);
        if (first_row > last_row) {
            throw FastXlsxError(
                "WorksheetEditor::clear_rows() requires first_row <= last_row");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_values(CellRange {
            first_row,
            1,
            last_row,
            static_cast<std::uint32_t>(max_excel_columns),
        });
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
}

void WorksheetEditor::clear_column(std::uint32_t column)
{
    clear_columns(column, column);
}

void WorksheetEditor::clear_columns(std::uint32_t first_column, std::uint32_t last_column)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::validate_worksheet_editor_cell_coordinate(1, first_column);
        detail::validate_worksheet_editor_cell_coordinate(1, last_column);
        if (first_column > last_column) {
            throw FastXlsxError(
                "WorksheetEditor::clear_columns() requires first_column <= last_column");
        }

        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->clear_cell_values(CellRange {
            1,
            first_column,
            max_excel_rows,
            last_column,
        });
        state.clear_last_edit_error();
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

void WorksheetEditor::clear_cell_values(std::string_view range_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange range {};
    try {
        range = detail::parse_worksheet_editor_a1_cell_range(range_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    clear_cell_values(range);
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

        std::vector<detail::CellStoreUpdate> updates;
        updates.reserve(cells.size());
        const CellValue blank_value = CellValue::blank();
        bool cleared_any_cell = false;
        for (const WorksheetCellReference& cell : cells) {
            if (session->try_cell(cell.row, cell.column) == nullptr) {
                continue;
            }

            updates.push_back(detail::CellStoreUpdate {
                detail::CellPosition {cell.row, cell.column},
                &blank_value,
            });
            cleared_any_cell = true;
        }

        if (cleared_any_cell) {
            session->set_cells(
                updates, detail::CellStoreBatchStylePolicy::PreserveExistingStyles);
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

void WorksheetEditor::erase_cells()
{
    WorkbookEditor::Impl& state = *owner().impl_;
    try {
        detail::MaterializedWorksheetSession* session =
            state.materialized_sessions.try_session(planned_name_);
        if (session == nullptr) {
            throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
        }

        session->erase_cells();
        state.clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }
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

void WorksheetEditor::erase_cells(std::string_view range_reference)
{
    WorkbookEditor::Impl& state = *owner().impl_;
    CellRange range {};
    try {
        range = detail::parse_worksheet_editor_a1_cell_range(range_reference);
    } catch (const FastXlsxError& error) {
        state.record_last_edit_error(error);
        throw;
    }

    erase_cells(range);
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

        std::vector<detail::CellPosition> erasures;
        erasures.reserve(cells.size());
        bool erased_any_cell = false;
        for (const WorksheetCellReference& cell : cells) {
            if (session->try_cell(cell.row, cell.column) == nullptr) {
                continue;
            }

            erasures.push_back(detail::CellPosition {cell.row, cell.column});
            erased_any_cell = true;
        }

        if (erased_any_cell) {
            session->apply_cell_edits(
                erasures, std::span<const detail::CellStoreUpdate>());
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

std::optional<CellRange> WorksheetEditor::used_range() const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }

    const auto& records = session->store().records();
    if (records.empty()) {
        return std::nullopt;
    }

    auto iterator = records.begin();
    CellRange range {
        iterator->first.row,
        iterator->first.column,
        iterator->first.row,
        iterator->first.column,
    };
    ++iterator;

    for (; iterator != records.end(); ++iterator) {
        const detail::CellPosition& position = iterator->first;
        if (position.row < range.first_row) {
            range.first_row = position.row;
        }
        if (position.column < range.first_column) {
            range.first_column = position.column;
        }
        if (position.row > range.last_row) {
            range.last_row = position.row;
        }
        if (position.column > range.last_column) {
            range.last_column = position.column;
        }
    }
    return range;
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

std::vector<WorksheetCellSnapshot> WorksheetEditor::row_cells(std::uint32_t row) const
{
    detail::validate_worksheet_editor_cell_coordinate(row, 1);
    return sparse_cells(CellRange {
        row,
        1,
        row,
        static_cast<std::uint32_t>(max_excel_columns),
    });
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::column_cells(std::uint32_t column) const
{
    detail::validate_worksheet_editor_cell_coordinate(1, column);
    return sparse_cells(CellRange {
        1,
        column,
        max_excel_rows,
        column,
    });
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::sparse_cells(
    std::string_view range_reference) const
{
    return sparse_cells(detail::parse_worksheet_editor_a1_cell_range(range_reference));
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::sparse_cells(
    std::span<const WorksheetCellReference> cells) const
{
    const WorkbookEditor::Impl& state = *owner().impl_;
    for (const WorksheetCellReference& cell : cells) {
        detail::validate_worksheet_editor_cell_coordinate(cell.row, cell.column);
    }

    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    if (cells.empty()) {
        return {};
    }

    std::vector<WorksheetCellSnapshot> snapshots;
    snapshots.reserve(cells.size());
    for (const WorksheetCellReference& cell : cells) {
        const detail::CellRecord* record = session->try_cell(cell.row, cell.column);
        if (record == nullptr) {
            continue;
        }

        snapshots.push_back(WorksheetCellSnapshot {
            cell,
            record->to_value(),
        });
    }
    return snapshots;
}

std::vector<WorksheetCellSnapshot> WorksheetEditor::sparse_cells(
    std::initializer_list<WorksheetCellReference> cells) const
{
    return sparse_cells(std::span<const WorksheetCellReference>(cells.begin(), cells.size()));
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
