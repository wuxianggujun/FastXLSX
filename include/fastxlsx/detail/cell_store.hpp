#pragma once

#include <fastxlsx/cell_value.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace fastxlsx::detail {

/// Worksheet-local sparse coordinate for the internal in-memory editor store.
struct CellPosition {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
};

[[nodiscard]] bool operator<(const CellPosition& left, const CellPosition& right) noexcept;

/// Compact internal cell record for future in-memory editor storage.
///
/// This is intentionally separate from public Cell and CellValue objects. It
/// keeps only the value kind, compact scalar payload, optional style handle, and
/// owned text/formula payload needed by the first sparse-store slice.
struct CellRecord {
    CellValueKind kind = CellValueKind::Blank;
    double number_value = 0.0;
    bool boolean_value = false;
    std::string text_value;
    std::optional<StyleId> style_id;

    [[nodiscard]] static CellRecord from_value(const CellValue& value);
    [[nodiscard]] CellValue to_value() const;
};

/// Internal sparse worksheet cell store for future small-file editing.
///
/// API mode: internal P7 foundation. The store records only cells explicitly
/// loaded or edited by a future in-memory editor. It does not allocate a full
/// worksheet matrix, evaluate formulas, migrate shared strings, merge styles,
/// repair relationships, or serialize worksheet XML.
class CellStore {
public:
    void set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value);
    void erase_cell(std::uint32_t row, std::uint32_t column);

    [[nodiscard]] const CellRecord* find_cell(
        std::uint32_t row, std::uint32_t column) const;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t cell_count() const noexcept;
    [[nodiscard]] std::size_t estimated_memory_usage() const noexcept;

    [[nodiscard]] const std::map<CellPosition, CellRecord>& records() const noexcept;

private:
    std::map<CellPosition, CellRecord> cells_;
};

} // namespace fastxlsx::detail
