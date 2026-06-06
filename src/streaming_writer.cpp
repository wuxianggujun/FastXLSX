#include <fastxlsx/streaming_writer.hpp>

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/xml.hpp>

#include "zip_store_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace fastxlsx {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576;
constexpr std::uint32_t max_excel_columns = 16384;

struct ColumnWidth {
    std::uint32_t first_column = 1;
    std::uint32_t last_column = 1;
    double width = 0.0;
};

std::string format_number(double value)
{
    std::array<char, 64> buffer {};
    const auto [ptr, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error == std::errc()) {
        return {buffer.data(), ptr};
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}

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

std::filesystem::path make_temp_path()
{
    static std::atomic<std::uint64_t> counter {0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path()
        / ("fastxlsx-stream-" + std::to_string(tick) + "-" + std::to_string(id) + ".xml");
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to open temporary worksheet XML");
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

namespace detail {

struct WorksheetWriterState {
    explicit WorksheetWriterState(std::string sheet_name)
        : name(std::move(sheet_name))
        , body_path(make_temp_path())
        , body(body_path, std::ios::binary)
    {
        if (!body) {
            throw FastXlsxError("failed to create temporary worksheet XML");
        }
    }

    ~WorksheetWriterState()
    {
        if (body.is_open()) {
            body.close();
        }
        std::error_code ignored;
        std::filesystem::remove(body_path, ignored);
    }

    WorksheetWriterState(const WorksheetWriterState&) = delete;
    WorksheetWriterState& operator=(const WorksheetWriterState&) = delete;

    std::string name;
    std::filesystem::path body_path;
    std::ofstream body;
    std::uint32_t row_count = 0;
    std::uint32_t max_column = 0;
    std::vector<ColumnWidth> column_widths;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> frozen_panes;
    std::optional<CellRange> auto_filter;
    std::vector<CellRange> merged_ranges;
};

struct WorkbookWriterState {
    std::filesystem::path output_path;
    WorkbookWriterOptions options;
    std::vector<std::unique_ptr<WorksheetWriterState>> worksheets;
    bool closed = false;
};

} // namespace detail

namespace {

void write_cell(std::string& xml, std::uint32_t row, std::uint32_t column, const CellView& cell)
{
    const std::string reference = detail::cell_reference(row, column);
    switch (cell.type()) {
    case CellView::Type::Number:
        xml += "<c r=\"";
        xml += reference;
        xml += "\"><v>";
        xml += format_number(cell.number_value());
        xml += "</v></c>";
        break;
    case CellView::Type::String:
        xml += "<c r=\"";
        xml += reference;
        xml += "\" t=\"inlineStr\"><is>";
        append_text_element(xml, cell.text_value());
        xml += "</is></c>";
        break;
    case CellView::Type::Boolean:
        xml += "<c r=\"";
        xml += reference;
        xml += "\" t=\"b\"><v>";
        xml += cell.boolean_value() ? "1" : "0";
        xml += "</v></c>";
        break;
    case CellView::Type::Formula:
        xml += "<c r=\"";
        xml += reference;
        xml += "\"><f>";
        xml += detail::escape_xml_text(cell.text_value());
        xml += "</f></c>";
        break;
    }
}

std::string worksheet_dimension(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.row_count == 0 || worksheet.max_column == 0) {
        return "A1";
    }
    return "A1:" + detail::cell_reference(worksheet.row_count, worksheet.max_column);
}

std::string build_workbook(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )";
    xml += R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
    xml += "<sheets>";
    for (std::size_t index = 0; index < worksheets.size(); ++index) {
        xml += R"(<sheet name=")";
        xml += detail::escape_xml_attribute(worksheets[index]->name);
        xml += R"(" sheetId=")";
        xml += std::to_string(index + 1);
        xml += R"(" r:id="rId)";
        xml += std::to_string(index + 1);
        xml += R"("/>)";
    }
    xml += "</sheets></workbook>";
    return xml;
}

std::string build_sheet_views(const detail::WorksheetWriterState& worksheet)
{
    if (!worksheet.frozen_panes.has_value()) {
        return {};
    }

    const auto [row_split, column_split] = *worksheet.frozen_panes;
    std::string xml;
    xml += "<sheetViews><sheetView workbookViewId=\"0\"><pane";
    if (column_split > 0) {
        xml += " xSplit=\"";
        xml += std::to_string(column_split);
        xml += "\"";
    }
    if (row_split > 0) {
        xml += " ySplit=\"";
        xml += std::to_string(row_split);
        xml += "\"";
    }
    xml += " topLeftCell=\"";
    xml += detail::cell_reference(row_split + 1, column_split + 1);
    xml += "\" activePane=\"bottomRight\" state=\"frozen\"/></sheetView></sheetViews>";
    return xml;
}

std::string build_columns(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.column_widths.empty()) {
        return {};
    }

    std::string xml = "<cols>";
    for (const ColumnWidth& width : worksheet.column_widths) {
        xml += "<col min=\"";
        xml += std::to_string(width.first_column);
        xml += "\" max=\"";
        xml += std::to_string(width.last_column);
        xml += "\" width=\"";
        xml += format_number(width.width);
        xml += "\" customWidth=\"1\"/>";
    }
    xml += "</cols>";
    return xml;
}

std::string build_merge_cells(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.merged_ranges.empty()) {
        return {};
    }

    std::string xml = "<mergeCells count=\"";
    xml += std::to_string(worksheet.merged_ranges.size());
    xml += "\">";
    for (const CellRange& range : worksheet.merged_ranges) {
        xml += "<mergeCell ref=\"";
        xml += detail::range_reference(range);
        xml += "\"/>";
    }
    xml += "</mergeCells>";
    return xml;
}

std::string build_worksheet(detail::WorksheetWriterState& worksheet)
{
    if (worksheet.body.is_open()) {
        worksheet.body.close();
    }

    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
    xml += R"(<dimension ref=")";
    xml += worksheet_dimension(worksheet);
    xml += "\"/>";
    xml += build_sheet_views(worksheet);
    xml += build_columns(worksheet);
    xml += "<sheetData>";
    xml += read_file(worksheet.body_path);
    xml += "</sheetData>";
    if (worksheet.auto_filter.has_value()) {
        xml += "<autoFilter ref=\"";
        xml += detail::range_reference(*worksheet.auto_filter);
        xml += "\"/>";
    }
    xml += build_merge_cells(worksheet);
    xml += "</worksheet>";
    return xml;
}

} // namespace

CellView CellView::number(double value) noexcept
{
    CellView cell;
    cell.type_ = Type::Number;
    cell.number_value_ = value;
    return cell;
}

CellView CellView::text(std::string_view value) noexcept
{
    CellView cell;
    cell.type_ = Type::String;
    cell.text_value_ = value;
    return cell;
}

CellView CellView::boolean(bool value) noexcept
{
    CellView cell;
    cell.type_ = Type::Boolean;
    cell.boolean_value_ = value;
    return cell;
}

CellView CellView::formula(std::string_view value) noexcept
{
    CellView cell;
    cell.type_ = Type::Formula;
    cell.text_value_ = value;
    return cell;
}

CellView::Type CellView::type() const noexcept
{
    return type_;
}

double CellView::number_value() const noexcept
{
    return number_value_;
}

std::string_view CellView::text_value() const noexcept
{
    return text_value_;
}

bool CellView::boolean_value() const noexcept
{
    return boolean_value_;
}

WorksheetWriter::WorksheetWriter() noexcept = default;

WorksheetWriter::WorksheetWriter(detail::WorksheetWriterState* state) noexcept
    : state_(state)
{
}

void WorksheetWriter::append_row(std::span<const CellView> cells, RowOptions options)
{
    if (state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (cells.size() > max_excel_columns) {
        throw FastXlsxError("row exceeds Excel's column limit");
    }
    if (state_->row_count >= max_excel_rows) {
        throw FastXlsxError("worksheet exceeds Excel's row limit");
    }
    if (options.height.has_value() && *options.height <= 0.0) {
        throw FastXlsxError("row height must be positive");
    }

    ++state_->row_count;
    state_->max_column = std::max(state_->max_column, static_cast<std::uint32_t>(cells.size()));

    std::string row_xml;
    row_xml += "<row r=\"";
    row_xml += std::to_string(state_->row_count);
    if (options.height.has_value()) {
        row_xml += "\" ht=\"";
        row_xml += format_number(*options.height);
        row_xml += "\" customHeight=\"1";
    }
    row_xml += "\">";
    for (std::uint32_t column = 0; column < cells.size(); ++column) {
        write_cell(row_xml, state_->row_count, column + 1, cells[column]);
    }
    row_xml += "</row>";

    state_->body << row_xml;
    if (!state_->body) {
        throw FastXlsxError("failed to write worksheet row XML");
    }
}

void WorksheetWriter::append_row(std::initializer_list<CellView> cells, RowOptions options)
{
    append_row(std::span<const CellView>(cells.begin(), cells.size()), options);
}

void WorksheetWriter::set_column_width(std::uint32_t first_column, std::uint32_t last_column, double width)
{
    if (state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (first_column == 0 || last_column == 0 || first_column > last_column
        || last_column > max_excel_columns || width <= 0.0) {
        throw FastXlsxError("invalid column width range");
    }
    state_->column_widths.push_back({first_column, last_column, width});
}

void WorksheetWriter::freeze_panes(std::uint32_t row_split, std::uint32_t column_split)
{
    if (state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (row_split > max_excel_rows || column_split > max_excel_columns) {
        throw FastXlsxError("invalid freeze pane split");
    }
    state_->frozen_panes = std::make_pair(row_split, column_split);
}

void WorksheetWriter::set_auto_filter(CellRange range)
{
    if (state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    (void)detail::range_reference(range);
    state_->auto_filter = range;
}

void WorksheetWriter::merge_cells(CellRange range)
{
    if (state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    (void)detail::range_reference(range);
    if (range.first_row == range.last_row && range.first_column == range.last_column) {
        throw FastXlsxError("merged range must include more than one cell");
    }
    state_->merged_ranges.push_back(range);
}

WorkbookWriter::WorkbookWriter() = default;
WorkbookWriter::~WorkbookWriter() = default;
WorkbookWriter::WorkbookWriter(WorkbookWriter&&) noexcept = default;
WorkbookWriter& WorkbookWriter::operator=(WorkbookWriter&&) noexcept = default;

WorkbookWriter::WorkbookWriter(std::unique_ptr<detail::WorkbookWriterState> state) noexcept
    : state_(std::move(state))
{
}

WorkbookWriter WorkbookWriter::create(std::filesystem::path path, WorkbookWriterOptions options)
{
    auto state = std::make_unique<detail::WorkbookWriterState>();
    state->output_path = std::move(path);
    state->options = options;
    return WorkbookWriter(std::move(state));
}

WorksheetWriter WorkbookWriter::add_worksheet(std::string name)
{
    if (!state_) {
        throw FastXlsxError("workbook writer is not initialized");
    }
    if (state_->closed) {
        throw FastXlsxError("cannot add worksheet after close");
    }
    validate_sheet_name(name);

    std::set<std::string> existing_names;
    for (const auto& worksheet : state_->worksheets) {
        existing_names.insert(worksheet->name);
    }
    if (existing_names.contains(name)) {
        throw FastXlsxError("worksheet names must be unique");
    }

    state_->worksheets.push_back(std::make_unique<detail::WorksheetWriterState>(std::move(name)));
    return WorksheetWriter(state_->worksheets.back().get());
}

void WorkbookWriter::close()
{
    if (!state_) {
        throw FastXlsxError("workbook writer is not initialized");
    }
    if (state_->closed) {
        return;
    }
    if (state_->worksheets.empty()) {
        throw FastXlsxError("workbook must contain at least one worksheet");
    }
    if (state_->options.string_strategy != StringStrategy::InlineString) {
        throw FastXlsxError("only inline string strategy is implemented");
    }

    std::vector<detail::ZipEntry> entries;
    const detail::PackageManifest manifest =
        detail::make_minimal_workbook_manifest(state_->worksheets.size());
    const auto* workbook_relationships =
        manifest.relationships_for(detail::PartName("/xl/workbook.xml"));
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook relationships missing from package manifest");
    }

    entries.reserve(4 + state_->worksheets.size());
    entries.push_back({"[Content_Types].xml", detail::serialize_content_types(manifest.content_types())});
    entries.push_back({"_rels/.rels", detail::serialize_relationships(manifest.package_relationships())});
    entries.push_back({"xl/workbook.xml", build_workbook(state_->worksheets)});
    entries.push_back(
        {"xl/_rels/workbook.xml.rels", detail::serialize_relationships(*workbook_relationships)});

    for (std::size_t index = 0; index < state_->worksheets.size(); ++index) {
        entries.push_back({"xl/worksheets/sheet" + std::to_string(index + 1) + ".xml",
            build_worksheet(*state_->worksheets[index])});
    }

    detail::write_stored_zip(state_->output_path, entries);
    state_->closed = true;
}

} // namespace fastxlsx
