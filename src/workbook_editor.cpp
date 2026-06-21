#include <fastxlsx/workbook_editor.hpp>

#include "workbook_editor_save_as_policy.hpp"
#include "workbook_editor_sheet_data_replacement.hpp"
#include "workbook_editor_sheet_rename.hpp"
#include "workbook_editor_image_edit.hpp"
#include "workbook_editor_state.hpp"

#include <fastxlsx/image.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx {

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
        detail::workbook_editor_cell_store_options_from_worksheet_options(options);
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
                detail::workbook_editor_cell_store_options_from_editor_options(impl_->options),
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
        const detail::WorkbookEditorSheetRenameResult result =
            detail::rename_workbook_editor_sheet(
                impl_->editor,
                impl_->sheet_catalog,
                impl_->materialized_sessions,
                impl_->pending_sheet_data_payloads,
                old_name,
                std::move(new_name));
        (void)result;
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

} // namespace fastxlsx
