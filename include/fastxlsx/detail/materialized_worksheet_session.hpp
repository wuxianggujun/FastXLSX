#pragma once

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
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
        return store_.options().max_cells == options.max_cells &&
            store_.options().memory_budget_bytes == options.memory_budget_bytes;
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

    void clear_cell_values(const CellRange& range)
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

        for (const CellPosition& position : positions) {
            clear_cell_value(position.row, position.column);
        }
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

private:
    std::string planned_name_;
    CellStore store_;
    bool dirty_ = false;
};

struct MaterializedWorksheetProjection {
    std::string_view planned_name;
    WorksheetInputChunkCallback read_next_chunk;
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

private:
    std::map<std::string, MaterializedWorksheetSession> sessions_;
};

} // namespace fastxlsx::detail
