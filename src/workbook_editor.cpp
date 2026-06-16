#include <fastxlsx/workbook_editor.hpp>

#include "package_editor.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx {

namespace {

detail::CellStoreOptions cell_store_options_from_editor_options(
    const WorkbookEditorOptions& options)
{
    detail::CellStoreOptions store_options;
    store_options.max_cells = options.max_replacement_cells;
    store_options.memory_budget_bytes = options.replacement_memory_budget_bytes;
    return store_options;
}

detail::CellStoreOptions cell_store_options_from_worksheet_options(
    const WorksheetEditorOptions& options)
{
    detail::CellStoreOptions store_options;
    store_options.max_cells = options.max_cells;
    store_options.memory_budget_bytes = options.memory_budget_bytes;
    return store_options;
}

struct PendingSheetDataPayloadDiagnostic {
    std::size_t cell_count = 0;
    std::size_t estimated_memory_usage = 0;
};

void migrate_pending_sheet_data_payload_diagnostic(
    std::map<std::string, PendingSheetDataPayloadDiagnostic>& pending_payloads,
    const std::string& old_name,
    const std::string& new_name)
{
    if (old_name == new_name) {
        return;
    }

    auto pending_payload = pending_payloads.extract(old_name);
    if (pending_payload.empty()) {
        return;
    }

    pending_payload.key() = new_name;
    auto insert_result = pending_payloads.insert(std::move(pending_payload));
    if (!insert_result.inserted && !insert_result.node.empty()) {
        insert_result.position->second = insert_result.node.mapped();
    }
}

[[nodiscard]] std::string missing_planned_sheet_message(std::string_view sheet_name)
{
    std::string message = "WorkbookEditor worksheet is not present in current planned catalog";
    if (!sheet_name.empty()) {
        message += ": ";
        message += sheet_name;
    }
    return message;
}

bool same_existing_path(
    const std::filesystem::path& left, const std::filesystem::path& right) noexcept
{
    std::error_code error;
    const bool same = std::filesystem::equivalent(left, right, error);
    return !error && same;
}

bool path_is_existing_directory(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    return !error && directory;
}

bool path_parent_is_not_directory(const std::filesystem::path& path) noexcept
{
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        return false;
    }

    std::error_code error;
    const bool directory = std::filesystem::is_directory(parent, error);
    return error || !directory;
}

} // namespace

struct WorkbookEditor::Impl {
    Impl(detail::PackageEditor editor, WorkbookEditorOptions options)
        : editor(std::move(editor))
        , options(std::move(options))
    {
    }

    detail::PackageEditor editor;
    WorkbookEditorOptions options;
    detail::MaterializedWorksheetSessionRegistry materialized_sessions;
    std::size_t pending_public_edit_count = 0;
    std::map<std::string, std::string> planned_sheet_names_by_source;
    std::map<std::string, PendingSheetDataPayloadDiagnostic> pending_sheet_data_payloads;
    std::optional<std::string> last_public_edit_error;

    [[nodiscard]] std::vector<std::string> source_worksheet_names() const
    {
        std::vector<std::string> names;
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            names.push_back(sheet.name);
        }
        return names;
    }

    [[nodiscard]] std::vector<std::string> current_worksheet_names() const
    {
        std::vector<std::string> names;
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            const auto planned_name = planned_sheet_names_by_source.find(sheet.name);
            names.push_back(planned_name == planned_sheet_names_by_source.end()
                    ? sheet.name
                    : planned_name->second);
        }
        return names;
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetCatalogEntry> worksheet_catalog() const
    {
        std::vector<WorkbookEditorWorksheetCatalogEntry> entries;
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            const auto planned_name = planned_sheet_names_by_source.find(sheet.name);
            const std::string& current_name =
                planned_name == planned_sheet_names_by_source.end()
                ? sheet.name
                : planned_name->second;

            WorkbookEditorWorksheetCatalogEntry entry;
            entry.source_name = sheet.name;
            entry.planned_name = current_name;
            entry.renamed = current_name != sheet.name;
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] bool has_source_worksheet(std::string_view sheet_name) const
    {
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            if (sheet.name == sheet_name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool has_current_worksheet(std::string_view sheet_name) const
    {
        for (const std::string& name : current_worksheet_names()) {
            if (name == sheet_name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<std::string> source_name_for_current_worksheet(
        std::string_view sheet_name) const
    {
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            const auto planned_name = planned_sheet_names_by_source.find(sheet.name);
            const std::string& current_name =
                planned_name == planned_sheet_names_by_source.end()
                ? sheet.name
                : planned_name->second;
            if (current_name == sheet_name) {
                return sheet.name;
            }
        }
        return std::nullopt;
    }

    void preflight_materialized_flush_targets(
        const std::vector<detail::MaterializedWorksheetProjection>& projections) const
    {
        for (const detail::MaterializedWorksheetProjection& projection : projections) {
            if (!has_current_worksheet(projection.planned_name)) {
                throw FastXlsxError(missing_planned_sheet_message(projection.planned_name));
            }
        }
    }

    void flush_dirty_materialized_sessions_to_patch_plan()
    {
        const std::vector<detail::MaterializedWorksheetProjection> projections =
            materialized_sessions.dirty_worksheet_chunk_sources();
        preflight_materialized_flush_targets(projections);
        for (const detail::MaterializedWorksheetProjection& projection : projections) {
            editor.replace_worksheet_part_from_chunk_source_by_name(
                projection.planned_name, projection.read_next_chunk);

            detail::MaterializedWorksheetSession* session =
                materialized_sessions.try_session(projection.planned_name);
            if (session != nullptr) {
                session->clear_dirty();
            }
            ++pending_public_edit_count;
        }
    }

    void preflight_save_as_path(const std::filesystem::path& path) const
    {
        if (path.empty()) {
            throw FastXlsxError("PackageEditor output path cannot be empty");
        }
        if (path_is_existing_directory(path)) {
            throw FastXlsxError("PackageEditor output path is an existing directory");
        }
        if (path_parent_is_not_directory(path)) {
            throw FastXlsxError("PackageEditor output parent path is not an existing directory");
        }
        if (same_existing_path(editor.reader().path(), path)) {
            throw FastXlsxError("PackageEditor cannot save over the source package");
        }
    }

    [[nodiscard]] bool has_pending_sheet_data_payload(std::string_view sheet_name) const noexcept
    {
        for (const auto& [pending_sheet_name, diagnostic] : pending_sheet_data_payloads) {
            (void)diagnostic;
            if (pending_sheet_name == sheet_name) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const
    {
        std::vector<std::string> names;
        for (const std::string& sheet_name : current_worksheet_names()) {
            if (has_pending_sheet_data_payload(sheet_name)) {
                names.push_back(sheet_name);
            }
        }

        for (const auto& [sheet_name, diagnostic] : pending_sheet_data_payloads) {
            (void)diagnostic;
            if (std::find(names.begin(), names.end(), sheet_name) == names.end()) {
                names.push_back(sheet_name);
            }
        }
        return names;
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetEditSummary> pending_worksheet_edits()
        const
    {
        std::vector<WorkbookEditorWorksheetEditSummary> summaries;

        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            const auto planned_name = planned_sheet_names_by_source.find(sheet.name);
            const std::string& current_name =
                planned_name == planned_sheet_names_by_source.end()
                ? sheet.name
                : planned_name->second;
            const auto pending_payload = pending_sheet_data_payloads.find(current_name);
            const bool renamed = current_name != sheet.name;
            const bool sheet_data_replaced =
                pending_payload != pending_sheet_data_payloads.end();
            if (!renamed && !sheet_data_replaced) {
                continue;
            }

            WorkbookEditorWorksheetEditSummary summary;
            summary.source_name = sheet.name;
            summary.planned_name = current_name;
            summary.renamed = renamed;
            summary.sheet_data_replaced = sheet_data_replaced;
            if (sheet_data_replaced) {
                summary.replacement_cell_count = pending_payload->second.cell_count;
                summary.estimated_replacement_memory_usage =
                    pending_payload->second.estimated_memory_usage;
            }
            summaries.push_back(std::move(summary));
        }

        return summaries;
    }

    void record_planned_sheet_rename(std::string_view old_name, std::string_view new_name)
    {
        for (const detail::WorkbookSheetReference& sheet : editor.reader().workbook_sheets()) {
            const auto planned_name = planned_sheet_names_by_source.find(sheet.name);
            const std::string& current_name =
                planned_name == planned_sheet_names_by_source.end()
                ? sheet.name
                : planned_name->second;
            if (current_name != old_name) {
                continue;
            }

            if (new_name == sheet.name) {
                planned_sheet_names_by_source.erase(sheet.name);
            } else {
                planned_sheet_names_by_source[sheet.name] = std::string(new_name);
            }
            return;
        }

        planned_sheet_names_by_source[std::string(old_name)] = std::string(new_name);
    }

    void clear_last_edit_error()
    {
        last_public_edit_error.reset();
    }

    void record_last_edit_error(const FastXlsxError& error)
    {
        last_public_edit_error = error.what();
    }
};

WorkbookEditor::WorkbookEditor() = default;

WorkbookEditor::~WorkbookEditor() = default;

WorkbookEditor::WorkbookEditor(WorkbookEditor&& other) noexcept = default;

WorkbookEditor& WorkbookEditor::operator=(WorkbookEditor&& other) noexcept = default;

WorkbookEditor WorkbookEditor::open(const std::filesystem::path& path)
{
    return open(path, WorkbookEditorOptions {});
}

WorkbookEditor WorkbookEditor::open(
    const std::filesystem::path& path, WorkbookEditorOptions options)
{
    WorkbookEditor editor;
    editor.impl_ =
        std::make_unique<Impl>(detail::PackageEditor::open(path), std::move(options));
    return editor;
}

std::vector<std::string> WorkbookEditor::worksheet_names() const
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }
    return impl_->current_worksheet_names();
}

bool WorkbookEditor::has_worksheet(std::string_view sheet_name) const
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }
    return impl_->has_current_worksheet(sheet_name);
}

std::vector<std::string> WorkbookEditor::source_worksheet_names() const
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }
    return impl_->source_worksheet_names();
}

bool WorkbookEditor::has_source_worksheet(std::string_view sheet_name) const
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }
    return impl_->has_source_worksheet(sheet_name);
}

bool WorkbookEditor::has_pending_changes() const noexcept
{
    return impl_ != nullptr &&
        (impl_->pending_public_edit_count != 0 ||
            impl_->materialized_sessions.dirty_session_count() != 0);
}

std::size_t WorkbookEditor::pending_change_count() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->pending_public_edit_count;
}

std::size_t WorkbookEditor::pending_replacement_cell_count() const noexcept
{
    if (impl_ == nullptr) {
        return 0;
    }

    std::size_t total = 0;
    for (const auto& [sheet_name, diagnostic] : impl_->pending_sheet_data_payloads) {
        (void)sheet_name;
        total += diagnostic.cell_count;
    }
    return total;
}

std::vector<std::string> WorkbookEditor::pending_replacement_worksheet_names() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_replacement_worksheet_names();
}

bool WorkbookEditor::has_pending_replacement(std::string_view sheet_name) const noexcept
{
    return impl_ != nullptr && impl_->has_pending_sheet_data_payload(sheet_name);
}

std::size_t WorkbookEditor::estimated_pending_replacement_memory_usage() const noexcept
{
    if (impl_ == nullptr) {
        return 0;
    }

    std::size_t total = 0;
    for (const auto& [sheet_name, diagnostic] : impl_->pending_sheet_data_payloads) {
        (void)sheet_name;
        total += diagnostic.estimated_memory_usage;
    }
    return total;
}

std::optional<std::string> WorkbookEditor::last_edit_error() const
{
    if (impl_ == nullptr) {
        return std::nullopt;
    }
    return impl_->last_public_edit_error;
}

std::vector<WorkbookEditorWorksheetEditSummary> WorkbookEditor::pending_worksheet_edits() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_worksheet_edits();
}

std::vector<WorkbookEditorWorksheetCatalogEntry> WorkbookEditor::worksheet_catalog() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->worksheet_catalog();
}

WorksheetEditor WorkbookEditor::worksheet(
    std::string_view sheet_name, WorksheetEditorOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    if (!impl_->has_current_worksheet(sheet_name)) {
        throw FastXlsxError(missing_planned_sheet_message(sheet_name));
    }
    if (impl_->has_pending_sheet_data_payload(sheet_name)) {
        throw FastXlsxError(
            "cannot materialize planned worksheet session after replacing sheet data");
    }

    const std::optional<std::string> source_name =
        impl_->source_name_for_current_worksheet(sheet_name);
    if (!source_name.has_value()) {
        throw FastXlsxError(missing_planned_sheet_message(sheet_name));
    }

    const detail::CellStoreOptions store_options =
        cell_store_options_from_worksheet_options(options);
    impl_->materialized_sessions.materialize_from_workbook_sheet(
        impl_->editor.reader(), std::string(sheet_name), *source_name, store_options);
    return WorksheetEditor(impl_.get(), std::string(sheet_name));
}

void WorkbookEditor::replace_sheet_data(
    std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    try {
        if (!impl_->has_current_worksheet(sheet_name)) {
            throw FastXlsxError(missing_planned_sheet_message(sheet_name));
        }
        impl_->materialized_sessions.preflight_no_materialized_session(
            sheet_name, "replace sheet data");

        detail::CellStore store(cell_store_options_from_editor_options(impl_->options));
        std::uint32_t row_index = 1;
        for (const std::vector<CellValue>& row : rows) {
            std::uint32_t column_index = 1;
            for (const CellValue& value : row) {
                store.set_cell(row_index, column_index, value);
                ++column_index;
            }
            ++row_index;
        }

        // Reuse the landed internal CellStore -> standalone <sheetData> chunk
        // source and the bounded by-name sheetData Patch helper. CellStore::set_cell
        // skips no positions, so an empty row vector still advances the row index,
        // leaving a gap that the emitter renders as a missing row rather than an
        // empty one.
        const detail::WorksheetInputChunkCallback sheet_data_source =
            detail::cell_store_sheet_data_chunk_source(store);
        impl_->editor.replace_worksheet_sheet_data_from_chunk_source_by_name(
            sheet_name, sheet_data_source);
        impl_->pending_sheet_data_payloads[std::string(sheet_name)] = {
            store.cell_count(), store.estimated_memory_usage()};
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        impl_->record_last_edit_error(error);
        throw;
    }
}

void WorkbookEditor::rename_sheet(std::string_view old_name, std::string new_name)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string old_name_key(old_name);
    const std::string new_name_key = new_name;

    try {
        impl_->materialized_sessions.preflight_no_materialized_session(
            old_name_key, "rename sheet");

        // Narrow sheet-catalog name rewrite. Delegates to the internal helper with
        // the default ReferencePolicy; the facade does not expose policy. The helper
        // rewrites only the workbook sheet@name attribute and preserves worksheet
        // parts, relationships, content types, and unknown entries.
        impl_->editor.rename_sheet_catalog_entry(old_name, std::move(new_name));
        impl_->record_planned_sheet_rename(old_name_key, new_name_key);
        migrate_pending_sheet_data_payload_diagnostic(
            impl_->pending_sheet_data_payloads, old_name_key, new_name_key);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error("WorkbookEditor::rename_sheet() failed for '" + old_name_key +
            "' -> '" + new_name_key + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::save_as(const std::filesystem::path& path)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    impl_->preflight_save_as_path(path);
    impl_->flush_dirty_materialized_sessions_to_patch_plan();
    impl_->editor.save_as(path);
}

WorksheetEditor::WorksheetEditor(WorkbookEditor::Impl* impl, std::string planned_name)
    : impl_(impl)
    , planned_name_(std::move(planned_name))
{
}

std::string_view WorksheetEditor::name() const noexcept
{
    return planned_name_;
}

const WorkbookEditor::Impl& WorksheetEditor::impl() const
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorksheetEditor is not attached to an open WorkbookEditor");
    }
    if (impl_->materialized_sessions.try_session(planned_name_) == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return *impl_;
}

WorkbookEditor::Impl& WorksheetEditor::impl()
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorksheetEditor is not attached to an open WorkbookEditor");
    }
    if (impl_->materialized_sessions.try_session(planned_name_) == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return *impl_;
}

std::optional<CellValue> WorksheetEditor::try_cell(
    std::uint32_t row, std::uint32_t column) const
{
    const WorkbookEditor::Impl& state = impl();
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

void WorksheetEditor::set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    WorkbookEditor::Impl& state = impl();
    if (value.has_style() && value.style_id().value() != 0) {
        FastXlsxError error(
            "WorksheetEditor::set_cell() does not support non-default StyleId values");
        state.record_last_edit_error(error);
        throw error;
    }

    try {
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

void WorksheetEditor::erase_cell(std::uint32_t row, std::uint32_t column)
{
    WorkbookEditor::Impl& state = impl();
    try {
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

std::size_t WorksheetEditor::cell_count() const
{
    const WorkbookEditor::Impl& state = impl();
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->cell_count();
}

std::size_t WorksheetEditor::estimated_memory_usage() const
{
    const WorkbookEditor::Impl& state = impl();
    const detail::MaterializedWorksheetSession* session =
        state.materialized_sessions.try_session(planned_name_);
    if (session == nullptr) {
        throw FastXlsxError("WorksheetEditor materialized worksheet session is missing");
    }
    return session->estimated_memory_usage();
}

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
        cell_store_options_from_editor_options(editor.impl_->options));
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
