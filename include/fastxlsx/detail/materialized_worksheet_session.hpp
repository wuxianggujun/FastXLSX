#pragma once

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/formula.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

struct MaterializedCellSnapshot {
    CellPosition position;
    CellValue value;
};

/// Internal state holder for one explicitly materialized worksheet.
///
/// This is a private building block for a future WorkbookEditor-owned
/// WorksheetEditor session. It is intentionally not a public random-edit API:
/// callers still need a higher-level workbook facade to resolve sheet names,
/// enforce operation-mixing rules, and persist dirty stores through save_as().
class MaterializedWorksheetSession {
public:
    MaterializedWorksheetSession(std::string planned_name, CellStore store)
        : planned_name_(std::move(planned_name))
        , store_(std::move(store))
    {
    }

    [[nodiscard]] std::string_view planned_name() const noexcept
    {
        return planned_name_;
    }

    [[nodiscard]] const CellStoreOptions& options() const noexcept
    {
        return store_.options();
    }

    [[nodiscard]] bool options_match(const CellStoreOptions& options) const noexcept
    {
        return store_.options().materialization_policy == options.materialization_policy
            && store_.options().max_cells == options.max_cells
            && store_.options().memory_budget_bytes == options.memory_budget_bytes;
    }

    [[nodiscard]] bool dirty() const noexcept
    {
        return dirty_;
    }

    void clear_dirty() noexcept
    {
        dirty_ = false;
    }

    void set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
    {
        store_.set_cell(row, column, value);
        dirty_ = true;
    }

    void set_cells(
        std::span<const CellStoreUpdate> updates,
        CellStoreBatchStylePolicy style_policy = CellStoreBatchStylePolicy::Replace)
    {
        store_.set_cells(updates, style_policy);
        dirty_ = dirty_ || !updates.empty();
    }

    void apply_cell_edits(
        std::span<const CellPosition> erasures,
        std::span<const CellStoreUpdate> updates,
        CellStoreBatchStylePolicy style_policy = CellStoreBatchStylePolicy::Replace)
    {
        dirty_ = store_.apply_cell_edits(erasures, updates, style_policy) || dirty_;
    }

    void replace_store(CellStore store)
    {
        store_ = std::move(store);
        dirty_ = true;
    }

    void replace_formula_text(
        std::uint32_t row, std::uint32_t column, std::string formula_text)
    {
        const CellRecord* existing = store_.try_cell(row, column);
        if (existing == nullptr || existing->kind != CellValueKind::Formula) {
            throw FastXlsxError(
                "materialized worksheet formula rewrite target is not a formula cell");
        }

        CellValue value = CellValue::formula(std::move(formula_text));
        if (existing->style_id.has_value()) {
            value = value.with_style(*existing->style_id);
        }
        store_.set_cell(row, column, value);
        dirty_ = true;
    }

    void erase_cell(std::uint32_t row, std::uint32_t column)
    {
        const bool had_record = store_.try_cell(row, column) != nullptr;
        store_.erase_cell(row, column);
        dirty_ = dirty_ || had_record;
    }

    void clear_cell_value(std::uint32_t row, std::uint32_t column)
    {
        const CellRecord* existing = store_.try_cell(row, column);
        if (existing == nullptr) {
            return;
        }

        CellValue value = CellValue::blank();
        if (existing->style_id.has_value()) {
            value = value.with_style(*existing->style_id);
        }
        store_.set_cell(row, column, value);
        dirty_ = true;
    }

    void clear_cell_values()
    {
        std::vector<CellStoreUpdate> updates;
        updates.reserve(store_.cell_count());
        const CellValue blank_value = CellValue::blank();
        for (const auto& [position, record] : store_.records()) {
            (void)record;
            updates.push_back(CellStoreUpdate {position, &blank_value});
        }

        if (updates.empty()) {
            return;
        }

        set_cells(updates, CellStoreBatchStylePolicy::PreserveExistingStyles);
    }

    void clear_cell_values(const CellRange& range)
    {
        std::vector<CellStoreUpdate> updates;
        const CellValue blank_value = CellValue::blank();
        for (const auto& [position, record] : store_.records()) {
            (void)record;
            if (position.row < range.first_row || position.row > range.last_row ||
                position.column < range.first_column || position.column > range.last_column) {
                continue;
            }
            updates.push_back(CellStoreUpdate {position, &blank_value});
        }

        if (updates.empty()) {
            return;
        }

        set_cells(updates, CellStoreBatchStylePolicy::PreserveExistingStyles);
    }

    void erase_cells(const CellRange& range)
    {
        std::vector<CellPosition> positions;
        for (const auto& [position, record] : store_.records()) {
            (void)record;
            if (position.row < range.first_row || position.row > range.last_row ||
                position.column < range.first_column || position.column > range.last_column) {
                continue;
            }
            positions.push_back(position);
        }

        if (positions.empty()) {
            return;
        }
        apply_cell_edits(positions, std::span<const CellStoreUpdate>());
    }

    void erase_cells()
    {
        std::vector<CellPosition> positions;
        positions.reserve(store_.cell_count());
        for (const auto& [position, record] : store_.records()) {
            (void)record;
            positions.push_back(position);
        }

        if (positions.empty()) {
            return;
        }
        apply_cell_edits(positions, std::span<const CellStoreUpdate>());
    }

    void insert_rows(std::uint32_t first_row, std::uint32_t row_count)
    {
        validate_row_span("MaterializedWorksheetSession::insert_rows()", first_row, row_count);
        if (row_count == 0 || store_.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> shifted_records;
        bool shifted_any_record = false;
        const FormulaStructuralEdit structural_edit {
            FormulaStructuralEditKind::InsertRows, first_row, row_count};
        for (const auto& [position, record] : store_.records()) {
            CellPosition shifted_position = position;
            if (position.row >= first_row) {
                if (position.row > max_excel_rows - row_count) {
                    throw FastXlsxError(
                        "MaterializedWorksheetSession::insert_rows() cannot shift cells past Excel row 1048576");
                }
                shifted_position.row = position.row + row_count;
                shifted_any_record = true;
            }
            shifted_any_record =
                insert_shifted_record(shifted_records, position, shifted_position, record,
                    structural_edit, "MaterializedWorksheetSession::insert_rows()")
                || shifted_any_record;
        }

        if (!shifted_any_record) {
            return;
        }
        store_.replace_records(std::move(shifted_records));
        dirty_ = true;
    }

    void delete_rows(std::uint32_t first_row, std::uint32_t row_count)
    {
        validate_row_span("MaterializedWorksheetSession::delete_rows()", first_row, row_count);
        if (row_count == 0 || store_.empty()) {
            return;
        }

        const std::uint32_t last_deleted_row = first_row + row_count - 1U;
        std::map<CellPosition, CellRecord> shifted_records;
        bool changed_any_record = false;
        const FormulaStructuralEdit structural_edit {
            FormulaStructuralEditKind::DeleteRows, first_row, row_count};
        for (const auto& [position, record] : store_.records()) {
            if (position.row >= first_row && position.row <= last_deleted_row) {
                changed_any_record = true;
                continue;
            }

            CellPosition shifted_position = position;
            if (position.row > last_deleted_row) {
                shifted_position.row = position.row - row_count;
                changed_any_record = true;
            }
            changed_any_record =
                insert_shifted_record(shifted_records, position, shifted_position, record,
                    structural_edit, "MaterializedWorksheetSession::delete_rows()")
                || changed_any_record;
        }

        if (!changed_any_record) {
            return;
        }
        store_.replace_records(std::move(shifted_records));
        dirty_ = true;
    }

    void insert_columns(std::uint32_t first_column, std::uint32_t column_count)
    {
        validate_column_span(
            "MaterializedWorksheetSession::insert_columns()", first_column, column_count);
        if (column_count == 0 || store_.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> shifted_records;
        bool shifted_any_record = false;
        const FormulaStructuralEdit structural_edit {
            FormulaStructuralEditKind::InsertColumns, first_column, column_count};
        for (const auto& [position, record] : store_.records()) {
            CellPosition shifted_position = position;
            if (position.column >= first_column) {
                if (position.column > max_excel_columns - column_count) {
                    throw FastXlsxError(
                        "MaterializedWorksheetSession::insert_columns() cannot shift cells past Excel column 16384");
                }
                shifted_position.column = position.column + column_count;
                shifted_any_record = true;
            }
            shifted_any_record =
                insert_shifted_record(shifted_records, position, shifted_position, record,
                    structural_edit, "MaterializedWorksheetSession::insert_columns()")
                || shifted_any_record;
        }

        if (!shifted_any_record) {
            return;
        }
        store_.replace_records(std::move(shifted_records));
        dirty_ = true;
    }

    void delete_columns(std::uint32_t first_column, std::uint32_t column_count)
    {
        validate_column_span(
            "MaterializedWorksheetSession::delete_columns()", first_column, column_count);
        if (column_count == 0 || store_.empty()) {
            return;
        }

        const std::uint32_t last_deleted_column = first_column + column_count - 1U;
        std::map<CellPosition, CellRecord> shifted_records;
        bool changed_any_record = false;
        const FormulaStructuralEdit structural_edit {
            FormulaStructuralEditKind::DeleteColumns, first_column, column_count};
        for (const auto& [position, record] : store_.records()) {
            if (position.column >= first_column && position.column <= last_deleted_column) {
                changed_any_record = true;
                continue;
            }

            CellPosition shifted_position = position;
            if (position.column > last_deleted_column) {
                shifted_position.column = position.column - column_count;
                changed_any_record = true;
            }
            changed_any_record =
                insert_shifted_record(shifted_records, position, shifted_position, record,
                    structural_edit, "MaterializedWorksheetSession::delete_columns()")
                || changed_any_record;
        }

        if (!changed_any_record) {
            return;
        }
        store_.replace_records(std::move(shifted_records));
        dirty_ = true;
    }

    void copy_cells(const CellRange& source, CellPosition destination)
    {
        transfer_cells(*this, source, destination, false, "copy_cells");
    }

    void copy_cells_from(const MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        transfer_cells(source_session, source, destination, false, "copy_cells_from");
    }

    void copy_cell_values(const CellRange& source, CellPosition destination)
    {
        copy_cell_values_from(*this, source, destination);
    }

    void copy_cell_values_from(const MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_values_from() requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_values_from() requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_values_from() destination range exceeds Excel limits");
        }
        if (&source_session == this && destination.row == source.first_row
            && destination.column == source.first_column) {
            return;
        }

        const FormulaTranslationDelta delta {
            static_cast<std::int64_t>(destination.row)
                - static_cast<std::int64_t>(source.first_row),
            static_cast<std::int64_t>(destination.column)
                - static_cast<std::int64_t>(source.first_column),
        };
        struct ValueTransfer {
            CellPosition destination;
            CellRecord record;
        };
        std::vector<ValueTransfer> transferred_records;
        for (const auto& [position, record] : source_session.store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition destination_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            CellRecord transferred_record = record;
            if (record.kind == CellValueKind::Formula) {
                transferred_record.text_value =
                    translate_formula_references(record.text_value, delta);
            }
            transferred_record.style_id.reset();
            const CellRecord* destination_record =
                store_.try_cell(destination_position.row, destination_position.column);
            if (destination_record != nullptr && destination_record->style_id.has_value()) {
                transferred_record.style_id = destination_record->style_id;
            }
            transferred_records.push_back(
                ValueTransfer {destination_position, std::move(transferred_record)});
        }
        if (transferred_records.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_records = store_.records();
        for (ValueTransfer& transferred : transferred_records) {
            next_records[transferred.destination] = std::move(transferred.record);
        }
        store_.replace_records(std::move(next_records));
        dirty_ = true;
    }

    void move_cell_values(const CellRange& source, CellPosition destination)
    {
        move_cell_values_from(*this, source, destination);
    }

    void move_cell_values_from(MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        if (&source_session == this) {
            move_cell_values_within_session(source, destination);
            return;
        }
        move_cell_values_between_sessions(source_session, source, destination);
    }

    void move_cells(const CellRange& source, CellPosition destination)
    {
        transfer_cells(*this, source, destination, true, "move_cells");
    }

    void move_cells_from(MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        if (&source_session == this) {
            move_cells(source, destination);
            return;
        }
        move_cells_between_sessions(source_session, source, destination);
    }

    void copy_cell_style(CellPosition source, CellPosition destination)
    {
        const CellRecord* source_record = store_.try_cell(source.row, source.column);
        if (source_record == nullptr) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_style() source cell is not represented");
        }
        const CellRecord* destination_record =
            store_.try_cell(destination.row, destination.column);
        if (destination_record == nullptr) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_style() destination cell is not represented");
        }
        if (same_style(source_record->style_id, destination_record->style_id)) {
            return;
        }

        CellValue destination_value = destination_record->to_value().without_style();
        if (source_record->style_id.has_value()) {
            destination_value = destination_value.with_style(*source_record->style_id);
        }
        store_.set_cell(destination.row, destination.column, destination_value);
        dirty_ = true;
    }

    void copy_cell_styles(const CellRange& source, CellPosition destination)
    {
        copy_cell_styles_from(*this, source, destination);
    }

    void copy_cell_styles_from(const MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_styles() requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_styles() requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::copy_cell_styles() destination range exceeds Excel limits");
        }
        if (&source_session == this && destination.row == source.first_row
            && destination.column == source.first_column) {
            return;
        }

        struct SourceStyleSnapshot {
            CellPosition position;
            std::optional<StyleId> style_id;
        };
        std::vector<SourceStyleSnapshot> source_styles;
        for (const auto& [position, record] : source_session.store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }
            source_styles.push_back(SourceStyleSnapshot {position, record.style_id});
        }
        if (source_styles.empty()) {
            return;
        }

        struct PlannedStyleUpdate {
            CellPosition position;
            CellValue value;
        };
        std::vector<PlannedStyleUpdate> planned_updates;
        planned_updates.reserve(source_styles.size());
        for (const SourceStyleSnapshot& source_style : source_styles) {
            const CellPosition destination_position {
                destination.row + (source_style.position.row - source.first_row),
                destination.column + (source_style.position.column - source.first_column),
            };
            const CellRecord* destination_record =
                store_.try_cell(destination_position.row, destination_position.column);
            if (destination_record == nullptr) {
                throw FastXlsxError(
                    "MaterializedWorksheetSession::copy_cell_styles() destination cell is not represented");
            }
            if (same_style(source_style.style_id, destination_record->style_id)) {
                continue;
            }

            CellValue destination_value = destination_record->to_value().without_style();
            if (source_style.style_id.has_value()) {
                destination_value = destination_value.with_style(*source_style.style_id);
            }
            planned_updates.push_back(
                PlannedStyleUpdate {destination_position, std::move(destination_value)});
        }
        if (planned_updates.empty()) {
            return;
        }

        std::vector<CellStoreUpdate> updates;
        updates.reserve(planned_updates.size());
        for (const PlannedStyleUpdate& update : planned_updates) {
            updates.push_back(CellStoreUpdate {update.position, &update.value});
        }
        set_cells(updates);
    }

    void move_cell_style(CellPosition source, CellPosition destination)
    {
        const CellRecord* source_record = store_.try_cell(source.row, source.column);
        if (source_record == nullptr) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::move_cell_style() source cell is not represented");
        }
        if (store_.try_cell(destination.row, destination.column) == nullptr) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::move_cell_style() destination cell is not represented");
        }
        if (source.row == destination.row && source.column == destination.column) {
            return;
        }

        move_cell_styles(CellRange {source.row, source.column, source.row, source.column},
            destination);
    }

    void move_cell_styles(const CellRange& source, CellPosition destination)
    {
        constexpr std::string_view operation =
            "MaterializedWorksheetSession::move_cell_styles()";
        validate_style_move(source, destination, operation);
        if (destination.row == source.first_row
            && destination.column == source.first_column) {
            return;
        }

        struct StyleMoveSnapshot {
            CellPosition source;
            CellPosition destination;
            std::optional<StyleId> style_id;
        };
        std::vector<StyleMoveSnapshot> style_moves;
        for (const auto& [position, record] : store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition destination_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            if (store_.try_cell(
                    destination_position.row, destination_position.column)
                == nullptr) {
                throw FastXlsxError(std::string(operation)
                    + " destination cell is not represented");
            }
            style_moves.push_back(
                StyleMoveSnapshot {position, destination_position, record.style_id});
        }
        if (style_moves.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_records = store_.records();
        for (const StyleMoveSnapshot& style_move : style_moves) {
            next_records.at(style_move.source).style_id.reset();
        }
        for (const StyleMoveSnapshot& style_move : style_moves) {
            next_records.at(style_move.destination).style_id = style_move.style_id;
        }

        bool changed = false;
        for (const StyleMoveSnapshot& style_move : style_moves) {
            const CellRecord* source_record =
                store_.try_cell(style_move.source.row, style_move.source.column);
            const CellRecord* destination_record = store_.try_cell(
                style_move.destination.row, style_move.destination.column);
            if (!same_style(source_record->style_id,
                    next_records.at(style_move.source).style_id)
                || !same_style(destination_record->style_id,
                    next_records.at(style_move.destination).style_id)) {
                changed = true;
                break;
            }
        }
        if (!changed) {
            return;
        }

        CellStore next_store(store_.options());
        next_store.replace_records(std::move(next_records));
        store_.swap(next_store);
        dirty_ = true;
    }

    void move_cell_styles_from(MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        if (&source_session == this) {
            move_cell_styles(source, destination);
            return;
        }

        constexpr std::string_view operation =
            "MaterializedWorksheetSession::move_cell_styles_from()";
        validate_style_move(source, destination, operation);

        struct StyleMoveSnapshot {
            CellPosition source;
            CellPosition destination;
            std::optional<StyleId> style_id;
        };
        std::vector<StyleMoveSnapshot> style_moves;
        for (const auto& [position, record] : source_session.store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition destination_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            if (store_.try_cell(
                    destination_position.row, destination_position.column)
                == nullptr) {
                throw FastXlsxError(std::string(operation)
                    + " destination cell is not represented");
            }
            style_moves.push_back(
                StyleMoveSnapshot {position, destination_position, record.style_id});
        }
        if (style_moves.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_source_records =
            source_session.store_.records();
        std::map<CellPosition, CellRecord> next_destination_records = store_.records();
        for (const StyleMoveSnapshot& style_move : style_moves) {
            next_source_records.at(style_move.source).style_id.reset();
            next_destination_records.at(style_move.destination).style_id =
                style_move.style_id;
        }

        bool source_changed = false;
        bool destination_changed = false;
        for (const StyleMoveSnapshot& style_move : style_moves) {
            const CellRecord* source_record = source_session.store_.try_cell(
                style_move.source.row, style_move.source.column);
            const CellRecord* destination_record = store_.try_cell(
                style_move.destination.row, style_move.destination.column);
            source_changed = source_changed
                || !same_style(source_record->style_id,
                    next_source_records.at(style_move.source).style_id);
            destination_changed = destination_changed
                || !same_style(destination_record->style_id,
                    next_destination_records.at(style_move.destination).style_id);
        }
        if (!source_changed && !destination_changed) {
            return;
        }

        CellStore next_source_store(source_session.store_.options());
        next_source_store.replace_records(std::move(next_source_records));
        CellStore next_destination_store(store_.options());
        next_destination_store.replace_records(std::move(next_destination_records));

        if (source_changed) {
            source_session.store_.swap(next_source_store);
            source_session.dirty_ = true;
        }
        if (destination_changed) {
            store_.swap(next_destination_store);
            dirty_ = true;
        }
    }

    void clear_cell_style(CellPosition position)
    {
        const CellRecord* record = store_.try_cell(position.row, position.column);
        if (record == nullptr || !record->style_id.has_value()) {
            return;
        }

        store_.set_cell(position.row, position.column, record->to_value().without_style());
        dirty_ = true;
    }

    void clear_cell_styles(const CellRange& range)
    {
        if (range.first_row == 0 || range.first_column == 0
            || range.first_row > range.last_row
            || range.first_column > range.last_column
            || range.last_row > max_excel_rows
            || range.last_column > max_excel_columns) {
            throw FastXlsxError(
                "MaterializedWorksheetSession::clear_cell_styles() requires a valid range");
        }

        struct PlannedStyleUpdate {
            CellPosition position;
            CellValue value;
        };
        std::vector<PlannedStyleUpdate> planned_updates;
        for (const auto& [position, record] : store_.records()) {
            if (position.row < range.first_row || position.row > range.last_row
                || position.column < range.first_column
                || position.column > range.last_column
                || !record.style_id.has_value()) {
                continue;
            }
            planned_updates.push_back(
                PlannedStyleUpdate {position, record.to_value().without_style()});
        }
        if (planned_updates.empty()) {
            return;
        }

        std::vector<CellStoreUpdate> updates;
        updates.reserve(planned_updates.size());
        for (const PlannedStyleUpdate& update : planned_updates) {
            updates.push_back(CellStoreUpdate {update.position, &update.value});
        }
        set_cells(updates);
    }

    [[nodiscard]] const CellRecord* try_cell(
        std::uint32_t row, std::uint32_t column) const
    {
        return store_.try_cell(row, column);
    }

    [[nodiscard]] std::size_t cell_count() const noexcept
    {
        return store_.cell_count();
    }

    [[nodiscard]] std::size_t estimated_memory_usage() const noexcept
    {
        return store_.estimated_memory_usage();
    }

    [[nodiscard]] std::vector<MaterializedCellSnapshot> sparse_cell_snapshots() const
    {
        std::vector<MaterializedCellSnapshot> snapshots;
        snapshots.reserve(store_.cell_count());
        for (const auto& [position, record] : store_.records()) {
            snapshots.push_back(MaterializedCellSnapshot {position, record.to_value()});
        }
        return snapshots;
    }

    [[nodiscard]] std::vector<MaterializedCellSnapshot> sparse_cell_snapshots(
        const CellRange& range) const
    {
        std::vector<MaterializedCellSnapshot> snapshots;
        for (const auto& [position, record] : store_.records()) {
            if (position.row < range.first_row || position.row > range.last_row ||
                position.column < range.first_column || position.column > range.last_column) {
                continue;
            }
            snapshots.push_back(MaterializedCellSnapshot {position, record.to_value()});
        }
        return snapshots;
    }

    [[nodiscard]] const CellStore& store() const noexcept
    {
        return store_;
    }

    [[nodiscard]] CellStore& store() noexcept
    {
        return store_;
    }

    /// Creates a full worksheet chunk source from the current materialized
    /// sparse store.
    ///
    /// The returned callback references this session's CellStore; callers must
    /// keep the session alive and unmodified until the callback is fully
    /// consumed. This is an internal save-as handoff bridge only and does not
    /// imply public WorksheetEditor persistence.
    [[nodiscard]] WorksheetInputChunkCallback worksheet_chunk_source(
        std::shared_ptr<const CellStoreSharedStringIndexProvider>
            shared_string_index_provider = {}) const
    {
        if (shared_string_index_provider) {
            return cell_store_worksheet_chunk_source_with_shared_strings(
                store_, std::move(shared_string_index_provider));
        }
        return cell_store_worksheet_chunk_source(store_);
    }

    /// Creates a standalone `<sheetData>` chunk source from the current
    /// materialized sparse store.
    ///
    /// This is the preferred save-as handoff when the source worksheet wrapper
    /// should be preserved and only sheetData should be replaced. The callback
    /// references this session's CellStore; callers must keep the session alive
    /// and unmodified until the callback is fully consumed.
    [[nodiscard]] WorksheetInputChunkCallback sheet_data_chunk_source(
        std::shared_ptr<const CellStoreSharedStringIndexProvider>
            shared_string_index_provider = {}) const
    {
        if (shared_string_index_provider) {
            return cell_store_sheet_data_chunk_source_with_shared_strings(
                store_, std::move(shared_string_index_provider));
        }
        return cell_store_sheet_data_chunk_source(store_);
    }

    [[nodiscard]] std::string dimension_reference() const
    {
        return cell_store_dimension_reference(store_);
    }

private:
    static constexpr std::uint32_t max_excel_rows = 1048576U;
    static constexpr std::uint32_t max_excel_columns = 16384U;

    static bool same_style(
        const std::optional<StyleId>& left, const std::optional<StyleId>& right) noexcept
    {
        if (left.has_value() != right.has_value()) {
            return false;
        }
        return !left.has_value() || left->value() == right->value();
    }

    static void validate_style_move(
        const CellRange& source, CellPosition destination, std::string_view operation)
    {
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(
                std::string(operation) + " destination range exceeds Excel limits");
        }
    }

    static void validate_value_move(
        const CellRange& source, CellPosition destination, std::string_view operation)
    {
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(
                std::string(operation) + " destination range exceeds Excel limits");
        }
    }

    struct ValueMoveRecord {
        CellPosition source;
        CellPosition destination;
        CellRecord source_blank;
        CellRecord destination_value;
    };

    [[nodiscard]] static std::vector<ValueMoveRecord> plan_value_move(
        const CellStore& source_store, const CellStore& destination_store,
        const CellRange& source, CellPosition destination)
    {
        const FormulaTranslationDelta delta {
            static_cast<std::int64_t>(destination.row)
                - static_cast<std::int64_t>(source.first_row),
            static_cast<std::int64_t>(destination.column)
                - static_cast<std::int64_t>(source.first_column),
        };
        std::vector<ValueMoveRecord> records;
        for (const auto& [position, record] : source_store.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition destination_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            CellRecord source_blank = CellRecord::from_value(CellValue::blank());
            source_blank.style_id = record.style_id;

            CellRecord destination_value = record;
            if (record.kind == CellValueKind::Formula) {
                destination_value.text_value =
                    translate_formula_references(record.text_value, delta);
            }
            destination_value.style_id.reset();
            const CellRecord* destination_record = destination_store.try_cell(
                destination_position.row, destination_position.column);
            if (destination_record != nullptr && destination_record->style_id.has_value()) {
                destination_value.style_id = destination_record->style_id;
            }
            records.push_back(ValueMoveRecord {position, destination_position,
                std::move(source_blank), std::move(destination_value)});
        }
        return records;
    }

    void move_cell_values_within_session(
        const CellRange& source, CellPosition destination)
    {
        constexpr std::string_view operation =
            "MaterializedWorksheetSession::move_cell_values()";
        validate_value_move(source, destination, operation);
        if (destination.row == source.first_row
            && destination.column == source.first_column) {
            return;
        }

        std::vector<ValueMoveRecord> records =
            plan_value_move(store_, store_, source, destination);
        if (records.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_records = store_.records();
        for (ValueMoveRecord& record : records) {
            next_records[record.source] = std::move(record.source_blank);
        }
        for (ValueMoveRecord& record : records) {
            next_records[record.destination] = std::move(record.destination_value);
        }

        CellStore next_store(store_.options());
        next_store.replace_records(std::move(next_records));
        store_.swap(next_store);
        dirty_ = true;
    }

    void move_cell_values_between_sessions(
        MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        constexpr std::string_view operation =
            "MaterializedWorksheetSession::move_cell_values_from()";
        validate_value_move(source, destination, operation);

        std::vector<ValueMoveRecord> records =
            plan_value_move(source_session.store_, store_, source, destination);
        if (records.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_source_records =
            source_session.store_.records();
        std::map<CellPosition, CellRecord> next_destination_records = store_.records();
        for (ValueMoveRecord& record : records) {
            next_source_records[record.source] = std::move(record.source_blank);
            next_destination_records[record.destination] =
                std::move(record.destination_value);
        }

        CellStore next_source_store(source_session.store_.options());
        next_source_store.replace_records(std::move(next_source_records));
        CellStore next_destination_store(store_.options());
        next_destination_store.replace_records(std::move(next_destination_records));

        source_session.store_.swap(next_source_store);
        store_.swap(next_destination_store);
        source_session.dirty_ = true;
        dirty_ = true;
    }

    void move_cells_between_sessions(MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination)
    {
        constexpr std::string_view operation =
            "MaterializedWorksheetSession::move_cells_from()";
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(std::string(operation) + " requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(
                std::string(operation) + " destination range exceeds Excel limits");
        }

        const FormulaTranslationDelta delta {
            static_cast<std::int64_t>(destination.row)
                - static_cast<std::int64_t>(source.first_row),
            static_cast<std::int64_t>(destination.column)
                - static_cast<std::int64_t>(source.first_column),
        };
        struct TransferredRecord {
            CellPosition source;
            CellPosition destination;
            CellRecord record;
        };
        std::vector<TransferredRecord> transferred_records;
        for (const auto& [position, record] : source_session.store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition transferred_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            CellRecord transferred_record = record;
            if (record.kind == CellValueKind::Formula) {
                transferred_record.text_value =
                    translate_formula_references(record.text_value, delta);
            }
            transferred_records.push_back(TransferredRecord {
                position, transferred_position, std::move(transferred_record)});
        }
        if (transferred_records.empty()) {
            return;
        }

        std::map<CellPosition, CellRecord> next_source_records =
            source_session.store_.records();
        std::map<CellPosition, CellRecord> next_destination_records = store_.records();
        for (TransferredRecord& transferred : transferred_records) {
            next_source_records.erase(transferred.source);
            next_destination_records[transferred.destination] =
                std::move(transferred.record);
        }

        CellStore next_source_store(source_session.store_.options());
        next_source_store.replace_records(std::move(next_source_records));
        CellStore next_destination_store(store_.options());
        next_destination_store.replace_records(std::move(next_destination_records));

        source_session.store_.swap(next_source_store);
        store_.swap(next_destination_store);
        source_session.dirty_ = true;
        dirty_ = true;
    }

    void transfer_cells(const MaterializedWorksheetSession& source_session,
        const CellRange& source, CellPosition destination, bool erase_source,
        std::string_view operation)
    {
        const std::string operation_name =
            "MaterializedWorksheetSession::" + std::string(operation) + "()";
        if (source.first_row == 0 || source.first_column == 0
            || source.first_row > source.last_row
            || source.first_column > source.last_column
            || source.last_row > max_excel_rows
            || source.last_column > max_excel_columns) {
            throw FastXlsxError(operation_name + " requires a valid source range");
        }
        if (destination.row == 0 || destination.row > max_excel_rows
            || destination.column == 0 || destination.column > max_excel_columns) {
            throw FastXlsxError(operation_name + " requires a valid destination cell");
        }

        const std::uint32_t row_span = source.last_row - source.first_row;
        const std::uint32_t column_span = source.last_column - source.first_column;
        if (destination.row > max_excel_rows - row_span
            || destination.column > max_excel_columns - column_span) {
            throw FastXlsxError(operation_name + " destination range exceeds Excel limits");
        }
        if (&source_session == this && destination.row == source.first_row
            && destination.column == source.first_column) {
            return;
        }
        if (erase_source && &source_session != this) {
            throw FastXlsxError(operation_name
                + " cannot erase records from a different materialized session");
        }

        const FormulaTranslationDelta delta {
            static_cast<std::int64_t>(destination.row)
                - static_cast<std::int64_t>(source.first_row),
            static_cast<std::int64_t>(destination.column)
                - static_cast<std::int64_t>(source.first_column),
        };
        struct TransferredRecord {
            CellPosition source;
            CellPosition destination;
            CellRecord record;
        };
        std::vector<TransferredRecord> transferred_records;
        for (const auto& [position, record] : source_session.store_.records()) {
            if (position.row < source.first_row || position.row > source.last_row
                || position.column < source.first_column
                || position.column > source.last_column) {
                continue;
            }

            const CellPosition transferred_position {
                destination.row + (position.row - source.first_row),
                destination.column + (position.column - source.first_column),
            };
            CellRecord transferred_record = record;
            if (record.kind == CellValueKind::Formula) {
                transferred_record.text_value =
                    translate_formula_references(record.text_value, delta);
            }
            transferred_records.push_back(TransferredRecord {
                position, transferred_position, std::move(transferred_record)});
        }

        if (transferred_records.empty()) {
            return;
        }
        std::map<CellPosition, CellRecord> next_records = store_.records();
        if (erase_source) {
            for (const TransferredRecord& transferred : transferred_records) {
                next_records.erase(transferred.source);
            }
        }
        for (TransferredRecord& transferred : transferred_records) {
            next_records[transferred.destination] = std::move(transferred.record);
        }

        store_.replace_records(std::move(next_records));
        dirty_ = true;
    }

    static void validate_row_span(
        std::string_view operation, std::uint32_t first_row, std::uint32_t row_count)
    {
        if (first_row == 0 || first_row > max_excel_rows) {
            throw FastXlsxError(std::string(operation)
                + " requires first_row in the Excel row range 1..1048576");
        }
        if (row_count > max_excel_rows - first_row + 1U) {
            throw FastXlsxError(std::string(operation)
                + " row_count exceeds the Excel row range 1..1048576");
        }
    }

    static void validate_column_span(std::string_view operation,
        std::uint32_t first_column, std::uint32_t column_count)
    {
        if (first_column == 0 || first_column > max_excel_columns) {
            throw FastXlsxError(std::string(operation)
                + " requires first_column in the Excel column range 1..16384");
        }
        if (column_count > max_excel_columns - first_column + 1U) {
            throw FastXlsxError(std::string(operation)
                + " column_count exceeds the Excel column range 1..16384");
        }
    }

    static bool insert_shifted_record(std::map<CellPosition, CellRecord>& records,
        CellPosition source_position, CellPosition position, const CellRecord& record,
        FormulaStructuralEdit structural_edit, std::string_view operation)
    {
        CellRecord shifted_record = record;
        if (record.kind == CellValueKind::Formula) {
            shifted_record.text_value = rewrite_formula_references_for_structural_edit(
                record.text_value, structural_edit);
        }

        const bool changed = source_position.row != position.row
            || source_position.column != position.column
            || shifted_record.text_value != record.text_value;
        const auto [_, inserted] = records.emplace(position, std::move(shifted_record));
        if (!inserted) {
            throw FastXlsxError(std::string(operation)
                + " produced duplicate shifted sparse cell coordinates");
        }
        return changed;
    }

    std::string planned_name_;
    CellStore store_;
    bool dirty_ = false;
};

struct MaterializedWorksheetProjection {
    std::string_view planned_name;
    WorksheetInputChunkCallback read_next_chunk;
};

struct MaterializedWorksheetSheetDataProjection {
    std::string_view planned_name;
    WorksheetInputChunkCallback read_next_chunk;
    std::string dimension_reference;
};

/// Internal registry for WorkbookEditor-owned materialized worksheet sessions.
///
/// The registry is a private ownership helper for the future WorksheetEditor
/// path. It keeps repeated materialization for the same planned sheet
/// idempotent when options match, rejects option mismatches before registry
/// mutation, and exposes dirty-session bookkeeping / projection sources for a
/// later save-as bridge. It does not load source worksheets, persist edits,
/// define public handle lifetime, or expose random editing as public API.
class MaterializedWorksheetSessionRegistry {
public:
    [[nodiscard]] bool empty() const noexcept
    {
        return sessions_.empty();
    }

    [[nodiscard]] std::size_t session_count() const noexcept
    {
        return sessions_.size();
    }

    void swap(MaterializedWorksheetSessionRegistry& other) noexcept
    {
        sessions_.swap(other.sessions_);
    }

    friend void swap(
        MaterializedWorksheetSessionRegistry& left,
        MaterializedWorksheetSessionRegistry& right) noexcept
    {
        left.swap(right);
    }

    [[nodiscard]] bool contains(std::string_view planned_name) const
    {
        return try_session(planned_name) != nullptr;
    }

    /// Verifies that materializing `planned_name` with `options` can reuse or
    /// create a session without mutating registry state.
    void preflight_materialization(
        std::string_view planned_name, const CellStoreOptions& options) const
    {
        const MaterializedWorksheetSession* existing = try_session(planned_name);
        if (existing == nullptr || existing->options_match(options)) {
            return;
        }

        throw FastXlsxError(
            "materialized worksheet session options mismatch for planned sheet");
    }

    /// Verifies that another edit operation can proceed before a worksheet has
    /// been materialized.
    ///
    /// Future WorkbookEditor operation-mixing guards can use this to reject
    /// whole-sheet operations that would make an existing materialized session
    /// ambiguous. This helper is mutation-free and does not decide any public
    /// operation policy by itself.
    void preflight_no_materialized_session(
        std::string_view planned_name, std::string_view operation_name) const
    {
        if (!contains(planned_name)) {
            return;
        }

        std::string message = "cannot ";
        message.append(operation_name);
        message += " after materializing planned worksheet session";
        throw FastXlsxError(message);
    }

    /// Inserts a clean session or returns the existing matching session.
    ///
    /// Repeated materialization with matching options preserves the existing
    /// session, including dirty cells. This lets a future WorkbookEditor facade
    /// retry a lookup without losing pending edits.
    MaterializedWorksheetSession& materialize(std::string planned_name, CellStore store)
    {
        preflight_materialization(planned_name, store.options());

        auto existing = sessions_.find(planned_name);
        if (existing != sessions_.end()) {
            return existing->second;
        }

        std::string key = planned_name;
        auto [inserted, _] = sessions_.emplace(
            std::move(key),
            MaterializedWorksheetSession(std::move(planned_name), std::move(store)));
        return inserted->second;
    }

    /// Loads one source workbook sheet into a materialized session.
    ///
    /// The registry first checks whether an existing planned session can be
    /// reused. A matching existing session is returned without re-reading the
    /// package, preserving dirty state. Mismatched options fail before package
    /// access and before registry mutation.
    MaterializedWorksheetSession& materialize_from_workbook_sheet(const PackageReader& reader,
        std::string planned_name, std::string_view source_sheet_name,
        CellStoreOptions options = {}, WorksheetEventReaderOptions reader_options = {})
    {
        preflight_materialization(planned_name, options);
        if (MaterializedWorksheetSession* existing = try_session(planned_name)) {
            return *existing;
        }

        CellStore store = load_cell_store_from_workbook_sheet(
            reader, source_sheet_name, std::move(options), reader_options);
        return materialize(std::move(planned_name), std::move(store));
    }

    [[nodiscard]] MaterializedWorksheetSession* try_session(std::string_view planned_name)
    {
        auto existing = sessions_.find(std::string(planned_name));
        if (existing == sessions_.end()) {
            return nullptr;
        }
        return &existing->second;
    }

    [[nodiscard]] const MaterializedWorksheetSession* try_session(
        std::string_view planned_name) const
    {
        auto existing = sessions_.find(std::string(planned_name));
        if (existing == sessions_.end()) {
            return nullptr;
        }
        return &existing->second;
    }

    [[nodiscard]] const std::map<std::string, MaterializedWorksheetSession>&
    sessions() const noexcept
    {
        return sessions_;
    }

    [[nodiscard]] std::size_t dirty_session_count() const
    {
        std::size_t count = 0;
        for (const auto& [_, session] : sessions_) {
            if (session.dirty()) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t dirty_cell_count() const noexcept
    {
        std::size_t count = 0;
        for (const auto& [_, session] : sessions_) {
            if (session.dirty()) {
                count += session.cell_count();
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t estimated_dirty_memory_usage() const noexcept
    {
        std::size_t total = 0;
        for (const auto& [_, session] : sessions_) {
            if (session.dirty()) {
                total += session.estimated_memory_usage();
            }
        }
        return total;
    }

    [[nodiscard]] std::vector<std::string_view> dirty_session_names() const
    {
        std::vector<std::string_view> names;
        for (const auto& [_, session] : sessions_) {
            if (session.dirty()) {
                names.push_back(session.planned_name());
            }
        }
        return names;
    }

    void clear_dirty_sessions(std::span<const std::string> planned_names) noexcept
    {
        for (const std::string& planned_name : planned_names) {
            auto existing = sessions_.find(planned_name);
            if (existing != sessions_.end()) {
                existing->second.clear_dirty();
            }
        }
    }

    /// Creates full worksheet chunk sources for dirty materialized sessions.
    ///
    /// Returned names and callbacks reference registry-owned sessions. Callers
    /// must keep this registry alive and avoid mutating the corresponding
    /// sessions until the callbacks are fully consumed. This is an internal
    /// projection collection only; it does not queue package edits or save.
    [[nodiscard]] std::vector<MaterializedWorksheetProjection>
    dirty_worksheet_chunk_sources(
        std::shared_ptr<const CellStoreSharedStringIndexProvider>
            shared_string_index_provider = {}) const
    {
        std::vector<MaterializedWorksheetProjection> projections;
        for (const auto& [_, session] : sessions_) {
            if (!session.dirty()) {
                continue;
            }
            projections.push_back(
                MaterializedWorksheetProjection {
                    session.planned_name(),
                    session.worksheet_chunk_source(shared_string_index_provider),
                });
        }
        return projections;
    }

    /// Creates standalone `<sheetData>` chunk sources for dirty materialized
    /// sessions while carrying the sparse-store dimension reference.
    ///
    /// Returned names and callbacks reference registry-owned sessions. Callers
    /// must keep this registry alive and avoid mutating the corresponding
    /// sessions until the callbacks are fully consumed.
    [[nodiscard]] std::vector<MaterializedWorksheetSheetDataProjection>
    dirty_sheet_data_chunk_sources(
        std::shared_ptr<const CellStoreSharedStringIndexProvider>
            shared_string_index_provider = {}) const
    {
        std::vector<MaterializedWorksheetSheetDataProjection> projections;
        for (const auto& [_, session] : sessions_) {
            if (!session.dirty()) {
                continue;
            }
            projections.push_back(
                MaterializedWorksheetSheetDataProjection {
                    session.planned_name(),
                    session.sheet_data_chunk_source(shared_string_index_provider),
                    session.dimension_reference(),
                });
        }
        return projections;
    }

private:
    std::map<std::string, MaterializedWorksheetSession> sessions_;
};

} // namespace fastxlsx::detail
