#include <fastxlsx/workbook_editor.hpp>

#include "workbook_editor_save_as_policy.hpp"
#include "workbook_editor_sheet_data_replacement.hpp"
#include "workbook_editor_sheet_rename.hpp"
#include "workbook_editor_image_edit.hpp"
#include "workbook_editor_state.hpp"

#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/worksheet_transformer.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/image.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fastxlsx {

namespace {

struct WorkbookEditorTargetedCellPatchMaterializedCell {
    std::string cell_reference;
    std::string replacement_xml;
};

struct WorkbookEditorTargetedCellPatchInput {
    std::size_t input_cell_count = 0;
    std::size_t unique_cell_count = 0;
    std::size_t replacement_xml_bytes = 0;
    std::vector<WorkbookEditorTargetedCellPatchMaterializedCell> materialized_cells;
    std::vector<std::pair<std::string, std::size_t>> public_diagnostics;
};

void validate_workbook_editor_targeted_cell_patch_target(
    const detail::WorkbookEditorSheetCatalogPlan& sheet_catalog,
    const detail::MaterializedWorksheetSessionRegistry& materialized_sessions,
    bool has_pending_sheet_data_payload,
    std::string_view sheet_name,
    std::string_view operation_name)
{
    if (!sheet_catalog.has_current(sheet_name)) {
        throw FastXlsxError(detail::workbook_editor_missing_planned_sheet_message(sheet_name));
    }
    if (has_pending_sheet_data_payload) {
        throw FastXlsxError(
            "cannot " + std::string(operation_name)
            + " after replacing sheet data for the same worksheet");
    }
    materialized_sessions.preflight_no_materialized_session(sheet_name, operation_name);
}

WorkbookEditorTargetedCellPatchInput materialize_workbook_editor_targeted_cell_patch_input(
    std::span<const WorksheetCellUpdate> cells)
{
    std::map<detail::CellPosition, const CellValue*> final_updates;
    for (const WorksheetCellUpdate& cell : cells) {
        const std::string cell_reference =
            detail::cell_reference(cell.reference.row, cell.reference.column);
        (void)cell_reference;
        final_updates[detail::CellPosition {cell.reference.row, cell.reference.column}] =
            &cell.value;
    }

    WorkbookEditorTargetedCellPatchInput input;
    input.input_cell_count = cells.size();
    input.unique_cell_count = final_updates.size();
    input.materialized_cells.reserve(final_updates.size());
    input.public_diagnostics.reserve(final_updates.size());

    for (const auto& [position, value] : final_updates) {
        detail::CellRecord record = detail::CellRecord::from_value(*value);
        std::string cell_reference = detail::cell_reference(position.row, position.column);
        std::string replacement_xml = detail::cell_record_xml(position, record);
        input.replacement_xml_bytes += replacement_xml.size();
        input.public_diagnostics.push_back({cell_reference, replacement_xml.size()});
        input.materialized_cells.push_back(
            WorkbookEditorTargetedCellPatchMaterializedCell {
                std::move(cell_reference),
                std::move(replacement_xml),
            });
    }
    return input;
}

std::vector<detail::WorksheetCellReplacement>
workbook_editor_targeted_cell_replacements_from_materialized_cells(
    const std::vector<WorkbookEditorTargetedCellPatchMaterializedCell>& materialized_cells)
{
    std::vector<detail::WorksheetCellReplacement> replacements;
    replacements.reserve(materialized_cells.size());
    for (const WorkbookEditorTargetedCellPatchMaterializedCell& cell :
         materialized_cells) {
        replacements.push_back(detail::WorksheetCellReplacement {
            cell.cell_reference,
            detail::WorksheetCellReplacementPayload::from_materialized_xml(
                cell.replacement_xml),
        });
    }
    return replacements;
}

[[nodiscard]] std::string_view workbook_editor_targeted_cell_patch_operation_name(
    CellPatchMissingCellPolicy missing_cell_policy)
{
    switch (missing_cell_policy) {
    case CellPatchMissingCellPolicy::Fail:
        return "replace cells";
    case CellPatchMissingCellPolicy::Insert:
        return "replace or insert cells";
    }
    throw FastXlsxError("unknown CellPatchMissingCellPolicy");
}

[[nodiscard]] bool workbook_editor_targeted_cell_patch_inserts_missing_cells(
    CellPatchMissingCellPolicy missing_cell_policy)
{
    switch (missing_cell_policy) {
    case CellPatchMissingCellPolicy::Fail:
        return false;
    case CellPatchMissingCellPolicy::Insert:
        return true;
    }
    throw FastXlsxError("unknown CellPatchMissingCellPolicy");
}

} // namespace

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

std::size_t WorkbookEditor::pending_targeted_cell_replacement_count() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->pending_targeted_cell_replacement_count();
}

std::vector<std::string> WorkbookEditor::pending_replacement_worksheet_names() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_replacement_worksheet_names();
}

std::vector<std::string> WorkbookEditor::pending_targeted_cell_replacement_worksheet_names()
    const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->pending_targeted_cell_replacement_worksheet_names();
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

bool WorkbookEditor::has_pending_targeted_cell_replacement(
    std::string_view sheet_name) const noexcept
{
    return impl_ != nullptr && impl_->has_pending_targeted_cell_replacement(sheet_name);
}

std::size_t WorkbookEditor::estimated_pending_replacement_memory_usage() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->pending_sheet_data_payloads.estimated_memory_usage();
}

std::size_t WorkbookEditor::estimated_pending_targeted_cell_replacement_xml_bytes()
    const noexcept
{
    return impl_ == nullptr ? 0
                            : impl_->estimated_pending_targeted_cell_replacement_xml_bytes();
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

std::vector<WorkbookEditorFormulaReferenceAudit>
WorkbookEditor::source_formula_reference_audits() const
{
    if (impl_ == nullptr) {
        return {};
    }
    return impl_->source_formula_reference_audits();
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
    if (impl_->has_pending_targeted_cell_replacement(sheet_name)) {
        throw FastXlsxError(
            "cannot materialize planned worksheet session after replacing cells");
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
        if (impl_->has_pending_targeted_cell_replacement(sheet_name)) {
            throw FastXlsxError(
                "cannot replace sheet data after replacing cells for the same worksheet");
        }
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

void WorkbookEditor::replace_cells(
    std::string_view sheet_name, std::span<const WorksheetCellUpdate> cells)
{
    replace_cells_impl(sheet_name, cells, CellPatchMissingCellPolicy::Fail);
}

void WorkbookEditor::replace_cells(std::string_view sheet_name,
    std::span<const WorksheetCellUpdate> cells,
    CellPatchMissingCellPolicy missing_cell_policy)
{
    replace_cells_impl(sheet_name, cells, missing_cell_policy);
}

void WorkbookEditor::replace_cells(
    std::string_view sheet_name, std::initializer_list<WorksheetCellUpdate> cells)
{
    replace_cells(sheet_name, std::span<const WorksheetCellUpdate>(cells.begin(), cells.size()));
}

void WorkbookEditor::replace_cells(std::string_view sheet_name,
    std::initializer_list<WorksheetCellUpdate> cells,
    CellPatchMissingCellPolicy missing_cell_policy)
{
    replace_cells(
        sheet_name, std::span<const WorksheetCellUpdate>(cells.begin(), cells.size()),
        missing_cell_policy);
}

void WorkbookEditor::replace_cells_impl(std::string_view sheet_name,
    std::span<const WorksheetCellUpdate> cells,
    CellPatchMissingCellPolicy missing_cell_policy)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    WorkbookEditorTargetedCellPatchInput input;
    try {
        const std::string_view operation_name =
            workbook_editor_targeted_cell_patch_operation_name(missing_cell_policy);
        const bool insert_missing_cells =
            workbook_editor_targeted_cell_patch_inserts_missing_cells(missing_cell_policy);
        validate_workbook_editor_targeted_cell_patch_target(impl_->sheet_catalog,
            impl_->materialized_sessions,
            impl_->has_pending_sheet_data_payload(sheet_name),
            sheet_name, operation_name);
        input = materialize_workbook_editor_targeted_cell_patch_input(cells);
        if (input.materialized_cells.empty()) {
            impl_->clear_last_edit_error();
            return;
        }

        const std::vector<detail::WorksheetCellReplacement> replacements =
            workbook_editor_targeted_cell_replacements_from_materialized_cells(
                input.materialized_cells);
        if (insert_missing_cells) {
            impl_->editor.replace_or_insert_worksheet_cells_by_name(sheet_name, replacements);
        } else {
            impl_->editor.replace_worksheet_cells_by_name(sheet_name, replacements);
        }
        impl_->record_pending_targeted_cell_replacements(
            sheet_name, input.public_diagnostics);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(std::string("WorkbookEditor::replace_cells() failed for '")
            + sheet_name_key + "' with " + std::to_string(cells.size())
            + " input cells and " + std::to_string(input.unique_cell_count)
            + " unique targets: " + error.what());
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
    rename_sheet(old_name, std::move(new_name), WorkbookEditorRenameOptions {});
}

void WorkbookEditor::rename_sheet(
    std::string_view old_name,
    std::string new_name,
    WorkbookEditorRenameOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string old_name_key(old_name);
    const std::string new_name_key = new_name;

    try {
        detail::WorkbookEditorSheetRenameOptions rename_options;
        if (options.formula_policy == WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames) {
            rename_options.formula_policy =
                detail::WorkbookEditorSheetRenameFormulaPolicy::RewriteDefinedNames;
        } else if (
            options.formula_policy
            == WorkbookEditorRenameFormulaPolicy::
                RewriteDefinedNamesAndMaterializedWorksheetFormulas) {
            rename_options.formula_policy =
                detail::WorkbookEditorSheetRenameFormulaPolicy::
                    RewriteDefinedNamesAndMaterializedWorksheetFormulas;
        }
        const detail::WorkbookEditorSheetRenameResult result =
            detail::rename_workbook_editor_sheet(
                impl_->editor,
                impl_->sheet_catalog,
                impl_->materialized_sessions,
                impl_->pending_sheet_data_payloads,
                old_name,
                std::move(new_name),
                rename_options);
        (void)result;
        impl_->move_pending_targeted_cell_replacements(old_name_key, new_name_key);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error("WorkbookEditor::rename_sheet() failed for '" + old_name_key +
            "' -> '" + new_name_key + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::request_full_calculation()
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    try {
        impl_->editor.request_full_calculation();
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            std::string("WorkbookEditor::request_full_calculation() failed: ")
            + error.what());
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
