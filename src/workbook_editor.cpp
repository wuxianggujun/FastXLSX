#include <fastxlsx/workbook_editor.hpp>

#include "package_editor.hpp"
#include "workbook_editor_image_edit.hpp"
#include "workbook_editor_formula_diagnostics.hpp"
#include "workbook_editor_materialized_edits.hpp"
#include "workbook_editor_pending_edits.hpp"
#include "workbook_editor_save_as_policy.hpp"
#include "workbook_editor_sheet_catalog.hpp"
#include "workbook_editor_sheet_data_replacement.hpp"
#include "workbook_editor_worksheet_access.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/image.hpp>

#include <cstddef>
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

std::vector<std::string> source_sheet_names_from_workbook_sheets(
    const std::vector<detail::WorkbookSheetReference>& sheets)
{
    std::vector<std::string> names;
    names.reserve(sheets.size());
    for (const detail::WorkbookSheetReference& sheet : sheets) {
        names.push_back(sheet.name);
    }
    return names;
}

std::vector<WorkbookEditorWorksheetCatalogEntry> public_catalog_from_detail_catalog(
    const std::vector<detail::WorkbookEditorSheetCatalogEntry>& detail_catalog)
{
    std::vector<WorkbookEditorWorksheetCatalogEntry> public_catalog;
    public_catalog.reserve(detail_catalog.size());
    for (const detail::WorkbookEditorSheetCatalogEntry& entry : detail_catalog) {
        public_catalog.push_back(WorkbookEditorWorksheetCatalogEntry {
            entry.source_name,
            entry.planned_name,
            entry.renamed,
        });
    }
    return public_catalog;
}

} // namespace

struct WorkbookEditor::Impl {
    Impl(detail::PackageEditor editor, WorkbookEditorOptions options)
        : editor(std::move(editor))
        , options(std::move(options))
        , sheet_catalog(source_sheet_names_from_workbook_sheets(
              this->editor.reader().workbook_sheets()))
    {
    }

    detail::PackageEditor editor;
    WorkbookEditorOptions options;
    detail::WorkbookEditorSheetCatalogPlan sheet_catalog;
    detail::MaterializedWorksheetSessionRegistry materialized_sessions;
    std::size_t pending_public_edit_count = 0;
    detail::WorkbookEditorPendingSheetDataPayloads pending_sheet_data_payloads;
    std::optional<std::string> last_public_edit_error;

    [[nodiscard]] std::vector<std::string> source_worksheet_names() const
    {
        return sheet_catalog.source_names();
    }

    [[nodiscard]] std::vector<std::string> current_worksheet_names() const
    {
        return sheet_catalog.current_names();
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetCatalogEntry> worksheet_catalog() const
    {
        return public_catalog_from_detail_catalog(sheet_catalog.entries());
    }

    [[nodiscard]] bool has_source_worksheet(std::string_view sheet_name) const
    {
        return sheet_catalog.has_source(sheet_name);
    }

    [[nodiscard]] bool has_current_worksheet(std::string_view sheet_name) const
    {
        return sheet_catalog.has_current(sheet_name);
    }

    [[nodiscard]] std::optional<std::string> source_name_for_current_worksheet(
        std::string_view sheet_name) const
    {
        return sheet_catalog.source_name_for_current(sheet_name);
    }

    void flush_dirty_materialized_sessions_to_patch_plan()
    {
        const detail::WorkbookEditorMaterializedFlushResult result =
            detail::flush_workbook_editor_dirty_materialized_sessions_to_patch_plan(
                editor, materialized_sessions, sheet_catalog);
        pending_public_edit_count += result.flushed_worksheet_count;
    }

    [[nodiscard]] bool has_pending_sheet_data_payload(std::string_view sheet_name) const noexcept
    {
        return pending_sheet_data_payloads.contains(sheet_name);
    }

    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const
    {
        return pending_sheet_data_payloads.worksheet_names(current_worksheet_names());
    }

    [[nodiscard]] std::vector<std::string> pending_materialized_worksheet_names() const
    {
        return detail::workbook_editor_pending_materialized_worksheet_names(
            sheet_catalog, materialized_sessions);
    }

    [[nodiscard]] std::size_t pending_materialized_cell_count() const noexcept
    {
        return materialized_sessions.dirty_cell_count();
    }

    [[nodiscard]] std::size_t estimated_pending_materialized_memory_usage() const noexcept
    {
        return materialized_sessions.estimated_dirty_memory_usage();
    }

    [[nodiscard]] std::vector<WorkbookEditorWorksheetEditSummary> pending_worksheet_edits()
        const
    {
        std::vector<WorkbookEditorWorksheetEditSummary> summaries;

        for (const detail::WorkbookEditorSheetCatalogEntry& catalog_entry :
             sheet_catalog.entries()) {
            const std::string& current_name = catalog_entry.planned_name;
            const detail::WorkbookEditorPendingSheetDataPayloadDiagnostic* pending_payload =
                pending_sheet_data_payloads.find(current_name);
            const detail::MaterializedWorksheetSession* materialized_session =
                materialized_sessions.try_session(current_name);
            const bool sheet_data_replaced = pending_payload != nullptr;
            const bool materialized_dirty =
                materialized_session != nullptr && materialized_session->dirty();
            if (!catalog_entry.renamed && !sheet_data_replaced && !materialized_dirty) {
                continue;
            }

            WorkbookEditorWorksheetEditSummary summary;
            summary.source_name = catalog_entry.source_name;
            summary.planned_name = current_name;
            summary.renamed = catalog_entry.renamed;
            summary.sheet_data_replaced = sheet_data_replaced;
            summary.materialized_dirty = materialized_dirty;
            if (sheet_data_replaced) {
                summary.replacement_cell_count = pending_payload->cell_count;
                summary.estimated_replacement_memory_usage =
                    pending_payload->estimated_memory_usage;
            }
            if (materialized_dirty) {
                summary.materialized_cell_count = materialized_session->cell_count();
                summary.estimated_materialized_memory_usage =
                    materialized_session->estimated_memory_usage();
            }
            summaries.push_back(std::move(summary));
        }

        return summaries;
    }

    [[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit> formula_reference_audits()
        const
    {
        return detail::workbook_editor_formula_reference_audits(
            sheet_catalog.entries(), materialized_sessions);
    }

    [[nodiscard]] std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
    defined_name_formula_reference_audits() const
    {
        return detail::workbook_editor_defined_name_formula_reference_audits(
            sheet_catalog.entries(), editor.reader());
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

WorkbookEditor::WorkbookEditor(WorkbookEditor&& other) noexcept
    : impl_(std::move(other.impl_))
    , handle_generation_(other.handle_generation_)
{
    ++other.handle_generation_;
}

WorkbookEditor& WorkbookEditor::operator=(WorkbookEditor&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    impl_ = std::move(other.impl_);
    ++handle_generation_;
    ++other.handle_generation_;
    return *this;
}

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
    return impl_ == nullptr ? 0 : impl_->pending_sheet_data_payloads.cell_count();
}

std::vector<std::string> WorkbookEditor::pending_replacement_worksheet_names() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_replacement_worksheet_names();
}

std::vector<std::string> WorkbookEditor::pending_materialized_worksheet_names() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_materialized_worksheet_names();
}

std::size_t WorkbookEditor::pending_materialized_cell_count() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->pending_materialized_cell_count();
}

std::size_t WorkbookEditor::estimated_pending_materialized_memory_usage() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->estimated_pending_materialized_memory_usage();
}

bool WorkbookEditor::has_pending_replacement(std::string_view sheet_name) const noexcept
{
    return impl_ != nullptr && impl_->has_pending_sheet_data_payload(sheet_name);
}

std::size_t WorkbookEditor::estimated_pending_replacement_memory_usage() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->pending_sheet_data_payloads.estimated_memory_usage();
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

std::vector<WorkbookEditorFormulaReferenceAudit> WorkbookEditor::formula_reference_audits()
    const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->formula_reference_audits();
}

std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
WorkbookEditor::defined_name_formula_reference_audits() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->defined_name_formula_reference_audits();
}

WorksheetEditor WorkbookEditor::worksheet(
    std::string_view sheet_name, WorksheetEditorOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    if (!impl_->has_current_worksheet(sheet_name)) {
        throw FastXlsxError(detail::workbook_editor_missing_planned_sheet_message(sheet_name));
    }
    if (impl_->has_pending_sheet_data_payload(sheet_name)) {
        throw FastXlsxError(
            "cannot materialize planned worksheet session after replacing sheet data");
    }

    const std::optional<std::string> source_name =
        impl_->source_name_for_current_worksheet(sheet_name);
    if (!source_name.has_value()) {
        throw FastXlsxError(detail::workbook_editor_missing_planned_sheet_message(sheet_name));
    }

    const detail::CellStoreOptions store_options =
        cell_store_options_from_worksheet_options(options);
    impl_->materialized_sessions.materialize_from_workbook_sheet(
        impl_->editor.reader(), std::string(sheet_name), *source_name, store_options);
    return WorksheetEditor(this, std::string(sheet_name), handle_generation_);
}

std::optional<WorksheetEditor> WorkbookEditor::try_worksheet(
    std::string_view sheet_name, WorksheetEditorOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    if (!impl_->has_current_worksheet(sheet_name)) {
        return std::nullopt;
    }

    return std::optional<WorksheetEditor>(worksheet(sheet_name, std::move(options)));
}

void WorkbookEditor::replace_sheet_data(
    std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    const detail::WorkbookEditorSheetDataReplacementInputDiagnostic input =
        detail::workbook_editor_sheet_data_replacement_input_diagnostic(rows);

    try {
        const detail::WorkbookEditorSheetDataReplacementResult result =
            detail::replace_workbook_editor_sheet_data_from_rows(
                impl_->editor,
                impl_->sheet_catalog,
                impl_->materialized_sessions,
                impl_->pending_sheet_data_payloads,
                sheet_name,
                rows,
                cell_store_options_from_editor_options(impl_->options),
                input);
        (void)result;
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error("WorkbookEditor::replace_sheet_data() failed for '"
            + sheet_name_key + "' with " + std::to_string(input.row_count) + " rows and "
            + std::to_string(input.cell_count) + " cells: " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::replace_image(
    std::string_view image_part_name, std::filesystem::path image_path)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string image_part_name_key(image_part_name);
    const std::filesystem::path image_path_key = image_path;

    try {
        const detail::WorkbookEditorImageTarget target =
            detail::resolve_workbook_editor_image_target(
                impl_->editor.manifest(), image_part_name);
        const ImageInfo replacement_info = read_image_info(image_path);
        detail::validate_workbook_editor_image_replacement_format(
            target.format, replacement_info.format);

        impl_->editor.replace_part_chunks(target.part_name,
            std::vector<detail::PackageEntryChunk> {
                detail::PackageEntryChunk::file(std::move(image_path))},
            "existing-workbook image replacement");
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error("WorkbookEditor::replace_image() failed for '"
            + image_part_name_key + "' from file '" + image_path_key.generic_string()
            + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::replace_image(
    std::string_view image_part_name, std::span<const std::byte> image_bytes)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string image_part_name_key(image_part_name);
    const std::size_t image_byte_count = image_bytes.size();

    try {
        const detail::WorkbookEditorImageTarget target =
            detail::resolve_workbook_editor_image_target(
                impl_->editor.manifest(), image_part_name);
        const ImageInfo replacement_info = read_image_info(image_bytes);
        detail::validate_workbook_editor_image_replacement_format(
            target.format, replacement_info.format);

        std::string copied_bytes;
        copied_bytes.assign(reinterpret_cast<const char*>(image_bytes.data()),
            reinterpret_cast<const char*>(image_bytes.data()) + image_bytes.size());

        impl_->editor.replace_part_chunks(target.part_name,
            std::vector<detail::PackageEntryChunk> {
                detail::PackageEntryChunk::memory(std::move(copied_bytes))},
            "existing-workbook image replacement");
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error("WorkbookEditor::replace_image() failed for '"
            + image_part_name_key + "' from memory bytes ("
            + std::to_string(image_byte_count) + " bytes): " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
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
        impl_->sheet_catalog.record_rename(old_name_key, new_name_key);
        impl_->pending_sheet_data_payloads.migrate(old_name_key, new_name_key);
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

    detail::validate_workbook_editor_save_as_path(impl_->editor.reader().path(), path);
    impl_->flush_dirty_materialized_sessions_to_patch_plan();
    impl_->editor.save_as(path);
}

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
        if (value.has_style() && value.style_id().value() != 0) {
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
