#include <fastxlsx/detail/cell_store.hpp>

#include <fastxlsx/detail/xml.hpp>

#include <utility>

namespace fastxlsx::detail {

bool operator<(const CellPosition& left, const CellPosition& right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

namespace {

void validate_position(std::uint32_t row, std::uint32_t column)
{
    (void)cell_reference(row, column);
}

std::size_t record_memory_usage(const CellRecord& record) noexcept
{
    return sizeof(CellRecord) + record.text_value.capacity();
}

std::size_t entry_memory_usage(const CellPosition& position, const CellRecord& record) noexcept
{
    return sizeof(position) + record_memory_usage(record) + (sizeof(void*) * 3);
}

} // namespace

CellRecord CellRecord::from_value(const CellValue& value)
{
    CellRecord record;
    record.kind = value.kind();
    record.number_value = value.number_value();
    record.boolean_value = value.boolean_value();
    record.text_value = value.text_value();
    if (value.has_style()) {
        record.style_id = value.style_id();
    }
    return record;
}

CellValue CellRecord::to_value() const
{
    CellValue value = CellValue::blank();
    switch (kind) {
    case CellValueKind::Blank:
        value = CellValue::blank();
        break;
    case CellValueKind::Number:
        value = CellValue::number(number_value);
        break;
    case CellValueKind::Text:
        value = CellValue::text(text_value);
        break;
    case CellValueKind::Boolean:
        value = CellValue::boolean(boolean_value);
        break;
    case CellValueKind::Formula:
        value = CellValue::formula(text_value);
        break;
    }

    if (style_id.has_value()) {
        value = value.with_style(*style_id);
    }
    return value;
}

CellStore::CellStore(CellStoreOptions options)
    : options_(std::move(options))
{
}

void CellStore::set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value)
{
    validate_position(row, column);
    const CellPosition position {row, column};
    CellRecord record = CellRecord::from_value(value);
    const auto existing = cells_.find(position);
    const bool inserting_new_record = existing == cells_.end();
    const std::size_t next_cell_count = cells_.size() + (inserting_new_record ? 1U : 0U);

    if (options_.max_cells.has_value() && next_cell_count > *options_.max_cells) {
        throw FastXlsxError("CellStore max_cells guardrail exceeded");
    }

    if (options_.memory_budget_bytes.has_value()) {
        std::size_t next_memory_usage = estimated_memory_usage();
        if (!inserting_new_record) {
            next_memory_usage -= entry_memory_usage(existing->first, existing->second);
        }
        next_memory_usage += entry_memory_usage(position, record);
        if (next_memory_usage > *options_.memory_budget_bytes) {
            throw FastXlsxError("CellStore memory_budget_bytes guardrail exceeded");
        }
    }

    cells_[position] = std::move(record);
}

void CellStore::erase_cell(std::uint32_t row, std::uint32_t column)
{
    validate_position(row, column);
    cells_.erase(CellPosition {row, column});
}

const CellRecord* CellStore::find_cell(std::uint32_t row, std::uint32_t column) const
{
    validate_position(row, column);
    const auto iterator = cells_.find(CellPosition {row, column});
    if (iterator == cells_.end()) {
        return nullptr;
    }
    return &iterator->second;
}

bool CellStore::empty() const noexcept
{
    return cells_.empty();
}

std::size_t CellStore::cell_count() const noexcept
{
    return cells_.size();
}

std::size_t CellStore::estimated_memory_usage() const noexcept
{
    std::size_t total = sizeof(CellStore);
    for (const auto& [position, record] : cells_) {
        total += entry_memory_usage(position, record);
    }
    return total;
}

const CellStoreOptions& CellStore::options() const noexcept
{
    return options_;
}

const std::map<CellPosition, CellRecord>& CellStore::records() const noexcept
{
    return cells_;
}

} // namespace fastxlsx::detail
