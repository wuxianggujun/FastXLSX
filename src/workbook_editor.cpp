#include <fastxlsx/workbook_editor.hpp>

#include "package_editor.hpp"

#include <fastxlsx/detail/cell_store.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

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

struct PendingSheetDataPayloadDiagnostic {
    std::size_t cell_count = 0;
    std::size_t estimated_memory_usage = 0;
};

} // namespace

struct WorkbookEditor::Impl {
    Impl(detail::PackageEditor editor, WorkbookEditorOptions options)
        : editor(std::move(editor))
        , options(std::move(options))
    {
    }

    detail::PackageEditor editor;
    WorkbookEditorOptions options;
    std::size_t pending_public_edit_count = 0;
    std::map<std::string, PendingSheetDataPayloadDiagnostic> pending_sheet_data_payloads;
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
    std::vector<std::string> names;
    for (const detail::WorkbookSheetReference& sheet : impl_->editor.reader().workbook_sheets()) {
        names.push_back(sheet.name);
    }
    return names;
}

bool WorkbookEditor::has_worksheet(std::string_view sheet_name) const
{
    for (const detail::WorkbookSheetReference& sheet : impl_->editor.reader().workbook_sheets()) {
        if (sheet.name == sheet_name) {
            return true;
        }
    }
    return false;
}

bool WorkbookEditor::has_pending_changes() const noexcept
{
    return impl_ != nullptr && impl_->pending_public_edit_count != 0;
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

void WorkbookEditor::replace_sheet_data(
    std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows)
{
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

    // Reuse the landed internal CellStore -> standalone <sheetData> emitter and
    // the bounded by-name sheetData Patch helper. CellStore::set_cell skips no
    // positions, so an empty row vector still advances the row index, leaving a
    // gap that the emitter renders as a missing row rather than an empty one.

    impl_->editor.replace_worksheet_sheet_data_by_name(
        sheet_name, detail::cell_store_to_sheet_data_xml(store));
    impl_->pending_sheet_data_payloads[std::string(sheet_name)] = {
        store.cell_count(), store.estimated_memory_usage()};
    ++impl_->pending_public_edit_count;
}

void WorkbookEditor::rename_sheet(std::string_view old_name, std::string new_name)
{
    // Narrow sheet-catalog name rewrite. Delegates to the internal helper with
    // the default ReferencePolicy; the facade does not expose policy. The helper
    // rewrites only the workbook sheet@name attribute and preserves worksheet
    // parts, relationships, content types, and unknown entries.
    impl_->editor.rename_sheet_catalog_entry(old_name, std::move(new_name));
    ++impl_->pending_public_edit_count;
}

void WorkbookEditor::save_as(const std::filesystem::path& path) const
{
    impl_->editor.save_as(path);
}

} // namespace fastxlsx
