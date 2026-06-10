#include <fastxlsx/cell_value.hpp>

#include <cmath>
#include <utility>

namespace fastxlsx {

CellValue CellValue::blank()
{
    return {};
}

CellValue CellValue::from_cell(const Cell& cell)
{
    switch (cell.type()) {
    case Cell::Type::Number:
        return CellValue::number(cell.number_value());
    case Cell::Type::String:
        return CellValue::text(cell.string_value());
    case Cell::Type::Boolean:
        return CellValue::boolean(cell.boolean_value());
    case Cell::Type::Formula:
        return CellValue::formula(cell.string_value());
    }

    return {};
}

std::optional<Cell> CellValue::to_cell() const
{
    switch (kind_) {
    case CellValueKind::Blank:
        return std::nullopt;
    case CellValueKind::Number:
        return Cell::number(number_value_);
    case CellValueKind::Text:
        return Cell::text(text_value_);
    case CellValueKind::Boolean:
        return Cell::boolean(boolean_value_);
    case CellValueKind::Formula:
        return Cell::formula(text_value_);
    }

    return std::nullopt;
}

CellValue CellValue::number(double value)
{
    if (!std::isfinite(value)) {
        throw FastXlsxError("cell value numeric payloads must be finite");
    }

    CellValue cell;
    cell.kind_ = CellValueKind::Number;
    cell.number_value_ = value;
    return cell;
}

CellValue CellValue::text(std::string value)
{
    CellValue cell;
    cell.kind_ = CellValueKind::Text;
    cell.text_value_ = std::move(value);
    return cell;
}

CellValue CellValue::boolean(bool value)
{
    CellValue cell;
    cell.kind_ = CellValueKind::Boolean;
    cell.boolean_value_ = value;
    return cell;
}

CellValue CellValue::formula(std::string value)
{
    CellValue cell;
    cell.kind_ = CellValueKind::Formula;
    cell.text_value_ = std::move(value);
    return cell;
}

CellValue CellValue::with_style(StyleId style_id) const
{
    CellValue cell = *this;
    cell.style_id_ = style_id;
    return cell;
}

CellValue CellValue::without_style() const
{
    CellValue cell = *this;
    cell.style_id_.reset();
    return cell;
}

CellValueKind CellValue::kind() const noexcept
{
    return kind_;
}

double CellValue::number_value() const noexcept
{
    return number_value_;
}

const std::string& CellValue::text_value() const noexcept
{
    return text_value_;
}

bool CellValue::boolean_value() const noexcept
{
    return boolean_value_;
}

bool CellValue::has_style() const noexcept
{
    return style_id_.has_value();
}

StyleId CellValue::style_id() const noexcept
{
    return style_id_.value_or(StyleId {});
}

} // namespace fastxlsx
