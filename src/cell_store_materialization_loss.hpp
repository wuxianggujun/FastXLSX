#pragma once

#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace fastxlsx::detail {

enum class CellStoreMaterializationLossCategory {
    RichText,
    PhoneticMetadata,
    ExtensionMetadata,
    FormulaMetadata,
    CachedFormulaResult,
};

class CellStoreMaterializationLossError final : public FastXlsxError {
public:
    CellStoreMaterializationLossError(CellStoreMaterializationLossCategory category,
        std::uint32_t row, std::uint32_t column,
        std::optional<std::size_t> shared_string_index = std::nullopt)
        : FastXlsxError("CellStore strict materialization rejected known lossy source semantics")
        , category_(category)
        , row_(row)
        , column_(column)
        , shared_string_index_(shared_string_index)
    {
    }

    [[nodiscard]] CellStoreMaterializationLossCategory category() const noexcept
    {
        return category_;
    }

    [[nodiscard]] std::uint32_t row() const noexcept
    {
        return row_;
    }

    [[nodiscard]] std::uint32_t column() const noexcept
    {
        return column_;
    }

    [[nodiscard]] std::optional<std::size_t> shared_string_index() const noexcept
    {
        return shared_string_index_;
    }

private:
    CellStoreMaterializationLossCategory category_;
    std::uint32_t row_;
    std::uint32_t column_;
    std::optional<std::size_t> shared_string_index_;
};

} // namespace fastxlsx::detail
