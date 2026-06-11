#include <fastxlsx/workbook_editor.hpp>

#include "package_editor.hpp"

#include <fastxlsx/detail/cell_store.hpp>

#include <cstdint>
#include <utility>

namespace fastxlsx {

struct WorkbookEditor::Impl {
    explicit Impl(detail::PackageEditor editor)
        : editor(std::move(editor))
    {
    }

    detail::PackageEditor editor;
};

WorkbookEditor::WorkbookEditor() = default;

WorkbookEditor::~WorkbookEditor() = default;

WorkbookEditor::WorkbookEditor(WorkbookEditor&& other) noexcept = default;

WorkbookEditor& WorkbookEditor::operator=(WorkbookEditor&& other) noexcept = default;

WorkbookEditor WorkbookEditor::open(const std::filesystem::path& path)
{
    WorkbookEditor editor;
    editor.impl_ = std::make_unique<Impl>(detail::PackageEditor::open(path));
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

void WorkbookEditor::replace_sheet_data(
    std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows)
{
    detail::CellStore store;
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
}

void WorkbookEditor::save_as(const std::filesystem::path& path) const
{
    impl_->editor.save_as(path);
}

} // namespace fastxlsx
