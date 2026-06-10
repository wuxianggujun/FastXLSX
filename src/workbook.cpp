#include <fastxlsx/workbook.hpp>

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/xml.hpp>

#include "package_writer.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace fastxlsx {
namespace {

bool needs_space_preserve(std::string_view value)
{
    return !value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n'
            || value.front() == '\r' || value.back() == ' ' || value.back() == '\t'
            || value.back() == '\n' || value.back() == '\r');
}

void append_text_element(std::string& xml, std::string_view value)
{
    if (needs_space_preserve(value)) {
        xml += "<t xml:space=\"preserve\">";
    } else {
        xml += "<t>";
    }
    xml += detail::escape_xml_text(value);
    xml += "</t>";
}

void validate_sheet_name(std::string_view name)
{
    if (name.empty()) {
        throw FastXlsxError("worksheet name cannot be empty");
    }
    if (name.size() > 31) {
        throw FastXlsxError("worksheet name exceeds Excel's 31-character limit");
    }

    for (const char ch : name) {
        switch (ch) {
        case ':':
        case '\\':
        case '/':
        case '?':
        case '*':
        case '[':
        case ']':
            throw FastXlsxError("worksheet name contains a character rejected by Excel");
        default:
            break;
        }
    }
}

constexpr std::string_view recalculation_calc_id = "124519";

bool rows_have_formula(const std::vector<detail::WorksheetRowData>& rows) noexcept
{
    for (const auto& row_data : rows) {
        for (const Cell& cell : row_data.cells) {
            if (cell.type() == Cell::Type::Formula) {
                return true;
            }
        }
    }
    return false;
}

std::string build_workbook(const std::vector<Worksheet>& worksheets, bool full_calc_on_load)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )";
    xml += R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
    xml += "<sheets>";
    for (std::size_t index = 0; index < worksheets.size(); ++index) {
        xml += R"(<sheet name=")";
        xml += detail::escape_xml_attribute(worksheets[index].name());
        xml += R"(" sheetId=")";
        xml += std::to_string(index + 1);
        xml += R"(" r:id="rId)";
        xml += std::to_string(index + 1);
        xml += R"("/>)";
    }
    xml += "</sheets>";
    if (full_calc_on_load) {
        xml += R"(<calcPr calcId=")";
        xml += recalculation_calc_id;
        xml += R"(" fullCalcOnLoad="1"/>)";
    }
    xml += "</workbook>";
    return xml;
}

std::string worksheet_dimension(const std::vector<detail::WorksheetRowData>& rows)
{
    if (rows.empty()) {
        return "A1";
    }

    std::uint32_t max_column = 1;
    for (const auto& row : rows) {
        max_column = std::max(max_column, static_cast<std::uint32_t>(row.cells.size()));
    }

    const std::uint32_t max_row = static_cast<std::uint32_t>(rows.size());
    std::string dimension = "A1:";
    detail::append_cell_reference(dimension, max_row, max_column);
    return dimension;
}

std::string build_worksheet(const std::vector<detail::WorksheetRowData>& rows)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
    xml += R"(<dimension ref=")";
    xml += worksheet_dimension(rows);
    xml += "\"/>";
    xml += "<sheetData>";

    for (std::uint32_t row_index = 0; row_index < rows.size(); ++row_index) {
        const std::uint32_t row_number = row_index + 1;
        const auto& row_data = rows[row_index];
        const auto& row = row_data.cells;

        xml += "<row r=\"";
        xml += std::to_string(row_number);
        if (row_data.options.height.has_value()) {
            if (!std::isfinite(*row_data.options.height) || *row_data.options.height <= 0.0) {
                throw FastXlsxError("row height must be positive and finite");
            }
            xml += "\" ht=\"";
            detail::append_number(xml, *row_data.options.height);
            xml += "\" customHeight=\"1";
        }
        xml += "\">";

        for (std::uint32_t column_index = 0; column_index < row.size(); ++column_index) {
            const std::uint32_t column_number = column_index + 1;
            const Cell& cell = row[column_index];
            xml += "<c r=\"";
            detail::append_cell_reference(xml, row_number, column_number);

            switch (cell.type()) {
            case Cell::Type::Number:
                xml += "\"><v>";
                detail::append_number(xml, cell.number_value());
                xml += "</v></c>";
                break;
            case Cell::Type::String:
                xml += "\" t=\"inlineStr\"><is>";
                append_text_element(xml, cell.string_value());
                xml += "</is></c>";
                break;
            case Cell::Type::Boolean:
                xml += "\" t=\"b\"><v>";
                xml += cell.boolean_value() ? "1" : "0";
                xml += "</v></c>";
                break;
            case Cell::Type::Formula:
                xml += "\"><f>";
                xml += detail::escape_xml_text(cell.string_value());
                xml += "</f></c>";
                break;
            }
        }

        xml += "</row>";
    }

    xml += "</sheetData></worksheet>";
    return xml;
}

} // namespace

Cell Cell::number(double value)
{
    return Cell(value);
}

Cell Cell::text(std::string value)
{
    return Cell(std::move(value));
}

Cell Cell::boolean(bool value)
{
    return Cell(value);
}

Cell Cell::formula(std::string value)
{
    return Cell(Type::Formula, std::move(value));
}

Cell::Type Cell::type() const noexcept
{
    return type_;
}

double Cell::number_value() const noexcept
{
    return number_value_;
}

const std::string& Cell::string_value() const noexcept
{
    return string_value_;
}

bool Cell::boolean_value() const noexcept
{
    return boolean_value_;
}

Cell::Cell(double value)
    : type_(Type::Number)
    , number_value_(value)
{
}

Cell::Cell(std::string value)
    : type_(Type::String)
    , string_value_(std::move(value))
{
}

Cell::Cell(Type type, std::string value)
    : type_(type)
    , string_value_(std::move(value))
{
}

Cell::Cell(bool value)
    : type_(Type::Boolean)
    , boolean_value_(value)
{
}

Worksheet::Worksheet(std::string name)
    : name_(std::move(name))
{
}

void Worksheet::append_row(std::initializer_list<Cell> cells)
{
    rows_.push_back(detail::WorksheetRowData {std::vector<Cell>(cells), {}});
}

void Worksheet::append_row(const std::vector<Cell>& cells)
{
    rows_.push_back(detail::WorksheetRowData {cells, {}});
}

void Worksheet::append_row(const std::vector<Cell>& cells, RowOptions options)
{
    rows_.push_back(detail::WorksheetRowData {cells, options});
}

const std::string& Worksheet::name() const noexcept
{
    return name_;
}

std::uint32_t Worksheet::row_count() const noexcept
{
    return static_cast<std::uint32_t>(rows_.size());
}

Workbook Workbook::create()
{
    return Workbook {};
}

void Workbook::set_document_properties(DocumentProperties properties)
{
    document_properties_ = std::move(properties);
}

Worksheet& Workbook::add_worksheet(std::string name)
{
    validate_sheet_name(name);

    std::set<std::string> existing_names;
    for (const Worksheet& worksheet : worksheets_) {
        existing_names.insert(worksheet.name());
    }
    if (existing_names.contains(name)) {
        throw FastXlsxError("worksheet names must be unique");
    }

    worksheets_.push_back(Worksheet(std::move(name)));
    return worksheets_.back();
}

void Workbook::save(const std::filesystem::path& path) const
{
    if (worksheets_.empty()) {
        throw FastXlsxError("workbook must contain at least one worksheet");
    }

    const bool full_calc_on_load = std::any_of(worksheets_.begin(), worksheets_.end(),
        [](const Worksheet& worksheet) { return rows_have_formula(worksheet.rows_); });

    std::vector<detail::PackageEntry> entries;
    const detail::PackageManifest manifest =
        detail::make_minimal_workbook_manifest(worksheets_.size());
    const auto* workbook_relationships =
        manifest.relationships_for(detail::PartName("/xl/workbook.xml"));
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook relationships missing from package manifest");
    }

    entries.reserve(6 + worksheets_.size());
    entries.push_back({"[Content_Types].xml", detail::serialize_content_types(manifest.content_types())});
    entries.push_back({"_rels/.rels", detail::serialize_relationships(manifest.package_relationships())});
    entries.push_back({"docProps/core.xml", detail::build_core_properties(document_properties_)});
    entries.push_back({"docProps/app.xml", detail::build_extended_properties(document_properties_)});
    entries.push_back({"xl/workbook.xml", build_workbook(worksheets_, full_calc_on_load)});
    entries.push_back({"xl/_rels/workbook.xml.rels", detail::serialize_relationships(*workbook_relationships)});

    for (std::size_t index = 0; index < worksheets_.size(); ++index) {
        entries.push_back({"xl/worksheets/sheet" + std::to_string(index + 1) + ".xml",
            build_worksheet(worksheets_[index].rows_)});
    }

    detail::write_package(path, entries);
}

const std::vector<Worksheet>& Workbook::worksheets() const noexcept
{
    return worksheets_;
}

} // namespace fastxlsx
