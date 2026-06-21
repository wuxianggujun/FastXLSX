#pragma once

#include <fastxlsx/streaming_writer.hpp>

#include <optional>
#include <string>

namespace fastxlsx {

/// Owning semantic cell value for future in-memory/editor APIs.
///
/// API mode: In-memory / future editor boundary value. CellValue owns text and
/// formula payloads so it can cross API calls safely. It is not the internal
/// long-lived CellStore representation, does not imply random worksheet editing
/// is implemented, and does not make large worksheet paths DOM-based. Formula
/// values are stored as text only; FastXLSX does not parse, evaluate, cache, or
/// rebuild calculation-chain metadata through this type. Error values carry an
/// opaque source token only; FastXLSX does not validate Excel error semantics.
enum class CellValueKind {
    Blank,
    Number,
    Text,
    Boolean,
    Formula,
    Error,
};

/// Public owning value used to describe one cell's semantic payload.
///
/// This first slice models blank, finite number, text, boolean, formula, and
/// opaque error-token values plus an optional workbook-local StyleId handle.
/// Non-default style ids are only handles; a future workbook/editor style
/// registry is responsible for validating whether a non-default id belongs to
/// the target workbook. This type does not migrate shared-string indexes, merge
/// styles, repair relationships, or provide a worksheet cell store.
class CellValue {
public:
    /// Creates an explicit blank / clear candidate value.
    static CellValue blank();

    /// Creates an owning semantic value from the current small-workbook Cell.
    static CellValue from_cell(const Cell& cell);

    /// Converts this value back to a small-workbook Cell when possible.
    ///
    /// Blank values have no `Cell` representation and return `std::nullopt`.
    [[nodiscard]] std::optional<Cell> to_cell() const;

    /// Creates a numeric value.
    ///
    /// @throws FastXlsxError if value is NaN or infinite.
    static CellValue number(double value);

    /// Creates an owning text value.
    static CellValue text(std::string value);

    /// Creates a boolean value.
    static CellValue boolean(bool value);

    /// Creates an owning formula text value.
    static CellValue formula(std::string value);

    /// Creates an owning opaque Excel error token value, such as "#VALUE!".
    ///
    /// The token is stored as text and is not parsed, validated, evaluated, or
    /// mapped to an Excel error enum by FastXLSX.
    static CellValue error(std::string value);

    /// Returns a copy of this value with an explicit workbook-local style id.
    ///
    /// The id is copied as an opaque handle. Non-default ids still need to be
    /// validated by the future target workbook/editor before serialization.
    [[nodiscard]] CellValue with_style(StyleId style_id) const;

    /// Returns a copy of this value without an explicit style reference.
    [[nodiscard]] CellValue without_style() const;

    /// Returns the semantic value kind.
    [[nodiscard]] CellValueKind kind() const noexcept;

    /// Returns the numeric payload when kind() is CellValueKind::Number.
    [[nodiscard]] double number_value() const noexcept;

    /// Returns the owned text, formula, or error-token payload.
    [[nodiscard]] const std::string& text_value() const noexcept;

    /// Returns the boolean payload when kind() is CellValueKind::Boolean.
    [[nodiscard]] bool boolean_value() const noexcept;

    /// Returns whether this value carries an explicit style reference.
    [[nodiscard]] bool has_style() const noexcept;

    /// Returns the style id when has_style() is true, otherwise the default id.
    [[nodiscard]] StyleId style_id() const noexcept;

private:
    CellValueKind kind_ = CellValueKind::Blank;
    double number_value_ = 0.0;
    std::string text_value_;
    bool boolean_value_ = false;
    std::optional<StyleId> style_id_;
};

} // namespace fastxlsx
