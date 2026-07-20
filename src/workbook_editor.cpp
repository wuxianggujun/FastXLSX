#include <fastxlsx/workbook_editor.hpp>

#include "workbook_editor_save_as_policy.hpp"
#include "workbook_editor_sheet_data_replacement.hpp"
#include "workbook_editor_sheet_rename.hpp"
#include "workbook_editor_testing_hooks.hpp"
#include "workbook_editor_image_edit.hpp"
#include "cell_store_materialization_loss.hpp"
#include "package_writer.hpp"
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
#include <type_traits>
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

detail::PackageWriterOptions package_writer_options_for_workbook_editor_save(
    WorkbookEditorSaveOptions options)
{
    if (options.zip_compression_level < detail::package_writer_default_compression_level
        || options.zip_compression_level > detail::package_writer_max_compression_level) {
        throw FastXlsxError("ZIP compression level must be -1 or between 0 and 9");
    }

#ifndef FASTXLSX_HAS_MINIZIP_NG
    if (options.zip_compression_level > detail::package_writer_min_compression_level) {
        throw FastXlsxError(
            "ZIP compression levels 1..9 require the minizip-ng backend");
    }
#endif

    detail::PackageWriterOptions package_options;
    package_options.compression_level = options.zip_compression_level;
    if (options.zip_compression_level == detail::package_writer_min_compression_level) {
        package_options.backend = detail::PackageWriterBackend::StoredZipBootstrap;
    } else if (options.zip_compression_level
        == detail::package_writer_default_compression_level) {
        package_options.backend = detail::PackageWriterBackend::Auto;
    } else {
        package_options.backend = detail::PackageWriterBackend::MinizipNg;
    }
    return package_options;
}

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

[[nodiscard]] WorksheetMaterializationLossCategory public_materialization_loss_category(
    detail::CellStoreMaterializationLossCategory category)
{
    switch (category) {
    case detail::CellStoreMaterializationLossCategory::RichText:
        return WorksheetMaterializationLossCategory::RichText;
    case detail::CellStoreMaterializationLossCategory::PhoneticMetadata:
        return WorksheetMaterializationLossCategory::PhoneticMetadata;
    case detail::CellStoreMaterializationLossCategory::ExtensionMetadata:
        return WorksheetMaterializationLossCategory::ExtensionMetadata;
    case detail::CellStoreMaterializationLossCategory::FormulaMetadata:
        return WorksheetMaterializationLossCategory::FormulaMetadata;
    case detail::CellStoreMaterializationLossCategory::CachedFormulaResult:
        return WorksheetMaterializationLossCategory::CachedFormulaResult;
    }
    throw FastXlsxError("unknown CellStoreMaterializationLossCategory");
}

[[nodiscard]] std::string_view materialization_loss_category_name(
    WorksheetMaterializationLossCategory category)
{
    switch (category) {
    case WorksheetMaterializationLossCategory::RichText:
        return "rich text";
    case WorksheetMaterializationLossCategory::PhoneticMetadata:
        return "phonetic metadata";
    case WorksheetMaterializationLossCategory::ExtensionMetadata:
        return "extension metadata";
    case WorksheetMaterializationLossCategory::FormulaMetadata:
        return "formula metadata";
    case WorksheetMaterializationLossCategory::CachedFormulaResult:
        return "cached formula result";
    }
    throw FastXlsxError("unknown WorksheetMaterializationLossCategory");
}

[[nodiscard]] std::string worksheet_materialization_error_message(
    const WorksheetMaterializationDiagnostic& diagnostic)
{
    std::string message = "WorksheetEditor strict materialization rejected ";
    message += materialization_loss_category_name(diagnostic.category);
    message += " in worksheet '" + diagnostic.worksheet_name + "' at row ";
    message += std::to_string(diagnostic.row);
    message += ", column " + std::to_string(diagnostic.column);
    if (diagnostic.shared_string_index.has_value()) {
        message += ", shared string index ";
        message += std::to_string(*diagnostic.shared_string_index);
    }
    return message;
}

} // namespace

WorksheetMaterializationError::WorksheetMaterializationError(
    WorksheetMaterializationDiagnostic diagnostic)
    : FastXlsxError(worksheet_materialization_error_message(diagnostic))
    , diagnostic_(std::move(diagnostic))
{
}

const WorksheetMaterializationDiagnostic&
WorksheetMaterializationError::diagnostic() const noexcept
{
    return diagnostic_;
}

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

bool WorkbookEditor::has_unsaved_changes() const noexcept
{
    return impl_ != nullptr && impl_->has_unsaved_changes();
}

std::size_t WorkbookEditor::unsaved_change_count() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->unsaved_change_count();
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
        if (impl_->is_added_worksheet(sheet_name)) {
            throw FastXlsxError(
                "newly added worksheet cannot be materialized before save and reopen");
        }
        throw FastXlsxError(detail::workbook_editor_missing_planned_sheet_message(sheet_name));
    }

    const detail::CellStoreOptions store_options =
        detail::workbook_editor_cell_store_options_from_worksheet_options(options);
    try {
        impl_->materialized_sessions.materialize_from_workbook_sheet(
            impl_->editor.reader(), std::string(sheet_name), *source_name, store_options);
    } catch (const detail::CellStoreMaterializationLossError& error) {
        throw WorksheetMaterializationError(WorksheetMaterializationDiagnostic {
            public_materialization_loss_category(error.category()),
            std::string(sheet_name),
            error.row(),
            error.column(),
            error.shared_string_index(),
        });
    }
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

void WorkbookEditor::add_internal_hyperlink(
    std::string_view sheet_name, WorksheetCellReference cell,
    std::string location, HyperlinkOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    const std::string location_key = location;
    try {
        if (!impl_->has_current_worksheet(sheet_name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(sheet_name_key));
        }
        if (location.empty()) {
            throw FastXlsxError("internal hyperlink location cannot be empty");
        }

        auto updated_counts = impl_->pending_internal_hyperlink_counts;
        ++updated_counts[sheet_name_key];
        impl_->editor.add_internal_hyperlink_by_name(
            sheet_name_key, cell.row, cell.column, std::move(location),
            std::move(options.display), std::move(options.tooltip));

        using std::swap;
        swap(impl_->pending_internal_hyperlink_counts, updated_counts);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::add_internal_hyperlink() failed for '"
            + sheet_name_key + "' at row " + std::to_string(cell.row)
            + ", column " + std::to_string(cell.column) + " and location '"
            + location_key + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::add_external_hyperlink(
    std::string_view sheet_name, WorksheetCellReference cell,
    std::string target, HyperlinkOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    const std::string target_key = target;
    try {
        if (!impl_->has_current_worksheet(sheet_name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(sheet_name_key));
        }
        if (target.empty()) {
            throw FastXlsxError("external hyperlink target cannot be empty");
        }

        auto updated_counts = impl_->pending_external_hyperlink_counts;
        ++updated_counts[sheet_name_key];
        impl_->editor.add_external_hyperlink_by_name(
            sheet_name_key, cell.row, cell.column, std::move(target),
            std::move(options.display), std::move(options.tooltip));

        using std::swap;
        swap(impl_->pending_external_hyperlink_counts, updated_counts);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::add_external_hyperlink() failed for '"
            + sheet_name_key + "' at row " + std::to_string(cell.row)
            + ", column " + std::to_string(cell.column) + " and target '"
            + target_key + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::add_data_validation(
    std::string_view sheet_name, CellRange range, DataValidationRule rule)
{
    add_data_validation(
        sheet_name, std::span<const CellRange>(&range, 1), std::move(rule));
}

void WorkbookEditor::add_data_validation(
    std::string_view sheet_name, std::span<const CellRange> ranges,
    DataValidationRule rule)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    const std::size_t range_count = ranges.size();
    try {
        if (!impl_->has_current_worksheet(sheet_name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(sheet_name_key));
        }
        if (ranges.empty()) {
            throw FastXlsxError("data validation range list cannot be empty");
        }

        auto updated_counts = impl_->pending_data_validation_counts;
        ++updated_counts[sheet_name_key];
        impl_->editor.add_data_validation_by_name(
            sheet_name_key, std::vector<CellRange>(ranges.begin(), ranges.end()),
            std::move(rule));

        using std::swap;
        swap(impl_->pending_data_validation_counts, updated_counts);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::add_data_validation() failed for '"
            + sheet_name_key + "' with " + std::to_string(range_count)
            + " ranges: " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::add_data_validation(
    std::string_view sheet_name, std::initializer_list<CellRange> ranges,
    DataValidationRule rule)
{
    add_data_validation(
        sheet_name, std::span<const CellRange>(ranges.begin(), ranges.size()),
        std::move(rule));
}

void WorkbookEditor::set_auto_filter(
    std::string_view sheet_name, CellRange range)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    try {
        if (!impl_->has_current_worksheet(sheet_name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(sheet_name_key));
        }

        auto updated_edits = impl_->pending_auto_filter_edits;
        updated_edits[sheet_name_key] = range;
        (void)impl_->editor.rewrite_auto_filter_by_name(sheet_name_key, range);

        static_assert(std::is_nothrow_swappable_v<
            WorkbookEditor::Impl::PendingAutoFilterEdits>);
        using std::swap;
        swap(impl_->pending_auto_filter_edits, updated_edits);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::set_auto_filter() failed for '" + sheet_name_key
            + "' with range (" + std::to_string(range.first_row) + ", "
            + std::to_string(range.first_column) + ")-("
            + std::to_string(range.last_row) + ", "
            + std::to_string(range.last_column) + "): " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::clear_auto_filter(std::string_view sheet_name)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string sheet_name_key(sheet_name);
    try {
        if (!impl_->has_current_worksheet(sheet_name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(sheet_name_key));
        }

        auto updated_edits = impl_->pending_auto_filter_edits;
        updated_edits[sheet_name_key] = std::nullopt;
        const bool changed =
            impl_->editor.rewrite_auto_filter_by_name(sheet_name_key, std::nullopt);
        if (changed) {
            static_assert(std::is_nothrow_swappable_v<
                WorkbookEditor::Impl::PendingAutoFilterEdits>);
            using std::swap;
            swap(impl_->pending_auto_filter_edits, updated_edits);
            ++impl_->pending_public_edit_count;
        }
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::clear_auto_filter() failed for '" + sheet_name_key
            + "': " + error.what());
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

void WorkbookEditor::add_worksheet(std::string name)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string name_key = name;
    try {
        detail::WorkbookEditorSheetCatalogPlan updated_catalog = impl_->sheet_catalog;
        updated_catalog.record_add(name);
        impl_->editor.add_empty_worksheet(std::move(name));

        static_assert(
            std::is_nothrow_swappable_v<detail::WorkbookEditorSheetCatalogPlan>);
        using std::swap;
        swap(impl_->sheet_catalog, updated_catalog);

        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::add_worksheet() failed for '" + name_key + "': " + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::remove_worksheet(std::string_view name)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    const std::string name_key(name);
    try {
        if (!impl_->sheet_catalog.has_current(name_key)) {
            throw FastXlsxError(
                detail::workbook_editor_missing_planned_sheet_message(name_key));
        }
        if (!impl_->pending_replacement_worksheet_names().empty()
            || !impl_->pending_targeted_cell_replacement_worksheet_names().empty()
            || !impl_->pending_internal_hyperlink_counts.empty()
            || !impl_->pending_external_hyperlink_counts.empty()
            || !impl_->pending_data_validation_counts.empty()
            || !impl_->pending_auto_filter_edits.empty()) {
            throw FastXlsxError(
                "worksheet removal requires no queued worksheet payload edits");
        }
        impl_->materialized_sessions.preflight_no_materialized_session(
            name_key, "remove worksheet");

        const auto references_target = [&](const auto& audit) {
            return audit.formula_sheet_planned_name != name_key
                && ((audit.matched_current_workbook_sheet
                        && audit.matched_planned_sheet_name == name_key)
                    || audit.sheet_range_qualifier);
        };
        for (const WorkbookEditorFormulaReferenceAudit& audit :
             impl_->source_formula_reference_audits()) {
            if (references_target(audit)) {
                throw FastXlsxError(
                    "worksheet removal found a source worksheet formula dependency");
            }
        }
        for (const WorkbookEditorFormulaReferenceAudit& audit :
             impl_->formula_reference_audits()) {
            if (references_target(audit)) {
                throw FastXlsxError(
                    "worksheet removal found a materialized worksheet formula dependency");
            }
        }

        detail::WorkbookEditorSheetCatalogPlan updated_catalog = impl_->sheet_catalog;
        updated_catalog.record_remove(name_key);
        impl_->editor.remove_worksheet_catalog_entry(name_key);

        static_assert(
            std::is_nothrow_swappable_v<detail::WorkbookEditorSheetCatalogPlan>);
        using std::swap;
        swap(impl_->sheet_catalog, updated_catalog);

        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            "WorkbookEditor::remove_worksheet() failed for '" + name_key + "': "
            + error.what());
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
        std::optional<WorkbookEditor::Impl::PendingTargetedCellReplacements>
            updated_targeted_cell_replacements =
                impl_->stage_pending_targeted_cell_replacements_move(
                    old_name_key, new_name_key);
        std::optional<std::map<std::string, std::size_t, std::less<>>>
            updated_internal_hyperlink_counts =
                impl_->stage_pending_internal_hyperlink_counts_move(
                    old_name_key, new_name_key);
        std::optional<std::map<std::string, std::size_t, std::less<>>>
            updated_external_hyperlink_counts =
                impl_->stage_pending_external_hyperlink_counts_move(
                    old_name_key, new_name_key);
        std::optional<std::map<std::string, std::size_t, std::less<>>>
            updated_data_validation_counts =
                impl_->stage_pending_data_validation_counts_move(
                    old_name_key, new_name_key);
        std::optional<WorkbookEditor::Impl::PendingAutoFilterEdits>
            updated_auto_filter_edits =
                impl_->stage_pending_auto_filter_edits_move(
                    old_name_key, new_name_key);
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
        impl_->commit_pending_targeted_cell_replacements_move(
            updated_targeted_cell_replacements);
        impl_->commit_pending_internal_hyperlink_counts_move(
            updated_internal_hyperlink_counts);
        impl_->commit_pending_external_hyperlink_counts_move(
            updated_external_hyperlink_counts);
        impl_->commit_pending_data_validation_counts_move(
            updated_data_validation_counts);
        impl_->commit_pending_auto_filter_edits_move(updated_auto_filter_edits);
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

void WorkbookEditor::set_document_properties(DocumentProperties properties)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    try {
        impl_->editor.set_document_properties(properties);
        ++impl_->pending_public_edit_count;
        impl_->clear_last_edit_error();
    } catch (const FastXlsxError& error) {
        FastXlsxError public_error(
            std::string("WorkbookEditor::set_document_properties() failed: ")
            + error.what());
        impl_->record_last_edit_error(public_error);
        throw public_error;
    }
}

void WorkbookEditor::save_as(const std::filesystem::path& path)
{
    WorkbookEditorSaveOptions options;
    options.zip_compression_level = detail::package_writer_min_compression_level;
    save_as(path, options);
}

void WorkbookEditor::save_as(
    const std::filesystem::path& path, WorkbookEditorSaveOptions options)
{
    if (impl_ == nullptr) {
        throw FastXlsxError("WorkbookEditor is not open");
    }

    detail::PackageWriterOptions package_options =
        package_writer_options_for_workbook_editor_save(options);
    package_options.telemetry = impl_->package_writer_telemetry;
    detail::validate_workbook_editor_save_as_path(impl_->editor.reader().path(), path);
    const detail::WorkbookEditorMaterializedStageResult materialized_stage =
        impl_->stage_dirty_materialized_sessions_to_patch_plan();
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    detail::run_testing_workbook_editor_save_as_staged_hook();
#endif
    impl_->editor.save_as(path, package_options);
    impl_->commit_materialized_stage(materialized_stage);
    impl_->mark_saved();
}

} // namespace fastxlsx
