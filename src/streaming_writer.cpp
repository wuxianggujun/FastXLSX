#include <fastxlsx/streaming_writer.hpp>

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/image.hpp>

#include "package_writer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fastxlsx {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576;
constexpr std::uint32_t max_excel_columns = 16384;
constexpr std::int64_t max_openxml_coordinate = 27273042316900LL;

struct ColumnWidth {
    std::uint32_t first_column = 1;
    std::uint32_t last_column = 1;
    double width = 0.0;
};

struct DataValidation {
    std::vector<CellRange> ranges;
    DataValidationRule rule;
};

struct ConditionalColorScale {
    std::vector<CellRange> ranges;
    ColorScalePoint lower;
    std::optional<ColorScalePoint> midpoint;
    ColorScalePoint upper;
    std::uint32_t priority = 1;
};

struct ConditionalDataBar {
    std::vector<CellRange> ranges;
    DataBarRule rule;
    std::uint32_t priority = 1;
};

struct ConditionalIconSet {
    std::vector<CellRange> ranges;
    IconSetRule rule;
    std::uint32_t priority = 1;
};

struct ExternalHyperlink {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
    std::string target_url;
    HyperlinkOptions options;
};

struct InternalHyperlink {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
    std::string location;
    HyperlinkOptions options;
};

struct WorksheetTable {
    CellRange range;
    TableOptions options;
};

struct WorksheetImage {
    CellRange anchor;
    ImageInfo info;
    std::filesystem::path media_path;
    ImageOptions options;
};

struct RegisteredStyle {
    CellStyle style;
    std::uint32_t number_format_id = 0;
    std::uint32_t font_id = 0;
    std::uint32_t fill_id = 0;
};

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
    detail::append_escaped_xml_text(xml, value);
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

std::string_view data_validation_type_name(DataValidationType type)
{
    switch (type) {
    case DataValidationType::Whole:
        return "whole";
    case DataValidationType::Decimal:
        return "decimal";
    case DataValidationType::List:
        return "list";
    case DataValidationType::Date:
        return "date";
    case DataValidationType::Time:
        return "time";
    case DataValidationType::TextLength:
        return "textLength";
    case DataValidationType::Custom:
        return "custom";
    }

    throw FastXlsxError("unknown data validation type");
}

std::string_view data_validation_operator_name(DataValidationOperator operator_type)
{
    switch (operator_type) {
    case DataValidationOperator::Between:
        return "between";
    case DataValidationOperator::NotBetween:
        return "notBetween";
    case DataValidationOperator::Equal:
        return "equal";
    case DataValidationOperator::NotEqual:
        return "notEqual";
    case DataValidationOperator::GreaterThan:
        return "greaterThan";
    case DataValidationOperator::LessThan:
        return "lessThan";
    case DataValidationOperator::GreaterThanOrEqual:
        return "greaterThanOrEqual";
    case DataValidationOperator::LessThanOrEqual:
        return "lessThanOrEqual";
    }

    throw FastXlsxError("unknown data validation operator");
}

std::string_view data_validation_error_style_name(DataValidationErrorStyle error_style)
{
    switch (error_style) {
    case DataValidationErrorStyle::Stop:
        return "stop";
    case DataValidationErrorStyle::Warning:
        return "warning";
    case DataValidationErrorStyle::Information:
        return "information";
    }

    throw FastXlsxError("unknown data validation error style");
}

std::string_view color_scale_value_type_name(ColorScaleValueType type)
{
    switch (type) {
    case ColorScaleValueType::Minimum:
        return "min";
    case ColorScaleValueType::Maximum:
        return "max";
    case ColorScaleValueType::Number:
        return "num";
    case ColorScaleValueType::Percent:
        return "percent";
    case ColorScaleValueType::Percentile:
        return "percentile";
    }

    throw FastXlsxError("unknown color scale value type");
}

std::string_view data_bar_value_type_name(DataBarValueType type)
{
    switch (type) {
    case DataBarValueType::Minimum:
        return "min";
    case DataBarValueType::Maximum:
        return "max";
    case DataBarValueType::Number:
        return "num";
    case DataBarValueType::Percent:
        return "percent";
    case DataBarValueType::Percentile:
        return "percentile";
    }

    throw FastXlsxError("unknown data bar value type");
}

std::string_view icon_set_style_name(IconSetStyle style)
{
    switch (style) {
    case IconSetStyle::ThreeArrows:
        return "3Arrows";
    }

    throw FastXlsxError("unknown icon set style");
}

std::string_view icon_set_value_type_name(IconSetValueType type)
{
    switch (type) {
    case IconSetValueType::Number:
        return "num";
    case IconSetValueType::Percent:
        return "percent";
    case IconSetValueType::Percentile:
        return "percentile";
    }

    throw FastXlsxError("unknown icon set value type");
}

bool color_scale_value_type_requires_value(ColorScaleValueType type)
{
    switch (type) {
    case ColorScaleValueType::Minimum:
    case ColorScaleValueType::Maximum:
        return false;
    case ColorScaleValueType::Number:
    case ColorScaleValueType::Percent:
    case ColorScaleValueType::Percentile:
        return true;
    }

    throw FastXlsxError("unknown color scale value type");
}

bool data_bar_value_type_requires_value(DataBarValueType type)
{
    switch (type) {
    case DataBarValueType::Minimum:
    case DataBarValueType::Maximum:
        return false;
    case DataBarValueType::Number:
    case DataBarValueType::Percent:
    case DataBarValueType::Percentile:
        return true;
    }

    throw FastXlsxError("unknown data bar value type");
}

std::string argb_color_value(ArgbColor color)
{
    constexpr char hex_digits[] = "0123456789ABCDEF";
    std::string value;
    value.reserve(8);
    const auto append_byte = [&value, &hex_digits](std::uint8_t byte) {
        value.push_back(hex_digits[(byte >> 4U) & 0x0FU]);
        value.push_back(hex_digits[byte & 0x0FU]);
    };
    append_byte(color.alpha);
    append_byte(color.red);
    append_byte(color.green);
    append_byte(color.blue);
    return value;
}

std::string worksheet_relationship_id(std::size_t index)
{
    return "rId" + std::to_string(index + 1);
}

std::string_view image_extension(ImageFormat format)
{
    switch (format) {
    case ImageFormat::Png:
        return "png";
    case ImageFormat::Jpeg:
        return "jpg";
    }

    throw FastXlsxError("unknown image format");
}

std::string_view image_content_type(ImageFormat format)
{
    switch (format) {
    case ImageFormat::Png:
        return "image/png";
    case ImageFormat::Jpeg:
        return "image/jpeg";
    }

    throw FastXlsxError("unknown image format");
}

bool is_ascii_letter(char ch) noexcept
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

char ascii_lower(char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

std::string ascii_lower_copy(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered.push_back(ascii_lower(ch));
    }
    return lowered;
}

bool looks_like_excel_cell_reference(std::string_view value)
{
    std::uint32_t column = 0;
    std::size_t offset = 0;
    while (offset < value.size() && is_ascii_letter(value[offset])) {
        column = column * 26 + static_cast<std::uint32_t>(ascii_lower(value[offset]) - 'a' + 1);
        if (column > max_excel_columns) {
            return false;
        }
        ++offset;
    }

    if (offset == 0 || offset == value.size()) {
        return false;
    }

    std::uint32_t row = 0;
    for (; offset < value.size(); ++offset) {
        if (!is_ascii_digit(value[offset])) {
            return false;
        }
        row = row * 10 + static_cast<std::uint32_t>(value[offset] - '0');
        if (row > max_excel_rows) {
            return false;
        }
    }

    return row > 0 && column > 0;
}

void validate_table_name(std::string_view name)
{
    if (name.empty()) {
        throw FastXlsxError("table name cannot be empty");
    }
    if (!(is_ascii_letter(name.front()) || name.front() == '_')) {
        throw FastXlsxError("table name must start with an ASCII letter or underscore");
    }
    for (const char ch : name) {
        if (!(is_ascii_letter(ch) || is_ascii_digit(ch) || ch == '_')) {
            throw FastXlsxError("table name must contain only ASCII letters, digits, and underscores");
        }
    }
    if (looks_like_excel_cell_reference(name)) {
        throw FastXlsxError("table name cannot look like an Excel cell reference");
    }
}

std::uint32_t range_width(CellRange range)
{
    return range.last_column - range.first_column + 1;
}

bool ranges_overlap(CellRange left, CellRange right)
{
    return left.first_row <= right.last_row && right.first_row <= left.last_row
        && left.first_column <= right.last_column && right.first_column <= left.last_column;
}

std::string_view table_totals_function_name(TableTotalsFunction function)
{
    switch (function) {
    case TableTotalsFunction::Sum:
        return "sum";
    case TableTotalsFunction::Count:
        return "count";
    case TableTotalsFunction::Average:
        return "average";
    case TableTotalsFunction::Maximum:
        return "max";
    case TableTotalsFunction::Minimum:
        return "min";
    case TableTotalsFunction::Product:
        return "product";
    case TableTotalsFunction::CountNumbers:
        return "countNums";
    case TableTotalsFunction::StandardDeviation:
        return "stdDev";
    case TableTotalsFunction::Variance:
        return "var";
    }

    throw FastXlsxError("unknown table totals function");
}

void validate_table_options(CellRange range, const TableOptions& options)
{
    (void)detail::range_reference(range);
    if (range.last_row == range.first_row) {
        throw FastXlsxError("table range must include at least one data row after the header");
    }
    if (options.show_totals_row && range.last_row <= range.first_row + 1) {
        throw FastXlsxError(
            "table range with totals row metadata must include header, data, and totals rows");
    }
    validate_table_name(options.name);

    const std::uint32_t width = range_width(range);
    if (options.column_names.size() != width) {
        throw FastXlsxError("table column name count must match the table range width");
    }
    if (!options.column_totals_functions.empty()
        && options.column_totals_functions.size() != width) {
        throw FastXlsxError("table totals function count must match the table range width");
    }
    if (!options.column_totals_labels.empty() && options.column_totals_labels.size() != width) {
        throw FastXlsxError("table totals label count must match the table range width");
    }
    if (!options.show_totals_row
        && (!options.column_totals_functions.empty() || !options.column_totals_labels.empty())) {
        throw FastXlsxError("table totals metadata requires visible totals row metadata");
    }
    if (options.show_totals_row) {
        const bool has_totals_function =
            std::any_of(options.column_totals_functions.begin(),
                options.column_totals_functions.end(),
                [](const auto& function) { return function.has_value(); });
        if (!has_totals_function) {
            throw FastXlsxError("visible table totals rows require at least one totals function");
        }
    }

    std::set<std::string> seen_column_names;
    for (const std::string& column_name : options.column_names) {
        if (column_name.empty()) {
            throw FastXlsxError("table column names cannot be empty");
        }
        if (!seen_column_names.insert(ascii_lower_copy(column_name)).second) {
            throw FastXlsxError("table column names must be unique within a table");
        }
    }
}

bool data_validation_operator_requires_formula2(DataValidationOperator operator_type) noexcept
{
    return operator_type == DataValidationOperator::Between
        || operator_type == DataValidationOperator::NotBetween;
}

void validate_data_validation_rule(const DataValidationRule& rule)
{
    if (rule.formula1.empty()) {
        throw FastXlsxError("data validation formula1 cannot be empty");
    }

    if (rule.hide_dropdown_arrow && rule.type != DataValidationType::List) {
        throw FastXlsxError("hide_dropdown_arrow is only valid for list data validations");
    }

    if (rule.type == DataValidationType::List || rule.type == DataValidationType::Custom) {
        if (rule.operator_type.has_value()) {
            throw FastXlsxError("list and custom data validations do not accept an operator");
        }
        if (!rule.formula2.empty()) {
            throw FastXlsxError("list and custom data validations do not accept formula2");
        }
        return;
    }

    if (!rule.operator_type.has_value()) {
        throw FastXlsxError("data validation operator is required for this type");
    }

    if (data_validation_operator_requires_formula2(*rule.operator_type)) {
        if (rule.formula2.empty()) {
            throw FastXlsxError("between data validations require formula2");
        }
    } else if (!rule.formula2.empty()) {
        throw FastXlsxError("single-formula data validation operator cannot use formula2");
    }
}

void validate_color_scale_point(const ColorScalePoint& point)
{
    (void)color_scale_value_type_name(point.type);
    if (color_scale_value_type_requires_value(point.type) && !std::isfinite(point.value)) {
        throw FastXlsxError("color scale endpoint values must be finite");
    }
}

void validate_two_color_scale_rule(const TwoColorScaleRule& rule)
{
    if (rule.lower.type == ColorScaleValueType::Maximum) {
        throw FastXlsxError("lower color scale endpoint cannot use maximum");
    }
    if (rule.upper.type == ColorScaleValueType::Minimum) {
        throw FastXlsxError("upper color scale endpoint cannot use minimum");
    }
    validate_color_scale_point(rule.lower);
    validate_color_scale_point(rule.upper);
}

void validate_data_bar_endpoint(const DataBarEndpoint& endpoint)
{
    (void)data_bar_value_type_name(endpoint.type);
    if (data_bar_value_type_requires_value(endpoint.type) && !std::isfinite(endpoint.value)) {
        throw FastXlsxError("data bar endpoint values must be finite");
    }
}

void validate_data_bar_rule(const DataBarRule& rule)
{
    if (rule.lower.type == DataBarValueType::Maximum) {
        throw FastXlsxError("lower data bar endpoint cannot use maximum");
    }
    if (rule.upper.type == DataBarValueType::Minimum) {
        throw FastXlsxError("upper data bar endpoint cannot use minimum");
    }
    validate_data_bar_endpoint(rule.lower);
    validate_data_bar_endpoint(rule.upper);
}

void validate_icon_set_rule(const IconSetRule& rule)
{
    (void)icon_set_style_name(rule.style);
    (void)icon_set_value_type_name(rule.value_type);
    for (double threshold : rule.thresholds) {
        if (!std::isfinite(threshold)) {
            throw FastXlsxError("icon set threshold values must be finite");
        }
    }
    if (!(rule.thresholds[0] < rule.thresholds[1]
            && rule.thresholds[1] < rule.thresholds[2])) {
        throw FastXlsxError("icon set threshold values must be strictly ascending");
    }
}

void validate_three_color_scale_rule(const ThreeColorScaleRule& rule)
{
    if (rule.lower.type == ColorScaleValueType::Maximum) {
        throw FastXlsxError("lower color scale endpoint cannot use maximum");
    }
    if (rule.midpoint.type == ColorScaleValueType::Minimum
        || rule.midpoint.type == ColorScaleValueType::Maximum) {
        throw FastXlsxError("middle color scale point must use a value-bearing type");
    }
    if (rule.upper.type == ColorScaleValueType::Minimum) {
        throw FastXlsxError("upper color scale endpoint cannot use minimum");
    }
    validate_color_scale_point(rule.lower);
    validate_color_scale_point(rule.midpoint);
    validate_color_scale_point(rule.upper);
}

std::filesystem::path make_temp_path()
{
    static std::atomic<std::uint64_t> counter {0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path()
        / ("fastxlsx-stream-" + std::to_string(tick) + "-" + std::to_string(id) + ".xml");
}

std::filesystem::path copy_image_to_temp_file(const std::filesystem::path& source_path)
{
    std::ifstream input(source_path, std::ios::binary);
    if (!input) {
        throw FastXlsxError("failed to open image file for package media part");
    }

    const std::filesystem::path temp_path = make_temp_path();
    std::ofstream output(temp_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary image media file");
    }

    output << input.rdbuf();
    if (input.bad()) {
        throw FastXlsxError("failed to read image file for package media part");
    }
    if (!output) {
        throw FastXlsxError("failed to write temporary image media file");
    }

    return temp_path;
}

std::filesystem::path copy_image_to_temp_file(std::span<const std::byte> bytes)
{
    if (bytes.empty()) {
        throw FastXlsxError("image memory buffer cannot be empty");
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw FastXlsxError("image memory buffer is too large for temporary media file");
    }

    const std::filesystem::path temp_path = make_temp_path();
    std::ofstream output(temp_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create temporary image media file");
    }

    output.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw FastXlsxError("failed to write temporary image media file");
    }

    return temp_path;
}

} // namespace

namespace detail {

struct WorkbookWriterState;

struct SharedStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view> {}(value);
    }

    std::size_t operator()(const std::string& value) const noexcept
    {
        return (*this)(std::string_view(value));
    }
};

struct SharedStringEqual {
    using is_transparent = void;

    bool operator()(std::string_view left, std::string_view right) const noexcept
    {
        return left == right;
    }

    bool operator()(const std::string& left, std::string_view right) const noexcept
    {
        return std::string_view(left) == right;
    }

    bool operator()(std::string_view left, const std::string& right) const noexcept
    {
        return left == std::string_view(right);
    }

    bool operator()(const std::string& left, const std::string& right) const noexcept
    {
        return left == right;
    }
};

#ifdef FASTXLSX_ENABLE_BENCHMARK_METRICS
namespace {
std::atomic<std::uint64_t> temporary_worksheet_part_footprint_bytes {0};
}

void reset_benchmark_metrics() noexcept
{
    temporary_worksheet_part_footprint_bytes.store(0, std::memory_order_relaxed);
}

std::uint64_t benchmark_temporary_worksheet_part_footprint_bytes() noexcept
{
    return temporary_worksheet_part_footprint_bytes.load(std::memory_order_relaxed);
}

void add_benchmark_temporary_worksheet_part_bytes(std::uint64_t bytes) noexcept
{
    temporary_worksheet_part_footprint_bytes.fetch_add(bytes, std::memory_order_relaxed);
}
#endif

struct SharedStringTable {
    std::size_t count = 0;
    std::vector<std::string> values;
    std::unordered_map<std::string, std::size_t, SharedStringHash, SharedStringEqual>
        index_by_value;
};

struct WorksheetWriterState {
    WorksheetWriterState(std::string sheet_name, WorkbookWriterState* workbook_state)
        : workbook(workbook_state)
        , name(std::move(sheet_name))
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
        for (const WorksheetImage& image : images) {
            std::filesystem::remove(image.media_path, ignored);
        }
    }

    WorksheetWriterState(const WorksheetWriterState&) = delete;
    WorksheetWriterState& operator=(const WorksheetWriterState&) = delete;

    WorkbookWriterState* workbook = nullptr;
    std::string name;
    std::filesystem::path body_path;
    std::ofstream body;
    std::uint32_t row_count = 0;
    std::uint32_t max_column = 0;
    std::vector<ColumnWidth> column_widths;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> frozen_panes;
    std::optional<CellRange> auto_filter;
    std::vector<CellRange> merged_ranges;
    std::uint32_t next_conditional_format_priority = 1;
    std::vector<ConditionalColorScale> conditional_color_scales;
    std::vector<ConditionalDataBar> conditional_data_bars;
    std::vector<ConditionalIconSet> conditional_icon_sets;
    std::vector<DataValidation> data_validations;
    std::vector<ExternalHyperlink> external_hyperlinks;
    std::vector<InternalHyperlink> internal_hyperlinks;
    std::vector<WorksheetTable> tables;
    std::vector<WorksheetImage> images;
    bool has_formula = false;
    std::string row_buffer;
};

struct WorkbookWriterState {
    std::filesystem::path output_path;
    WorkbookWriterOptions options;
    std::vector<std::unique_ptr<WorksheetWriterState>> worksheets;
    SharedStringTable shared_strings;
    std::vector<RegisteredStyle> styles;
    bool closed = false;
};

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void testing_set_worksheet_row_count(WorksheetWriter& worksheet, std::uint32_t row_count)
{
    if (worksheet.state_ == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (worksheet.state_->workbook != nullptr && worksheet.state_->workbook->closed) {
        throw FastXlsxError("cannot modify worksheet after workbook close");
    }
    worksheet.state_->row_count = row_count;
}
#endif

} // namespace detail

namespace {

bool uses_shared_strings(const detail::WorkbookWriterState& workbook) noexcept
{
    return workbook.options.string_strategy == StringStrategy::SharedString;
}

bool writes_styles(const detail::WorkbookWriterState& workbook) noexcept
{
    return !workbook.styles.empty();
}

bool has_number_format(const CellStyle& style) noexcept
{
    return !style.number_format.empty();
}

bool has_wrap_text_alignment(const CellStyle& style) noexcept
{
    return style.alignment.has_value() && style.alignment->wrap_text;
}

std::optional<HorizontalAlignment> horizontal_alignment(const CellStyle& style) noexcept
{
    if (!style.alignment.has_value()) {
        return std::nullopt;
    }
    return style.alignment->horizontal;
}

std::optional<VerticalAlignment> vertical_alignment(const CellStyle& style) noexcept
{
    if (!style.alignment.has_value()) {
        return std::nullopt;
    }
    return style.alignment->vertical;
}

bool has_alignment_property(const CellStyle& style) noexcept
{
    return has_wrap_text_alignment(style) || horizontal_alignment(style).has_value()
        || vertical_alignment(style).has_value();
}

bool has_font_property(const CellStyle& style) noexcept
{
    return style.font.has_value()
        && (style.font->bold || style.font->italic || style.font->color.has_value());
}

bool has_fill_property(const CellStyle& style) noexcept
{
    return style.fill.has_value();
}

bool has_supported_style_property(const CellStyle& style) noexcept
{
    return has_number_format(style) || has_alignment_property(style)
        || has_font_property(style) || has_fill_property(style);
}

bool same_argb_color(ArgbColor lhs, ArgbColor rhs) noexcept
{
    return lhs.alpha == rhs.alpha && lhs.red == rhs.red && lhs.green == rhs.green
        && lhs.blue == rhs.blue;
}

bool same_fill_properties(const CellFill& lhs, const CellFill& rhs) noexcept
{
    return same_argb_color(lhs.foreground, rhs.foreground);
}

bool same_font_properties(const CellFont& lhs, const CellFont& rhs) noexcept
{
    return lhs.bold == rhs.bold && lhs.italic == rhs.italic
        && lhs.color.has_value() == rhs.color.has_value()
        && (!lhs.color.has_value() || same_argb_color(*lhs.color, *rhs.color));
}

bool same_effective_font_properties(const CellStyle& lhs, const CellStyle& rhs) noexcept
{
    const bool lhs_has_font = has_font_property(lhs);
    const bool rhs_has_font = has_font_property(rhs);
    if (lhs_has_font != rhs_has_font) {
        return false;
    }
    if (!lhs_has_font) {
        return true;
    }
    return same_font_properties(*lhs.font, *rhs.font);
}

bool equivalent_style(const CellStyle& lhs, const CellStyle& rhs) noexcept
{
    return lhs.number_format == rhs.number_format
        && has_wrap_text_alignment(lhs) == has_wrap_text_alignment(rhs)
        && horizontal_alignment(lhs) == horizontal_alignment(rhs)
        && vertical_alignment(lhs) == vertical_alignment(rhs)
        && same_effective_font_properties(lhs, rhs)
        && lhs.fill.has_value() == rhs.fill.has_value()
        && (!lhs.fill.has_value() || same_fill_properties(*lhs.fill, *rhs.fill));
}

std::size_t custom_number_format_count(const std::vector<RegisteredStyle>& styles) noexcept
{
    std::size_t count = 0;
    for (auto style = styles.begin(); style != styles.end(); ++style) {
        if (style->number_format_id == 0) {
            continue;
        }
        const bool already_seen = std::any_of(styles.begin(), style,
            [id = style->number_format_id](const RegisteredStyle& previous) {
                return previous.number_format_id == id;
            });
        if (!already_seen) {
            ++count;
        }
    }
    return count;
}

std::optional<std::uint32_t> find_number_format_id(
    const std::vector<RegisteredStyle>& styles, std::string_view number_format)
{
    const auto existing = std::find_if(styles.begin(), styles.end(),
        [number_format](const RegisteredStyle& registered_style) {
            return registered_style.number_format_id != 0
                && registered_style.style.number_format == number_format;
        });
    if (existing == styles.end()) {
        return std::nullopt;
    }
    return existing->number_format_id;
}

std::size_t custom_font_count(const std::vector<RegisteredStyle>& styles) noexcept
{
    std::size_t count = 0;
    for (auto style = styles.begin(); style != styles.end(); ++style) {
        if (style->font_id == 0) {
            continue;
        }
        const bool already_seen = std::any_of(styles.begin(), style,
            [id = style->font_id](const RegisteredStyle& previous) {
                return previous.font_id == id;
            });
        if (!already_seen) {
            ++count;
        }
    }
    return count;
}

std::optional<std::uint32_t> find_font_id(
    const std::vector<RegisteredStyle>& styles, const CellFont& font)
{
    const auto existing = std::find_if(styles.begin(), styles.end(),
        [&font](const RegisteredStyle& registered_style) {
            return registered_style.font_id != 0 && registered_style.style.font.has_value()
                && same_font_properties(*registered_style.style.font, font);
        });
    if (existing == styles.end()) {
        return std::nullopt;
    }
    return existing->font_id;
}

std::size_t custom_fill_count(const std::vector<RegisteredStyle>& styles) noexcept
{
    std::size_t count = 0;
    for (auto style = styles.begin(); style != styles.end(); ++style) {
        if (style->fill_id == 0) {
            continue;
        }
        const bool already_seen = std::any_of(styles.begin(), style,
            [id = style->fill_id](const RegisteredStyle& previous) {
                return previous.fill_id == id;
            });
        if (!already_seen) {
            ++count;
        }
    }
    return count;
}

std::optional<std::uint32_t> find_fill_id(
    const std::vector<RegisteredStyle>& styles, const CellFill& fill)
{
    const auto existing = std::find_if(styles.begin(), styles.end(),
        [&fill](const RegisteredStyle& registered_style) {
            return registered_style.fill_id != 0 && registered_style.style.fill.has_value()
                && same_fill_properties(*registered_style.style.fill, fill);
        });
    if (existing == styles.end()) {
        return std::nullopt;
    }
    return existing->fill_id;
}

bool row_has_formula(std::span<const CellView> cells) noexcept
{
    return std::any_of(cells.begin(), cells.end(),
        [](const CellView& cell) { return cell.type() == CellView::Type::Formula; });
}

void ensure_mutable_worksheet(const detail::WorksheetWriterState* state)
{
    if (state == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (state->workbook != nullptr && state->workbook->closed) {
        throw FastXlsxError("cannot modify worksheet after workbook close");
    }
}

void validate_numeric_cells(std::span<const CellView> cells)
{
    for (const CellView& cell : cells) {
        if (cell.type() == CellView::Type::Number && !std::isfinite(cell.number_value())) {
            throw FastXlsxError("numeric values must be finite");
        }
    }
}

bool worksheet_has_relationships(const detail::WorksheetWriterState& worksheet) noexcept
{
    return !worksheet.external_hyperlinks.empty() || !worksheet.tables.empty()
        || !worksheet.images.empty();
}

bool workbook_has_table_name(
    const detail::WorkbookWriterState& workbook, std::string_view table_name) noexcept
{
    const std::string candidate = ascii_lower_copy(table_name);
    for (const auto& worksheet : workbook.worksheets) {
        for (const WorksheetTable& table : worksheet->tables) {
            if (ascii_lower_copy(table.options.name) == candidate) {
                return true;
            }
        }
    }
    return false;
}

std::size_t shared_string_index(detail::WorkbookWriterState& workbook, std::string_view value)
{
    auto& table = workbook.shared_strings;
    ++table.count;

    if (const auto existing = table.index_by_value.find(value);
        existing != table.index_by_value.end()) {
        return existing->second;
    }

    const std::size_t index = table.values.size();
    std::string key(value);
    table.values.push_back(key);
    table.index_by_value.emplace(std::move(key), index);
    return index;
}

void append_style_attribute(std::string& xml, StyleId style_id)
{
    if (style_id.value() == 0) {
        return;
    }

    xml += " s=\"";
    detail::append_unsigned_decimal(xml, style_id.value());
    xml += "\"";
}

void write_cell(std::string& xml, std::uint32_t row, std::uint32_t column, const CellView& cell,
    detail::WorksheetWriterState& worksheet)
{
    xml += "<c r=\"";
    detail::append_cell_reference(xml, row, column);
    switch (cell.type()) {
    case CellView::Type::Number:
        xml += "\"";
        append_style_attribute(xml, cell.style_id());
        xml += "><v>";
        detail::append_number(xml, cell.number_value());
        xml += "</v></c>";
        break;
    case CellView::Type::String:
        xml += "\"";
        append_style_attribute(xml, cell.style_id());
        if (worksheet.workbook != nullptr && uses_shared_strings(*worksheet.workbook)) {
            const std::size_t index = shared_string_index(*worksheet.workbook, cell.text_value());
            xml += " t=\"s\"><v>";
            detail::append_unsigned_decimal(xml, static_cast<std::uint64_t>(index));
            xml += "</v></c>";
        } else {
            xml += " t=\"inlineStr\"><is>";
            append_text_element(xml, cell.text_value());
            xml += "</is></c>";
        }
        break;
    case CellView::Type::Boolean:
        xml += "\"";
        append_style_attribute(xml, cell.style_id());
        xml += " t=\"b\"><v>";
        xml += cell.boolean_value() ? "1" : "0";
        xml += "</v></c>";
        break;
    case CellView::Type::Formula:
        xml += "\"";
        append_style_attribute(xml, cell.style_id());
        xml += "><f>";
        detail::append_escaped_xml_text(xml, cell.text_value());
        xml += "</f></c>";
        break;
    }
}

std::string worksheet_dimension(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.row_count == 0 || worksheet.max_column == 0) {
        return "A1";
    }
    std::string dimension = "A1:";
    detail::append_cell_reference(dimension, worksheet.row_count, worksheet.max_column);
    return dimension;
}

constexpr std::string_view recalculation_calc_id = "124519";

std::string build_workbook(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets,
    bool full_calc_on_load)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )";
    xml += R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
    xml += "<sheets>";
    for (std::size_t index = 0; index < worksheets.size(); ++index) {
        xml += R"(<sheet name=")";
        detail::append_escaped_xml_attribute(xml, worksheets[index]->name);
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

void write_stream(std::ofstream& stream, std::string_view xml, const char* error_message)
{
    stream.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    if (!stream) {
        throw FastXlsxError(error_message);
    }
}

void write_shared_strings_file(
    const std::filesystem::path& path, const detail::SharedStringTable& shared_strings)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to create temporary sharedStrings XML");
    }

    write_stream(stream,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)",
        "failed to write temporary sharedStrings XML");
    write_stream(stream,
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count=")",
        "failed to write temporary sharedStrings XML");
    write_stream(stream, std::to_string(shared_strings.count),
        "failed to write temporary sharedStrings XML");
    write_stream(stream, R"(" uniqueCount=")", "failed to write temporary sharedStrings XML");
    write_stream(stream, std::to_string(shared_strings.values.size()),
        "failed to write temporary sharedStrings XML");
    write_stream(stream, R"(">)", "failed to write temporary sharedStrings XML");

    for (const std::string& value : shared_strings.values) {
        std::string item_xml;
        item_xml.reserve(value.size() + 32);
        item_xml += "<si>";
        append_text_element(item_xml, value);
        item_xml += "</si>";
        write_stream(stream, item_xml, "failed to write temporary sharedStrings XML");
    }

    write_stream(stream, "</sst>", "failed to write temporary sharedStrings XML");
}

void append_default_font_xml(std::string& xml)
{
    xml += R"(<sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/>)";
}

void append_font_xml(std::string& xml, const CellFont& font)
{
    xml += "<font>";
    if (font.bold) {
        xml += "<b/>";
    }
    if (font.italic) {
        xml += "<i/>";
    }
    xml += R"(<sz val="11"/>)";
    if (font.color.has_value()) {
        xml += R"(<color rgb=")";
        xml += argb_color_value(*font.color);
        xml += R"("/>)";
    } else {
        xml += R"(<color theme="1"/>)";
    }
    xml += R"(<name val="Calibri"/><family val="2"/><scheme val="minor"/>)";
    xml += "</font>";
}

void append_fill_xml(std::string& xml, const CellFill& fill)
{
    xml += R"(<fill><patternFill patternType="solid"><fgColor rgb=")";
    xml += argb_color_value(fill.foreground);
    xml += R"("/><bgColor indexed="64"/></patternFill></fill>)";
}

std::string_view horizontal_alignment_token(HorizontalAlignment alignment)
{
    switch (alignment) {
    case HorizontalAlignment::Left:
        return "left";
    case HorizontalAlignment::Center:
        return "center";
    case HorizontalAlignment::Right:
        return "right";
    }
    throw FastXlsxError("unsupported horizontal alignment");
}

std::string_view vertical_alignment_token(VerticalAlignment alignment)
{
    switch (alignment) {
    case VerticalAlignment::Top:
        return "top";
    case VerticalAlignment::Center:
        return "center";
    case VerticalAlignment::Bottom:
        return "bottom";
    }
    throw FastXlsxError("unsupported vertical alignment");
}

void append_alignment_xml(std::string& xml, const CellStyle& style)
{
    xml += "<alignment";
    if (has_wrap_text_alignment(style)) {
        xml += R"( wrapText="1")";
    }
    if (const auto horizontal = horizontal_alignment(style)) {
        xml += R"( horizontal=")";
        xml += horizontal_alignment_token(*horizontal);
        xml += '"';
    }
    if (const auto vertical = vertical_alignment(style)) {
        xml += R"( vertical=")";
        xml += vertical_alignment_token(*vertical);
        xml += '"';
    }
    xml += "/>";
}

std::string build_styles_xml(const std::vector<RegisteredStyle>& styles)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)";
    const std::size_t number_format_count = custom_number_format_count(styles);
    if (number_format_count > 0) {
        xml += R"(<numFmts count=")";
        xml += std::to_string(number_format_count);
        xml += R"(">)";
        for (auto style = styles.begin(); style != styles.end(); ++style) {
            if (style->number_format_id == 0) {
                continue;
            }
            const bool already_emitted = std::any_of(styles.begin(), style,
                [id = style->number_format_id](const RegisteredStyle& previous) {
                    return previous.number_format_id == id;
                });
            if (already_emitted) {
                continue;
            }
            xml += R"(<numFmt numFmtId=")";
            xml += std::to_string(style->number_format_id);
            xml += R"(" formatCode=")";
            detail::append_escaped_xml_attribute(xml, style->style.number_format);
            xml += R"("/>)";
        }
        xml += "</numFmts>";
    }
    xml += R"(<fonts count=")";
    xml += std::to_string(custom_font_count(styles) + 1);
    xml += R"("><font>)";
    append_default_font_xml(xml);
    xml += "</font>";
    for (auto style = styles.begin(); style != styles.end(); ++style) {
        if (style->font_id == 0 || !style->style.font.has_value()) {
            continue;
        }
        const bool already_emitted = std::any_of(styles.begin(), style,
            [id = style->font_id](const RegisteredStyle& previous) {
                return previous.font_id == id;
            });
        if (already_emitted) {
            continue;
        }
        append_font_xml(xml, *style->style.font);
    }
    xml += "</fonts>";
    xml += R"(<fills count=")";
    xml += std::to_string(custom_fill_count(styles) + 2);
    xml += R"("><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill>)";
    for (auto style = styles.begin(); style != styles.end(); ++style) {
        if (style->fill_id == 0 || !style->style.fill.has_value()) {
            continue;
        }
        const bool already_emitted = std::any_of(styles.begin(), style,
            [id = style->fill_id](const RegisteredStyle& previous) {
                return previous.fill_id == id;
            });
        if (already_emitted) {
            continue;
        }
        append_fill_xml(xml, *style->style.fill);
    }
    xml += "</fills>";
    xml += R"(<borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders>)";
    xml += R"(<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>)";
    xml += R"(<cellXfs count=")";
    xml += std::to_string(styles.size() + 1);
    xml += R"("><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)";
    for (const RegisteredStyle& style : styles) {
        xml += R"(<xf numFmtId=")";
        xml += std::to_string(style.number_format_id);
        xml += R"(" fontId=")";
        xml += std::to_string(style.font_id);
        xml += R"(" fillId=")";
        xml += std::to_string(style.fill_id);
        xml += R"(" borderId="0" xfId="0")";
        if (style.number_format_id != 0) {
            xml += R"( applyNumberFormat="1")";
        }
        if (style.font_id != 0) {
            xml += R"( applyFont="1")";
        }
        if (style.fill_id != 0) {
            xml += R"( applyFill="1")";
        }
        if (has_alignment_property(style.style)) {
            xml += R"( applyAlignment="1")";
        }
        if (has_alignment_property(style.style)) {
            xml += ">";
            append_alignment_xml(xml, style.style);
            xml += "</xf>";
        } else {
            xml += "/>";
        }
    }
    xml += "</cellXfs>";
    xml += R"(<cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles>)";
    xml += R"(<dxfs count="0"/><tableStyles count="0" defaultTableStyle="TableStyleMedium2" defaultPivotStyle="PivotStyleLight16"/>)";
    xml += "</styleSheet>";
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
    detail::append_cell_reference(xml, row_split + 1, column_split + 1);
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
        detail::append_number(xml, width.width);
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

void append_color_scale_point_xml(std::string& xml, const ColorScalePoint& point)
{
    xml += "<cfvo type=\"";
    xml += color_scale_value_type_name(point.type);
    if (color_scale_value_type_requires_value(point.type)) {
        xml += "\" val=\"";
        detail::append_number(xml, point.value);
    }
    xml += "\"/>";
}

void append_color_scale_color_xml(std::string& xml, const ColorScalePoint& point)
{
    xml += "<color rgb=\"";
    xml += argb_color_value(point.color);
    xml += "\"/>";
}

void append_data_bar_endpoint_xml(std::string& xml, const DataBarEndpoint& endpoint)
{
    xml += "<cfvo type=\"";
    xml += data_bar_value_type_name(endpoint.type);
    if (data_bar_value_type_requires_value(endpoint.type)) {
        xml += "\" val=\"";
        detail::append_number(xml, endpoint.value);
    }
    xml += "\"/>";
}

void append_conditional_color_scale_xml(std::string& xml, const ConditionalColorScale& scale)
{
    xml += "<conditionalFormatting sqref=\"";
    xml += detail::sqref(scale.ranges);
    xml += "\"><cfRule type=\"colorScale\" priority=\"";
    xml += std::to_string(scale.priority);
    xml += "\"><colorScale>";
    append_color_scale_point_xml(xml, scale.lower);
    if (scale.midpoint.has_value()) {
        append_color_scale_point_xml(xml, *scale.midpoint);
    }
    append_color_scale_point_xml(xml, scale.upper);
    append_color_scale_color_xml(xml, scale.lower);
    if (scale.midpoint.has_value()) {
        append_color_scale_color_xml(xml, *scale.midpoint);
    }
    append_color_scale_color_xml(xml, scale.upper);
    xml += "</colorScale></cfRule></conditionalFormatting>";
}

void append_conditional_data_bar_xml(std::string& xml, const ConditionalDataBar& bar)
{
    xml += "<conditionalFormatting sqref=\"";
    xml += detail::sqref(bar.ranges);
    xml += "\"><cfRule type=\"dataBar\" priority=\"";
    xml += std::to_string(bar.priority);
    xml += "\"><dataBar";
    if (!bar.rule.show_value) {
        xml += " showValue=\"0\"";
    }
    xml += ">";
    append_data_bar_endpoint_xml(xml, bar.rule.lower);
    append_data_bar_endpoint_xml(xml, bar.rule.upper);
    xml += "<color rgb=\"";
    xml += argb_color_value(bar.rule.color);
    xml += "\"/></dataBar></cfRule></conditionalFormatting>";
}

void append_icon_set_threshold_xml(
    std::string& xml, IconSetValueType value_type, double threshold)
{
    xml += "<cfvo type=\"";
    xml += icon_set_value_type_name(value_type);
    xml += "\" val=\"";
    detail::append_number(xml, threshold);
    xml += "\"/>";
}

void append_conditional_icon_set_xml(std::string& xml, const ConditionalIconSet& icon_set)
{
    xml += "<conditionalFormatting sqref=\"";
    xml += detail::sqref(icon_set.ranges);
    xml += "\"><cfRule type=\"iconSet\" priority=\"";
    xml += std::to_string(icon_set.priority);
    xml += "\"><iconSet iconSet=\"";
    xml += icon_set_style_name(icon_set.rule.style);
    if (!icon_set.rule.show_value) {
        xml += "\" showValue=\"0";
    }
    if (icon_set.rule.reverse) {
        xml += "\" reverse=\"1";
    }
    xml += "\">";
    for (double threshold : icon_set.rule.thresholds) {
        append_icon_set_threshold_xml(xml, icon_set.rule.value_type, threshold);
    }
    xml += "</iconSet></cfRule></conditionalFormatting>";
}

std::string build_conditional_formattings(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.conditional_color_scales.empty() && worksheet.conditional_data_bars.empty()
        && worksheet.conditional_icon_sets.empty()) {
        return {};
    }

    std::string xml;
    auto color_scale_it = worksheet.conditional_color_scales.begin();
    auto data_bar_it = worksheet.conditional_data_bars.begin();
    auto icon_set_it = worksheet.conditional_icon_sets.begin();
    while (color_scale_it != worksheet.conditional_color_scales.end()
        || data_bar_it != worksheet.conditional_data_bars.end()
        || icon_set_it != worksheet.conditional_icon_sets.end()) {
        std::uint32_t next_priority = std::numeric_limits<std::uint32_t>::max();
        enum class NextRule {
            ColorScale,
            DataBar,
            IconSet,
        } next_rule = NextRule::ColorScale;
        if (color_scale_it != worksheet.conditional_color_scales.end()
            && color_scale_it->priority < next_priority) {
            next_priority = color_scale_it->priority;
            next_rule = NextRule::ColorScale;
        }
        if (data_bar_it != worksheet.conditional_data_bars.end()
            && data_bar_it->priority < next_priority) {
            next_priority = data_bar_it->priority;
            next_rule = NextRule::DataBar;
        }
        if (icon_set_it != worksheet.conditional_icon_sets.end()
            && icon_set_it->priority < next_priority) {
            next_rule = NextRule::IconSet;
        }

        switch (next_rule) {
        case NextRule::ColorScale:
            append_conditional_color_scale_xml(xml, *color_scale_it);
            ++color_scale_it;
            break;
        case NextRule::DataBar:
            append_conditional_data_bar_xml(xml, *data_bar_it);
            ++data_bar_it;
            break;
        case NextRule::IconSet:
            append_conditional_icon_set_xml(xml, *icon_set_it);
            ++icon_set_it;
            break;
        }
    }
    return xml;
}

std::string build_data_validations(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.data_validations.empty()) {
        return {};
    }

    std::string xml = "<dataValidations count=\"";
    xml += std::to_string(worksheet.data_validations.size());
    xml += "\">";
    for (const DataValidation& validation : worksheet.data_validations) {
        xml += "<dataValidation type=\"";
        xml += data_validation_type_name(validation.rule.type);
        xml += "\"";
        if (validation.rule.allow_blank) {
            xml += " allowBlank=\"1\"";
        }
        if (validation.rule.hide_dropdown_arrow) {
            xml += " showDropDown=\"1\"";
        }
        if (validation.rule.show_input_message) {
            xml += " showInputMessage=\"1\"";
        }
        if (validation.rule.show_error_message) {
            xml += " showErrorMessage=\"1\"";
        }
        if (validation.rule.error_style.has_value()) {
            xml += " errorStyle=\"";
            xml += data_validation_error_style_name(*validation.rule.error_style);
            xml += "\"";
        }
        if (!validation.rule.error_title.empty()) {
            xml += " errorTitle=\"";
            detail::append_escaped_xml_attribute(xml, validation.rule.error_title);
            xml += "\"";
        }
        if (!validation.rule.error.empty()) {
            xml += " error=\"";
            detail::append_escaped_xml_attribute(xml, validation.rule.error);
            xml += "\"";
        }
        if (!validation.rule.prompt_title.empty()) {
            xml += " promptTitle=\"";
            detail::append_escaped_xml_attribute(xml, validation.rule.prompt_title);
            xml += "\"";
        }
        if (!validation.rule.prompt.empty()) {
            xml += " prompt=\"";
            detail::append_escaped_xml_attribute(xml, validation.rule.prompt);
            xml += "\"";
        }
        if (validation.rule.operator_type.has_value()) {
            xml += " operator=\"";
            xml += data_validation_operator_name(*validation.rule.operator_type);
            xml += "\"";
        }
        xml += " sqref=\"";
        xml += detail::sqref(validation.ranges);
        xml += "\"><formula1>";
        detail::append_escaped_xml_text(xml, validation.rule.formula1);
        xml += "</formula1>";
        if (!validation.rule.formula2.empty()) {
            xml += "<formula2>";
            detail::append_escaped_xml_text(xml, validation.rule.formula2);
            xml += "</formula2>";
        }
        xml += "</dataValidation>";
    }
    xml += "</dataValidations>";
    return xml;
}

std::string build_hyperlinks(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.external_hyperlinks.empty() && worksheet.internal_hyperlinks.empty()) {
        return {};
    }

    std::string xml = "<hyperlinks>";
    for (std::size_t index = 0; index < worksheet.external_hyperlinks.size(); ++index) {
        const ExternalHyperlink& hyperlink = worksheet.external_hyperlinks[index];
        xml += "<hyperlink ref=\"";
        detail::append_cell_reference(xml, hyperlink.row, hyperlink.column);
        xml += "\" r:id=\"";
        xml += worksheet_relationship_id(index);
        xml += "\"";
        if (!hyperlink.options.display.empty()) {
            xml += " display=\"";
            detail::append_escaped_xml_attribute(xml, hyperlink.options.display);
            xml += "\"";
        }
        if (!hyperlink.options.tooltip.empty()) {
            xml += " tooltip=\"";
            detail::append_escaped_xml_attribute(xml, hyperlink.options.tooltip);
            xml += "\"";
        }
        xml += "/>";
    }
    for (const InternalHyperlink& hyperlink : worksheet.internal_hyperlinks) {
        xml += "<hyperlink ref=\"";
        detail::append_cell_reference(xml, hyperlink.row, hyperlink.column);
        xml += "\" location=\"";
        detail::append_escaped_xml_attribute(xml, hyperlink.location);
        xml += "\"";
        if (!hyperlink.options.display.empty()) {
            xml += " display=\"";
            detail::append_escaped_xml_attribute(xml, hyperlink.options.display);
            xml += "\"";
        }
        if (!hyperlink.options.tooltip.empty()) {
            xml += " tooltip=\"";
            detail::append_escaped_xml_attribute(xml, hyperlink.options.tooltip);
            xml += "\"";
        }
        xml += "/>";
    }
    xml += "</hyperlinks>";
    return xml;
}

std::string table_relationship_id(const detail::WorksheetWriterState& worksheet, std::size_t table_index)
{
    const std::size_t drawing_relationship_count = worksheet.images.empty() ? 0 : 1;
    return worksheet_relationship_id(
        worksheet.external_hyperlinks.size() + drawing_relationship_count + table_index);
}

std::string drawing_relationship_id(const detail::WorksheetWriterState& worksheet)
{
    return worksheet_relationship_id(worksheet.external_hyperlinks.size());
}

std::string build_table_parts(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.tables.empty()) {
        return {};
    }

    std::string xml = "<tableParts count=\"";
    xml += std::to_string(worksheet.tables.size());
    xml += "\">";
    for (std::size_t index = 0; index < worksheet.tables.size(); ++index) {
        xml += "<tablePart r:id=\"";
        xml += table_relationship_id(worksheet, index);
        xml += "\"/>";
    }
    xml += "</tableParts>";
    return xml;
}

std::string build_drawing_reference(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.images.empty()) {
        return {};
    }

    std::string xml = "<drawing r:id=\"";
    xml += drawing_relationship_id(worksheet);
    xml += "\"/>";
    return xml;
}

std::string build_worksheet_prefix(const detail::WorksheetWriterState& worksheet)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main")";
    if (worksheet_has_relationships(worksheet)) {
        xml += R"( xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")";
    }
    xml += ">";
    xml += R"(<dimension ref=")";
    xml += worksheet_dimension(worksheet);
    xml += "\"/>";
    xml += build_sheet_views(worksheet);
    xml += build_columns(worksheet);
    xml += "<sheetData>";
    return xml;
}

std::string build_worksheet_suffix(const detail::WorksheetWriterState& worksheet)
{
    std::string xml;
    xml += "</sheetData>";
    if (worksheet.auto_filter.has_value()) {
        xml += "<autoFilter ref=\"";
        xml += detail::range_reference(*worksheet.auto_filter);
        xml += "\"/>";
    }
    xml += build_merge_cells(worksheet);
    xml += build_conditional_formattings(worksheet);
    xml += build_data_validations(worksheet);
    xml += build_hyperlinks(worksheet);
    xml += build_drawing_reference(worksheet);
    xml += build_table_parts(worksheet);
    xml += "</worksheet>";
    return xml;
}

detail::PackageEntry build_worksheet_entry(std::string entry_name, detail::WorksheetWriterState& worksheet)
{
    if (worksheet.body.is_open()) {
        worksheet.body.close();
    }

    std::vector<detail::PackageEntryChunk> chunks;
    chunks.reserve(3);
    chunks.push_back(detail::PackageEntryChunk::memory(build_worksheet_prefix(worksheet)));
    chunks.push_back(detail::PackageEntryChunk::file(worksheet.body_path));
    chunks.push_back(detail::PackageEntryChunk::memory(build_worksheet_suffix(worksheet)));
    return {std::move(entry_name), std::move(chunks)};
}

std::string table_entry_name(std::size_t table_index)
{
    return "xl/tables/table" + std::to_string(table_index + 1) + ".xml";
}

std::string drawing_entry_name(std::size_t drawing_index)
{
    return "xl/drawings/drawing" + std::to_string(drawing_index + 1) + ".xml";
}

std::string drawing_relationship_entry_name(std::size_t drawing_index)
{
    return "xl/drawings/_rels/drawing" + std::to_string(drawing_index + 1) + ".xml.rels";
}

std::string media_entry_name(const WorksheetImage& image, std::size_t image_index)
{
    std::string name = "xl/media/image" + std::to_string(image_index + 1) + ".";
    name += image_extension(image.info.format);
    return name;
}

std::string build_worksheet_relationships(
    const detail::WorksheetWriterState& worksheet, std::size_t first_table_index,
    std::size_t drawing_index)
{
    detail::RelationshipSet relationships;
    constexpr std::string_view hyperlink_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    constexpr std::string_view drawing_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
    constexpr std::string_view table_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";

    for (std::size_t index = 0; index < worksheet.external_hyperlinks.size(); ++index) {
        relationships.add(worksheet_relationship_id(index),
            std::string(hyperlink_type),
            worksheet.external_hyperlinks[index].target_url,
            detail::Relationship::TargetMode::External);
    }

    if (!worksheet.images.empty()) {
        relationships.add(drawing_relationship_id(worksheet),
            std::string(drawing_type),
            "../drawings/drawing" + std::to_string(drawing_index + 1) + ".xml");
    }

    for (std::size_t index = 0; index < worksheet.tables.size(); ++index) {
        relationships.add(table_relationship_id(worksheet, index),
            std::string(table_type),
            "../tables/table" + std::to_string(first_table_index + index + 1) + ".xml");
    }

    return detail::serialize_relationships(relationships);
}

std::string worksheet_relationship_entry_name(std::size_t worksheet_index)
{
    const std::string sheet_name = "sheet" + std::to_string(worksheet_index + 1) + ".xml";
    return "xl/worksheets/_rels/" + sheet_name + ".rels";
}

std::string build_table_xml(const WorksheetTable& table, std::size_t table_index)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id=")";
    xml += std::to_string(table_index + 1);
    xml += R"(" name=")";
    detail::append_escaped_xml_attribute(xml, table.options.name);
    xml += R"(" displayName=")";
    detail::append_escaped_xml_attribute(xml, table.options.name);
    xml += R"(" ref=")";
    xml += detail::range_reference(table.range);
    if (table.options.show_totals_row) {
        xml += R"(" totalsRowCount="1">)";
    } else {
        xml += R"(" totalsRowShown="0">)";
    }
    xml += R"(<autoFilter ref=")";
    CellRange auto_filter_range = table.range;
    if (table.options.show_totals_row) {
        --auto_filter_range.last_row;
    }
    xml += detail::range_reference(auto_filter_range);
    xml += R"("/>)";
    xml += R"(<tableColumns count=")";
    xml += std::to_string(table.options.column_names.size());
    xml += R"(">)";
    for (std::size_t index = 0; index < table.options.column_names.size(); ++index) {
        xml += R"(<tableColumn id=")";
        xml += std::to_string(index + 1);
        xml += R"(" name=")";
        detail::append_escaped_xml_attribute(xml, table.options.column_names[index]);
        if (!table.options.column_totals_labels.empty()
            && !table.options.column_totals_labels[index].empty()) {
            xml += R"(" totalsRowLabel=")";
            detail::append_escaped_xml_attribute(xml, table.options.column_totals_labels[index]);
        }
        if (!table.options.column_totals_functions.empty()
            && table.options.column_totals_functions[index].has_value()) {
            xml += R"(" totalsRowFunction=")";
            xml += table_totals_function_name(*table.options.column_totals_functions[index]);
        }
        xml += R"("/>)";
    }
    xml += "</tableColumns>";
    if (!table.options.style_name.empty()) {
        xml += R"(<tableStyleInfo name=")";
        detail::append_escaped_xml_attribute(xml, table.options.style_name);
        xml += R"(" showFirstColumn=")";
        xml += table.options.show_first_column ? "1" : "0";
        xml += R"(" showLastColumn=")";
        xml += table.options.show_last_column ? "1" : "0";
        xml += R"(" showRowStripes=")";
        xml += table.options.show_row_stripes ? "1" : "0";
        xml += R"(" showColumnStripes=")";
        xml += table.options.show_column_stripes ? "1" : "0";
        xml += R"("/>)";
    }
    xml += "</table>";
    return xml;
}

std::string drawing_relationship_id(std::size_t image_index)
{
    return worksheet_relationship_id(image_index);
}

bool has_external_hyperlink(const WorksheetImage& image) noexcept
{
    return !image.options.external_hyperlink_url.empty();
}

std::size_t image_hyperlink_index_before(
    const detail::WorksheetWriterState& worksheet, std::size_t image_index) noexcept
{
    std::size_t count = 0;
    for (std::size_t index = 0; index < image_index; ++index) {
        if (has_external_hyperlink(worksheet.images[index])) {
            ++count;
        }
    }
    return count;
}

std::string drawing_hyperlink_relationship_id(
    const detail::WorksheetWriterState& worksheet, std::size_t image_index)
{
    return worksheet_relationship_id(
        worksheet.images.size() + image_hyperlink_index_before(worksheet, image_index));
}

std::string_view image_edit_as_name(ImageEditAs edit_as)
{
    switch (edit_as) {
    case ImageEditAs::TwoCell:
        return "twoCell";
    case ImageEditAs::OneCell:
        return "oneCell";
    case ImageEditAs::Absolute:
        return "absolute";
    }

    throw FastXlsxError("unknown image editAs mode");
}

void validate_image_offset(ImageAnchorOffset offset)
{
    if (offset.column_emu < 0 || offset.row_emu < 0) {
        throw FastXlsxError("image anchor offsets must be non-negative EMU values");
    }
    if (offset.column_emu > max_openxml_coordinate || offset.row_emu > max_openxml_coordinate) {
        throw FastXlsxError("image anchor offsets exceed OpenXML coordinate bounds");
    }
}

void validate_image_options(const ImageOptions& options)
{
    validate_image_offset(options.from_offset);
    validate_image_offset(options.to_offset);
    if (options.external_hyperlink_url.empty() && !options.external_hyperlink_tooltip.empty()) {
        throw FastXlsxError("image hyperlink tooltip requires an external hyperlink URL");
    }
}

void validate_cell_style(const CellStyle& style)
{
    if (!has_supported_style_property(style)) {
        throw FastXlsxError("cell style must set at least one supported property");
    }
    if (const auto horizontal = horizontal_alignment(style)) {
        static_cast<void>(horizontal_alignment_token(*horizontal));
    }
    if (const auto vertical = vertical_alignment(style)) {
        static_cast<void>(vertical_alignment_token(*vertical));
    }
}

std::string build_drawing_marker_xml(
    std::string_view element_name, std::uint32_t row, std::uint32_t column,
    ImageAnchorOffset offset)
{
    std::string xml;
    xml += "<xdr:";
    xml += element_name;
    xml += "><xdr:col>";
    xml += std::to_string(column - 1);
    xml += "</xdr:col><xdr:colOff>";
    xml += std::to_string(offset.column_emu);
    xml += "</xdr:colOff><xdr:row>";
    xml += std::to_string(row - 1);
    xml += "</xdr:row><xdr:rowOff>";
    xml += std::to_string(offset.row_emu);
    xml += "</xdr:rowOff></xdr:";
    xml += element_name;
    xml += ">";
    return xml;
}

std::string build_drawing_xml(
    const detail::WorksheetWriterState& worksheet, std::size_t first_image_index)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" )";
    xml += R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" )";
    xml += R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";

    for (std::size_t index = 0; index < worksheet.images.size(); ++index) {
        const WorksheetImage& image = worksheet.images[index];
        const std::size_t package_image_index = first_image_index + index;
        const std::uint64_t width_emu = static_cast<std::uint64_t>(image.info.width) * 9525U;
        const std::uint64_t height_emu = static_cast<std::uint64_t>(image.info.height) * 9525U;

        xml += R"(<xdr:twoCellAnchor editAs=")";
        xml += image_edit_as_name(image.options.edit_as);
        xml += R"(">)";
        xml += build_drawing_marker_xml(
            "from", image.anchor.first_row, image.anchor.first_column, image.options.from_offset);
        xml += build_drawing_marker_xml(
            "to", image.anchor.last_row + 1, image.anchor.last_column + 1, image.options.to_offset);
        xml += "<xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"";
        xml += std::to_string(package_image_index + 1);
        xml += "\" name=\"";
        if (image.options.name.empty()) {
            xml += "Picture ";
            xml += std::to_string(package_image_index + 1);
        } else {
            detail::append_escaped_xml_attribute(xml, image.options.name);
        }
        xml += "\"";
        if (!image.options.description.empty()) {
            xml += " descr=\"";
            detail::append_escaped_xml_attribute(xml, image.options.description);
            xml += "\"";
        }
        if (has_external_hyperlink(image)) {
            xml += R"(><a:hlinkClick r:id=")";
            xml += drawing_hyperlink_relationship_id(worksheet, index);
            xml += "\"";
            if (!image.options.external_hyperlink_tooltip.empty()) {
                xml += R"( tooltip=")";
                detail::append_escaped_xml_attribute(
                    xml, image.options.external_hyperlink_tooltip);
                xml += "\"";
            }
            xml += R"(/></xdr:cNvPr>)";
        } else {
            xml += "/>";
        }
        xml += R"(<xdr:cNvPicPr><a:picLocks noChangeAspect="1"/>)";
        xml += "</xdr:cNvPicPr></xdr:nvPicPr><xdr:blipFill><a:blip r:embed=\"";
        xml += drawing_relationship_id(index);
        xml += R"("/><a:stretch><a:fillRect/></a:stretch></xdr:blipFill>)";
        xml += R"(<xdr:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx=")";
        xml += std::to_string(width_emu);
        xml += R"(" cy=")";
        xml += std::to_string(height_emu);
        xml += R"("/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom>)";
        xml += "</xdr:spPr></xdr:pic><xdr:clientData/></xdr:twoCellAnchor>";
    }

    xml += "</xdr:wsDr>";
    return xml;
}

std::string build_drawing_relationships(
    const detail::WorksheetWriterState& worksheet, std::size_t first_image_index)
{
    detail::RelationshipSet relationships;
    constexpr std::string_view image_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
    constexpr std::string_view hyperlink_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";

    for (std::size_t index = 0; index < worksheet.images.size(); ++index) {
        const std::size_t package_image_index = first_image_index + index;
        relationships.add(drawing_relationship_id(index),
            std::string(image_type),
            "../media/image" + std::to_string(package_image_index + 1) + "."
                + std::string(image_extension(worksheet.images[index].info.format)));
    }
    for (std::size_t index = 0; index < worksheet.images.size(); ++index) {
        const WorksheetImage& image = worksheet.images[index];
        if (!has_external_hyperlink(image)) {
            continue;
        }
        relationships.add(drawing_hyperlink_relationship_id(worksheet, index),
            std::string(hyperlink_type),
            image.options.external_hyperlink_url,
            detail::Relationship::TargetMode::External);
    }

    return detail::serialize_relationships(relationships);
}

std::size_t count_tables(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::size_t count = 0;
    for (const auto& worksheet : worksheets) {
        count += worksheet->tables.size();
    }
    return count;
}

std::size_t count_images(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::size_t count = 0;
    for (const auto& worksheet : worksheets) {
        count += worksheet->images.size();
    }
    return count;
}

std::size_t count_drawings(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    return static_cast<std::size_t>(
        std::count_if(worksheets.begin(), worksheets.end(),
            [](const auto& worksheet) { return !worksheet->images.empty(); }));
}

std::vector<std::size_t> table_start_indexes(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::vector<std::size_t> starts;
    starts.reserve(worksheets.size());
    std::size_t next = 0;
    for (const auto& worksheet : worksheets) {
        starts.push_back(next);
        next += worksheet->tables.size();
    }
    return starts;
}

std::vector<std::size_t> image_start_indexes(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::vector<std::size_t> starts;
    starts.reserve(worksheets.size());
    std::size_t next = 0;
    for (const auto& worksheet : worksheets) {
        starts.push_back(next);
        next += worksheet->images.size();
    }
    return starts;
}

std::vector<std::size_t> worksheet_drawing_indexes(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    constexpr std::size_t no_drawing = std::numeric_limits<std::size_t>::max();

    std::vector<std::size_t> drawing_indexes;
    drawing_indexes.reserve(worksheets.size());
    std::size_t next = 0;
    for (const auto& worksheet : worksheets) {
        if (worksheet->images.empty()) {
            drawing_indexes.push_back(no_drawing);
        } else {
            drawing_indexes.push_back(next);
            ++next;
        }
    }
    return drawing_indexes;
}

bool workbook_has_image_format(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets,
    ImageFormat format) noexcept
{
    for (const auto& worksheet : worksheets) {
        for (const WorksheetImage& image : worksheet->images) {
            if (image.info.format == format) {
                return true;
            }
        }
    }
    return false;
}

class TemporaryFile {
public:
    TemporaryFile()
        : path_(make_temp_path())
    {
    }

    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    TemporaryFile(TemporaryFile&& other) noexcept
        : path_(std::move(other.path_))
    {
        other.path_.clear();
    }

    TemporaryFile& operator=(TemporaryFile&& other) noexcept
    {
        if (this != &other) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

StyleId::StyleId(std::uint32_t value, std::uintptr_t owner_token) noexcept
    : value_(value)
    , owner_token_(owner_token)
{
}

std::uint32_t StyleId::value() const noexcept
{
    return value_;
}

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

CellView CellView::with_style(StyleId style_id) const noexcept
{
    CellView cell = *this;
    cell.style_id_ = style_id;
    return cell;
}

StyleId CellView::style_id() const noexcept
{
    return style_id_;
}

WorksheetWriter::WorksheetWriter() noexcept = default;

WorksheetWriter::WorksheetWriter(detail::WorksheetWriterState* state) noexcept
    : state_(state)
{
}

void WorksheetWriter::append_row(std::span<const CellView> cells, RowOptions options)
{
    ensure_mutable_worksheet(state_);
    if (cells.size() > max_excel_columns) {
        throw FastXlsxError("row exceeds Excel's column limit");
    }
    if (state_->row_count >= max_excel_rows) {
        throw FastXlsxError("worksheet exceeds Excel's row limit");
    }
    if (options.height.has_value()
        && (!std::isfinite(*options.height) || *options.height <= 0.0)) {
        throw FastXlsxError("row height must be positive and finite");
    }
    validate_numeric_cells(cells);
    if (state_->workbook != nullptr) {
        const std::uintptr_t owner_token =
            reinterpret_cast<std::uintptr_t>(state_->workbook);
        for (const CellView& cell : cells) {
            const StyleId style_id = cell.style_id();
            if (style_id.value_ == 0) {
                continue;
            }
            if (style_id.owner_token_ != owner_token
                || style_id.value_ > state_->workbook->styles.size()) {
                throw FastXlsxError("cell style id is not registered in this workbook");
            }
        }
    }

    ++state_->row_count;
    state_->max_column = std::max(state_->max_column, static_cast<std::uint32_t>(cells.size()));
    state_->has_formula = state_->has_formula || row_has_formula(cells);

    std::string& row_xml = state_->row_buffer;
    row_xml.clear();
    const std::size_t expected_row_size = 32 + cells.size() * 48;
    if (row_xml.capacity() < expected_row_size) {
        row_xml.reserve(expected_row_size);
    }

    row_xml += "<row r=\"";
    detail::append_unsigned_decimal(row_xml, state_->row_count);
    if (options.height.has_value()) {
        row_xml += "\" ht=\"";
        detail::append_number(row_xml, *options.height);
        row_xml += "\" customHeight=\"1";
    }
    row_xml += "\">";
    for (std::uint32_t column = 0; column < cells.size(); ++column) {
        write_cell(row_xml, state_->row_count, column + 1, cells[column], *state_);
    }
    row_xml += "</row>";

    state_->body.write(row_xml.data(), static_cast<std::streamsize>(row_xml.size()));
    if (!state_->body) {
        throw FastXlsxError("failed to write worksheet row XML");
    }
#ifdef FASTXLSX_ENABLE_BENCHMARK_METRICS
    detail::add_benchmark_temporary_worksheet_part_bytes(
        static_cast<std::uint64_t>(row_xml.size()));
#endif
}

void WorksheetWriter::append_row(std::initializer_list<CellView> cells, RowOptions options)
{
    append_row(std::span<const CellView>(cells.begin(), cells.size()), options);
}

void WorksheetWriter::set_column_width(std::uint32_t first_column, std::uint32_t last_column, double width)
{
    ensure_mutable_worksheet(state_);
    if (first_column == 0 || last_column == 0 || first_column > last_column
        || last_column > max_excel_columns || !std::isfinite(width) || width <= 0.0) {
        throw FastXlsxError("invalid column width range");
    }
    state_->column_widths.push_back({first_column, last_column, width});
}

void WorksheetWriter::freeze_panes(std::uint32_t row_split, std::uint32_t column_split)
{
    ensure_mutable_worksheet(state_);
    if (row_split > max_excel_rows || column_split > max_excel_columns) {
        throw FastXlsxError("invalid freeze pane split");
    }
    state_->frozen_panes = std::make_pair(row_split, column_split);
}

void WorksheetWriter::set_auto_filter(CellRange range)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(range);
    state_->auto_filter = range;
}

void WorksheetWriter::merge_cells(CellRange range)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(range);
    if (range.first_row == range.last_row && range.first_column == range.last_column) {
        throw FastXlsxError("merged range must include more than one cell");
    }
    state_->merged_ranges.push_back(range);
}

void WorksheetWriter::add_conditional_color_scale(CellRange range, TwoColorScaleRule rule)
{
    add_conditional_color_scale(std::span<const CellRange>(&range, 1), rule);
}

void WorksheetWriter::add_conditional_color_scale(CellRange range, ThreeColorScaleRule rule)
{
    add_conditional_color_scale(std::span<const CellRange>(&range, 1), rule);
}

void WorksheetWriter::add_conditional_color_scale(
    std::span<const CellRange> ranges, TwoColorScaleRule rule)
{
    ensure_mutable_worksheet(state_);
    if (ranges.empty()) {
        throw FastXlsxError("conditional color scale range list cannot be empty");
    }
    for (const CellRange& range : ranges) {
        (void)detail::range_reference(range);
    }
    validate_two_color_scale_rule(rule);
    const auto priority = state_->next_conditional_format_priority++;
    state_->conditional_color_scales.push_back(
        {std::vector<CellRange>(ranges.begin(), ranges.end()), rule.lower, std::nullopt, rule.upper, priority});
}

void WorksheetWriter::add_conditional_color_scale(
    std::span<const CellRange> ranges, ThreeColorScaleRule rule)
{
    ensure_mutable_worksheet(state_);
    if (ranges.empty()) {
        throw FastXlsxError("conditional color scale range list cannot be empty");
    }
    for (const CellRange& range : ranges) {
        (void)detail::range_reference(range);
    }
    validate_three_color_scale_rule(rule);
    const auto priority = state_->next_conditional_format_priority++;
    state_->conditional_color_scales.push_back(
        {std::vector<CellRange>(ranges.begin(), ranges.end()), rule.lower, rule.midpoint, rule.upper, priority});
}

void WorksheetWriter::add_conditional_color_scale(
    std::initializer_list<CellRange> ranges, TwoColorScaleRule rule)
{
    add_conditional_color_scale(
        std::span<const CellRange>(ranges.begin(), ranges.size()), rule);
}

void WorksheetWriter::add_conditional_color_scale(
    std::initializer_list<CellRange> ranges, ThreeColorScaleRule rule)
{
    add_conditional_color_scale(
        std::span<const CellRange>(ranges.begin(), ranges.size()), rule);
}

void WorksheetWriter::add_conditional_data_bar(CellRange range, DataBarRule rule)
{
    add_conditional_data_bar(std::span<const CellRange>(&range, 1), rule);
}

void WorksheetWriter::add_conditional_data_bar(std::span<const CellRange> ranges, DataBarRule rule)
{
    ensure_mutable_worksheet(state_);
    if (ranges.empty()) {
        throw FastXlsxError("conditional data bar range list cannot be empty");
    }
    for (const CellRange& range : ranges) {
        (void)detail::range_reference(range);
    }
    validate_data_bar_rule(rule);
    const auto priority = state_->next_conditional_format_priority++;
    state_->conditional_data_bars.push_back(
        {std::vector<CellRange>(ranges.begin(), ranges.end()), rule, priority});
}

void WorksheetWriter::add_conditional_data_bar(
    std::initializer_list<CellRange> ranges, DataBarRule rule)
{
    add_conditional_data_bar(std::span<const CellRange>(ranges.begin(), ranges.size()), rule);
}

void WorksheetWriter::add_conditional_icon_set(CellRange range, IconSetRule rule)
{
    add_conditional_icon_set(std::span<const CellRange>(&range, 1), rule);
}

void WorksheetWriter::add_conditional_icon_set(std::span<const CellRange> ranges, IconSetRule rule)
{
    ensure_mutable_worksheet(state_);
    if (ranges.empty()) {
        throw FastXlsxError("conditional icon set range list cannot be empty");
    }
    for (const CellRange& range : ranges) {
        (void)detail::range_reference(range);
    }
    validate_icon_set_rule(rule);
    const auto priority = state_->next_conditional_format_priority++;
    state_->conditional_icon_sets.push_back(
        {std::vector<CellRange>(ranges.begin(), ranges.end()), rule, priority});
}

void WorksheetWriter::add_conditional_icon_set(
    std::initializer_list<CellRange> ranges, IconSetRule rule)
{
    add_conditional_icon_set(std::span<const CellRange>(ranges.begin(), ranges.size()), rule);
}

void WorksheetWriter::add_data_validation(CellRange range, DataValidationRule rule)
{
    add_data_validation(std::span<const CellRange>(&range, 1), std::move(rule));
}

void WorksheetWriter::add_data_validation(
    std::span<const CellRange> ranges, DataValidationRule rule)
{
    ensure_mutable_worksheet(state_);
    if (ranges.empty()) {
        throw FastXlsxError("data validation range list cannot be empty");
    }
    for (const CellRange& range : ranges) {
        (void)detail::range_reference(range);
    }
    validate_data_validation_rule(rule);
    state_->data_validations.push_back(
        {std::vector<CellRange>(ranges.begin(), ranges.end()), std::move(rule)});
}

void WorksheetWriter::add_data_validation(
    std::initializer_list<CellRange> ranges, DataValidationRule rule)
{
    add_data_validation(std::span<const CellRange>(ranges.begin(), ranges.size()), std::move(rule));
}

void WorksheetWriter::add_external_hyperlink(
    std::uint32_t row, std::uint32_t column, std::string target_url,
    HyperlinkOptions options)
{
    ensure_mutable_worksheet(state_);
    (void)detail::cell_reference(row, column);
    if (target_url.empty()) {
        throw FastXlsxError("external hyperlink target URL cannot be empty");
    }
    state_->external_hyperlinks.push_back(
        {row, column, std::move(target_url), std::move(options)});
}

void WorksheetWriter::add_internal_hyperlink(
    std::uint32_t row, std::uint32_t column, std::string location,
    HyperlinkOptions options)
{
    ensure_mutable_worksheet(state_);
    (void)detail::cell_reference(row, column);
    if (location.empty()) {
        throw FastXlsxError("internal hyperlink location cannot be empty");
    }
    state_->internal_hyperlinks.push_back(
        {row, column, std::move(location), std::move(options)});
}

void WorksheetWriter::add_table(CellRange range, TableOptions options)
{
    ensure_mutable_worksheet(state_);
    validate_table_options(range, options);
    for (const WorksheetTable& existing : state_->tables) {
        if (ranges_overlap(range, existing.range)) {
            throw FastXlsxError("table ranges cannot overlap within a worksheet");
        }
    }
    if (state_->workbook != nullptr && workbook_has_table_name(*state_->workbook, options.name)) {
        throw FastXlsxError("table names must be unique within a workbook");
    }
    state_->tables.push_back({range, std::move(options)});
}

void WorksheetWriter::add_image(const std::filesystem::path& path, CellRange anchor)
{
    add_image(path, anchor, {});
}

void WorksheetWriter::add_image(
    const std::filesystem::path& path, CellRange anchor, ImageOptions options)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(anchor);
    validate_image_options(options);

    const ImageInfo info = read_image_info(path);
    state_->images.push_back({anchor, info, copy_image_to_temp_file(path), std::move(options)});
}

void WorksheetWriter::add_image(std::span<const std::byte> bytes, CellRange anchor)
{
    add_image(bytes, anchor, {});
}

void WorksheetWriter::add_image(
    std::span<const std::byte> bytes, CellRange anchor, ImageOptions options)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(anchor);
    validate_image_options(options);

    const ImageInfo info = read_image_info(bytes);
    state_->images.push_back({anchor, info, copy_image_to_temp_file(bytes), std::move(options)});
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

StyleId WorkbookWriter::add_style(CellStyle style)
{
    if (!state_) {
        throw FastXlsxError("workbook writer is not initialized");
    }
    if (state_->closed) {
        throw FastXlsxError("cannot add styles after close");
    }
    validate_cell_style(style);

    constexpr std::uint32_t first_custom_number_format_id = 164;
    const auto existing = std::find_if(state_->styles.begin(), state_->styles.end(),
        [&style](const RegisteredStyle& registered_style) {
            return equivalent_style(registered_style.style, style);
        });
    if (existing != state_->styles.end()) {
        return StyleId(
            static_cast<std::uint32_t>(std::distance(state_->styles.begin(), existing) + 1),
            reinterpret_cast<std::uintptr_t>(state_.get()));
    }

    if (state_->styles.size()
        >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw FastXlsxError("too many cell styles");
    }

    std::uint32_t number_format_id = 0;
    if (has_number_format(style)) {
        if (const auto existing_number_format_id =
                find_number_format_id(state_->styles, style.number_format)) {
            number_format_id = *existing_number_format_id;
        } else {
            const std::size_t number_format_count = custom_number_format_count(state_->styles);
            if (number_format_count
                > static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max() - first_custom_number_format_id)) {
                throw FastXlsxError("too many custom number formats");
            }
            number_format_id =
                first_custom_number_format_id + static_cast<std::uint32_t>(number_format_count);
        }
    }

    std::uint32_t font_id = 0;
    if (has_font_property(style)) {
        if (const auto existing_font_id = find_font_id(state_->styles, *style.font)) {
            font_id = *existing_font_id;
        } else {
            const std::size_t font_count = custom_font_count(state_->styles);
            if (font_count
                >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                throw FastXlsxError("too many custom fonts");
            }
            font_id = static_cast<std::uint32_t>(font_count + 1);
        }
    }

    std::uint32_t fill_id = 0;
    if (has_fill_property(style)) {
        if (const auto existing_fill_id = find_fill_id(state_->styles, *style.fill)) {
            fill_id = *existing_fill_id;
        } else {
            const std::size_t fill_count = custom_fill_count(state_->styles);
            if (fill_count
                > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 2)) {
                throw FastXlsxError("too many custom fills");
            }
            fill_id = static_cast<std::uint32_t>(fill_count + 2);
        }
    }

    state_->styles.push_back({std::move(style), number_format_id, font_id, fill_id});
    return StyleId(static_cast<std::uint32_t>(state_->styles.size()),
        reinterpret_cast<std::uintptr_t>(state_.get()));
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

    state_->worksheets.push_back(
        std::make_unique<detail::WorksheetWriterState>(std::move(name), state_.get()));
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
    std::vector<detail::PackageEntry> entries;
    const bool write_shared_strings =
        uses_shared_strings(*state_) && state_->shared_strings.count > 0;
    const bool write_styles = writes_styles(*state_);
    detail::PackageManifest manifest =
        detail::make_minimal_workbook_manifest(
            state_->worksheets.size(), write_shared_strings, true, write_styles);
    const std::size_t table_count = count_tables(state_->worksheets);
    const std::size_t image_count = count_images(state_->worksheets);
    const std::size_t drawing_count = count_drawings(state_->worksheets);
    constexpr std::string_view content_type_table =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml";
    constexpr std::string_view content_type_drawing =
        "application/vnd.openxmlformats-officedocument.drawing+xml";
    if (workbook_has_image_format(state_->worksheets, ImageFormat::Png)) {
        manifest.content_types().add_default("png", std::string(image_content_type(ImageFormat::Png)));
    }
    if (workbook_has_image_format(state_->worksheets, ImageFormat::Jpeg)) {
        manifest.content_types().add_default("jpg", std::string(image_content_type(ImageFormat::Jpeg)));
    }
    for (std::size_t index = 0; index < table_count; ++index) {
        manifest.add_part(
            detail::PartName("/" + table_entry_name(index)), std::string(content_type_table))
            .set_write_mode(detail::PartWriteMode::GenerateSmallXml);
    }
    for (std::size_t index = 0; index < drawing_count; ++index) {
        manifest.add_part(
            detail::PartName("/" + drawing_entry_name(index)), std::string(content_type_drawing))
            .set_write_mode(detail::PartWriteMode::GenerateSmallXml);
    }

    const auto* workbook_relationships =
        manifest.relationships_for(detail::PartName("/xl/workbook.xml"));
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook relationships missing from package manifest");
    }

    const auto worksheet_relationship_count =
        std::count_if(state_->worksheets.begin(), state_->worksheets.end(),
            [](const auto& worksheet) { return worksheet_has_relationships(*worksheet); });
    entries.reserve(6 + state_->worksheets.size() + worksheet_relationship_count
        + table_count + drawing_count * 2 + image_count + (write_shared_strings ? 1 : 0)
        + (write_styles ? 1 : 0));
    entries.push_back({"[Content_Types].xml", detail::serialize_content_types(manifest.content_types())});
    entries.push_back({"_rels/.rels", detail::serialize_relationships(manifest.package_relationships())});
    entries.push_back(
        {"docProps/core.xml", detail::build_core_properties(state_->options.document_properties)});
    entries.push_back(
        {"docProps/app.xml", detail::build_extended_properties(state_->options.document_properties)});
    const bool full_calc_on_load = std::any_of(state_->worksheets.begin(), state_->worksheets.end(),
        [](const auto& worksheet) { return worksheet->has_formula; });
    entries.push_back({"xl/workbook.xml", build_workbook(state_->worksheets, full_calc_on_load)});
    entries.push_back(
        {"xl/_rels/workbook.xml.rels", detail::serialize_relationships(*workbook_relationships)});

    std::optional<TemporaryFile> shared_strings_file;
    if (write_shared_strings) {
        shared_strings_file.emplace();
        write_shared_strings_file(shared_strings_file->path(), state_->shared_strings);
        entries.emplace_back("xl/sharedStrings.xml",
            std::vector<detail::PackageEntryChunk> {
                detail::PackageEntryChunk::file(shared_strings_file->path())});
    }
    if (write_styles) {
        entries.emplace_back("xl/styles.xml", build_styles_xml(state_->styles));
    }

    const std::vector<std::size_t> first_table_indexes = table_start_indexes(state_->worksheets);
    const std::vector<std::size_t> first_image_indexes = image_start_indexes(state_->worksheets);
    const std::vector<std::size_t> drawing_indexes = worksheet_drawing_indexes(state_->worksheets);
    for (std::size_t index = 0; index < state_->worksheets.size(); ++index) {
        entries.push_back(build_worksheet_entry(
            "xl/worksheets/sheet" + std::to_string(index + 1) + ".xml",
            *state_->worksheets[index]));
        if (worksheet_has_relationships(*state_->worksheets[index])) {
            entries.emplace_back(worksheet_relationship_entry_name(index),
                build_worksheet_relationships(*state_->worksheets[index], first_table_indexes[index],
                    drawing_indexes[index]));
        }
        if (!state_->worksheets[index]->images.empty()) {
            const std::size_t drawing_index = drawing_indexes[index];
            entries.emplace_back(drawing_entry_name(drawing_index),
                build_drawing_xml(*state_->worksheets[index], first_image_indexes[index]));
            entries.emplace_back(drawing_relationship_entry_name(drawing_index),
                build_drawing_relationships(*state_->worksheets[index], first_image_indexes[index]));
            for (std::size_t image_index = 0; image_index < state_->worksheets[index]->images.size();
                ++image_index) {
                const WorksheetImage& image = state_->worksheets[index]->images[image_index];
                entries.emplace_back(media_entry_name(image, first_image_indexes[index] + image_index),
                    std::vector<detail::PackageEntryChunk> {
                        detail::PackageEntryChunk::file(image.media_path)});
            }
        }
        for (std::size_t table_index = 0; table_index < state_->worksheets[index]->tables.size();
            ++table_index) {
            const std::size_t package_table_index = first_table_indexes[index] + table_index;
            entries.emplace_back(table_entry_name(package_table_index),
                build_table_xml(state_->worksheets[index]->tables[table_index], package_table_index));
        }
    }

    detail::write_package(state_->output_path, entries);
    state_->closed = true;
}

} // namespace fastxlsx
