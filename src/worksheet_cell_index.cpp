#include <fastxlsx/detail/worksheet_cell_index.hpp>

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;
constexpr std::size_t default_materialized_index_chunk_size = 64U * 1024U;

bool is_ascii_alpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

std::uint32_t uppercase_column_value(char ch)
{
    const char upper = (ch >= 'a' && ch <= 'z')
        ? static_cast<char>(ch - ('a' - 'A'))
        : ch;
    return static_cast<std::uint32_t>(upper - 'A' + 1);
}

struct WorksheetCellCoordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

bool coordinate_less(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

bool coordinate_less(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    WorksheetCellCoordinate right) noexcept
{
    if (left.row != right.row) {
        return left.row < right.row;
    }
    return left.column < right.column;
}

bool coordinate_equal(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& right) noexcept
{
    return left.row == right.row && left.column == right.column;
}

bool coordinate_equal(
    const fastxlsx::detail::WorksheetCellIndex::CellEntry& left,
    WorksheetCellCoordinate right) noexcept
{
    return left.row == right.row && left.column == right.column;
}

std::optional<WorksheetCellCoordinate> parse_cell_reference_for_lookup(
    std::string_view reference) noexcept
{
    if (reference.empty()) {
        return std::nullopt;
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > max_excel_columns) {
            return std::nullopt;
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        return std::nullopt;
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        if (row > static_cast<std::uint64_t>(max_excel_rows) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > max_excel_rows) {
            return std::nullopt;
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        return std::nullopt;
    }

    return WorksheetCellCoordinate {
        static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column),
    };
}

WorksheetCellCoordinate parse_source_cell_reference(std::string_view reference)
{
    if (reference.empty()) {
        throw fastxlsx::FastXlsxError("worksheet cell index requires cell r attributes");
    }

    std::uint64_t column = 0;
    std::size_t position = 0;
    while (position < reference.size() && is_ascii_alpha(reference[position])) {
        column = column * 26U + uppercase_column_value(reference[position]);
        if (column > max_excel_columns) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell column exceeds Excel limits");
        }
        ++position;
    }
    if (position == 0 || position >= reference.size()) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index found an invalid source cell reference");
    }

    std::uint64_t row = 0;
    while (position < reference.size() && is_ascii_digit(reference[position])) {
        if (row > static_cast<std::uint64_t>(max_excel_rows) / 10U) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell row exceeds Excel limits");
        }
        row = row * 10U + static_cast<std::uint32_t>(reference[position] - '0');
        if (row > max_excel_rows) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index source cell row exceeds Excel limits");
        }
        ++position;
    }
    if (position != reference.size() || row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index found an invalid source cell reference");
    }

    return WorksheetCellCoordinate {
        static_cast<std::uint32_t>(row),
        static_cast<std::uint32_t>(column),
    };
}

std::string cell_reference_from_coordinate(WorksheetCellCoordinate coordinate)
{
    std::string column_name;
    std::uint32_t column = coordinate.column;
    while (column > 0) {
        --column;
        column_name.push_back(
            static_cast<char>('A' + static_cast<char>(column % 26U)));
        column /= 26U;
    }
    std::reverse(column_name.begin(), column_name.end());
    return column_name + std::to_string(coordinate.row);
}

std::uint64_t event_end_offset(const fastxlsx::detail::WorksheetEvent& event)
{
    if (static_cast<std::uint64_t>(event.raw_xml.size())
        > std::numeric_limits<std::uint64_t>::max() - event.raw_xml_offset) {
        throw fastxlsx::FastXlsxError("worksheet cell index source offset overflow");
    }
    return event.raw_xml_offset + static_cast<std::uint64_t>(event.raw_xml.size());
}

std::uint64_t checked_range_end_offset(std::uint64_t start, std::size_t size)
{
    if (static_cast<std::uint64_t>(size)
        > std::numeric_limits<std::uint64_t>::max() - start) {
        throw fastxlsx::FastXlsxError("worksheet cell index source offset overflow");
    }
    return start + static_cast<std::uint64_t>(size);
}

bool starts_with_at(std::string_view text, std::size_t position, std::string_view prefix)
{
    return position <= text.size() && text.substr(position, prefix.size()) == prefix;
}

bool xml_is_space(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

bool is_xml_declaration_tag(std::string_view raw)
{
    if (!starts_with_at(raw, 0, "<?xml")) {
        return false;
    }
    if (raw.size() <= 5) {
        return false;
    }
    const char after_target = raw[5];
    return xml_is_space(after_target) || after_target == '?';
}

std::string_view local_xml_name(std::string_view name)
{
    const std::size_t colon = name.find(':');
    if (colon == std::string_view::npos) {
        return name;
    }
    return name.substr(colon + 1);
}

std::size_t find_xml_markup_end_or_npos(std::string_view xml, std::size_t open)
{
    char quote = '\0';
    for (std::size_t index = open + 1; index < xml.size(); ++index) {
        const char ch = xml[index];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '>') {
            return index;
        }
    }
    return std::string_view::npos;
}

std::size_t xml_tag_name_begin(std::string_view raw_tag)
{
    std::size_t position = 1;
    if (position < raw_tag.size() && raw_tag[position] == '/') {
        ++position;
    }
    const std::size_t limit = raw_tag.empty() ? 0 : raw_tag.size() - 1;
    while (position < limit && xml_is_space(raw_tag[position])) {
        ++position;
    }
    return position;
}

std::size_t xml_tag_name_end(std::string_view raw_tag, std::size_t position)
{
    const std::size_t limit = raw_tag.empty() ? 0 : raw_tag.size() - 1;
    while (position < limit && !xml_is_space(raw_tag[position])
        && raw_tag[position] != '/' && raw_tag[position] != '?'
        && raw_tag[position] != '>') {
        ++position;
    }
    return position;
}

std::string_view xml_element_name(std::string_view raw_tag)
{
    const std::size_t begin = xml_tag_name_begin(raw_tag);
    const std::size_t end = xml_tag_name_end(raw_tag, begin);
    if (begin == end) {
        throw fastxlsx::FastXlsxError("worksheet cell index found an empty XML tag");
    }
    return local_xml_name(raw_tag.substr(begin, end - begin));
}

bool is_xml_closing_tag(std::string_view raw_tag)
{
    return raw_tag.size() > 2 && raw_tag[1] == '/';
}

bool is_xml_self_closing_tag(std::string_view raw_tag)
{
    if (is_xml_closing_tag(raw_tag)) {
        return false;
    }
    std::size_t index = raw_tag.size() - 2;
    while (index > 0 && xml_is_space(raw_tag[index])) {
        --index;
    }
    return raw_tag[index] == '/';
}

std::string_view unqualified_xml_attribute_value(
    std::string_view raw_tag, std::string_view attribute_name)
{
    std::size_t position =
        xml_tag_name_end(raw_tag, xml_tag_name_begin(raw_tag));
    const std::size_t limit = raw_tag.empty() ? 0 : raw_tag.size() - 1;

    while (position < limit) {
        while (position < limit && xml_is_space(raw_tag[position])) {
            ++position;
        }
        if (position >= limit || raw_tag[position] == '/' || raw_tag[position] == '?') {
            return {};
        }

        const std::size_t name_begin = position;
        while (position < limit && !xml_is_space(raw_tag[position])
            && raw_tag[position] != '=' && raw_tag[position] != '/'
            && raw_tag[position] != '?' && raw_tag[position] != '>') {
            ++position;
        }
        const std::string_view name =
            raw_tag.substr(name_begin, position - name_begin);

        while (position < limit && xml_is_space(raw_tag[position])) {
            ++position;
        }
        if (position >= limit || raw_tag[position] != '=') {
            continue;
        }
        ++position;
        while (position < limit && xml_is_space(raw_tag[position])) {
            ++position;
        }
        if (position >= limit || (raw_tag[position] != '"' && raw_tag[position] != '\'')) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found an unquoted attribute value");
        }

        const char quote = raw_tag[position];
        ++position;
        const std::size_t value_begin = position;
        while (position < limit && raw_tag[position] != quote) {
            ++position;
        }
        if (position >= limit) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found an unterminated attribute value");
        }

        if (name == attribute_name) {
            return raw_tag.substr(value_begin, position - value_begin);
        }
        ++position;
    }

    return {};
}

bool xml_has_non_whitespace(std::string_view value)
{
    for (const char ch : value) {
        if (!xml_is_space(ch)) {
            return true;
        }
    }
    return false;
}

char cell_value_element_code(std::string_view name) noexcept
{
    if (name == "v") {
        return 'v';
    }
    if (name == "t") {
        return 't';
    }
    if (name == "f") {
        return 'f';
    }
    return '\0';
}

struct ActiveCellRange {
    std::string reference;
    std::uint64_t start_offset = 0;
};

using TargetCoordinateKey = std::uint64_t;

TargetCoordinateKey target_coordinate_key(WorksheetCellCoordinate coordinate) noexcept
{
    return (static_cast<std::uint64_t>(coordinate.row) << 32U)
        | static_cast<std::uint64_t>(coordinate.column);
}

struct TargetCoordinateBucket {
    TargetCoordinateKey key = 0;
    std::vector<std::size_t> target_indices;
};

void sort_and_validate_rewrite_ranges(
    std::vector<fastxlsx::detail::WorksheetIndexedCellRewrite>& plan)
{
    std::sort(plan.begin(), plan.end(), [](const auto& left, const auto& right) {
        if (left.source_range.start_offset != right.source_range.start_offset) {
            return left.source_range.start_offset < right.source_range.start_offset;
        }
        return left.source_range.end_offset < right.source_range.end_offset;
    });

    for (std::size_t index_position = 1; index_position < plan.size(); ++index_position) {
        if (plan[index_position - 1].source_range.end_offset
            > plan[index_position].source_range.start_offset) {
            throw fastxlsx::FastXlsxError(
                "worksheet indexed rewrite source cell ranges overlap");
        }
    }
}

struct TargetedCellReference {
    std::string requested_reference;
    std::optional<WorksheetCellCoordinate> coordinate;
    bool found = false;
    fastxlsx::detail::WorksheetCellIndexedRange range;
};

struct ActiveTargetedCellRange {
    WorksheetCellCoordinate coordinate;
    std::uint64_t start_offset = 0;
};

class TargetedRewritePlanComplete {
};

class WorksheetCellIndexBuilder {
public:
    void consume(const fastxlsx::detail::WorksheetEvent& event)
    {
        using fastxlsx::detail::WorksheetEventKind;

        if (event.kind == WorksheetEventKind::CellStart) {
            consume_cell_start(event);
            return;
        }
        if (event.kind == WorksheetEventKind::CellEnd) {
            consume_cell_end(event);
        }
    }

    [[nodiscard]] fastxlsx::detail::WorksheetCellIndex finish() &&
    {
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError("worksheet cell index ended inside a source cell");
        }
        index_.finalize();
        return std::move(index_);
    }

private:
    void consume_cell_start(const fastxlsx::detail::WorksheetEvent& event)
    {
        if (event.self_closing) {
            index_.add_cell(event.cell_reference,
                fastxlsx::detail::WorksheetCellIndexedRange {
                    event.raw_xml_offset,
                    event_end_offset(event),
                });
            return;
        }
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a nested source cell");
        }
        active_cell_ = ActiveCellRange {
            std::string(event.cell_reference),
            event.raw_xml_offset,
        };
    }

    void consume_cell_end(const fastxlsx::detail::WorksheetEvent& event)
    {
        if (event.self_closing) {
            return;
        }
        if (!active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a closing source cell without a start");
        }
        index_.add_cell(active_cell_->reference,
            fastxlsx::detail::WorksheetCellIndexedRange {
                active_cell_->start_offset,
                event_end_offset(event),
            });
        active_cell_.reset();
    }

    fastxlsx::detail::WorksheetCellIndex index_;
    std::optional<ActiveCellRange> active_cell_;
};

class TargetedWorksheetCellRewritePlanner {
public:
    explicit TargetedWorksheetCellRewritePlanner(
        std::span<const std::string_view> cell_references,
        bool stop_after_all_targets_found)
        : stop_after_all_targets_found_(stop_after_all_targets_found)
    {
        add_targets(cell_references);
    }

    void consume(const fastxlsx::detail::WorksheetEvent& event)
    {
        using fastxlsx::detail::WorksheetEventKind;

        if (event.kind == WorksheetEventKind::Metadata
            && event.element_name == "dimension") {
            observe_dimension_metadata();
            return;
        }
        if (event.kind == WorksheetEventKind::CellStart) {
            consume_cell_start(event.cell_reference,
                event.raw_xml_offset,
                event.raw_xml.size(),
                event.self_closing);
            return;
        }
        if (event.kind == WorksheetEventKind::CellEnd) {
            consume_cell_end(event.raw_xml_offset, event.raw_xml.size(), event.self_closing);
        }
    }

    void observe_dimension_metadata() noexcept
    {
        source_has_top_level_dimension_ = true;
    }

    void consume_cell_start(std::string_view cell_reference,
        std::uint64_t raw_xml_offset,
        std::size_t raw_xml_size,
        bool self_closing)
    {
        const WorksheetCellCoordinate coordinate =
            parse_source_cell_reference(cell_reference);
        ++scanned_source_cell_count_;

        if (self_closing) {
            if (record_source_range(coordinate,
                fastxlsx::detail::WorksheetCellIndexedRange {
                    raw_xml_offset,
                    checked_range_end_offset(raw_xml_offset, raw_xml_size),
                })) {
                throw TargetedRewritePlanComplete {};
            }
            return;
        }
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a nested source cell");
        }
        active_cell_ = ActiveTargetedCellRange {
            coordinate,
            raw_xml_offset,
        };
    }

    void consume_cell_end(
        std::uint64_t raw_xml_offset, std::size_t raw_xml_size, bool self_closing)
    {
        if (self_closing) {
            return;
        }
        if (!active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found a closing source cell without a start");
        }
        const bool all_targets_found = record_source_range(active_cell_->coordinate,
            fastxlsx::detail::WorksheetCellIndexedRange {
                active_cell_->start_offset,
                checked_range_end_offset(raw_xml_offset, raw_xml_size),
            });
        active_cell_.reset();
        if (all_targets_found) {
            throw TargetedRewritePlanComplete {};
        }
    }

    [[nodiscard]] fastxlsx::detail::WorksheetTargetedCellRewritePlan finish() &&
    {
        if (active_cell_.has_value()) {
            throw fastxlsx::FastXlsxError("worksheet cell index ended inside a source cell");
        }

        fastxlsx::detail::WorksheetTargetedCellRewritePlan plan;
        plan.scanned_source_cell_count = scanned_source_cell_count_;
        plan.source_has_top_level_dimension = source_has_top_level_dimension_;
        plan.rewrites.reserve(targets_.size());
        for (const TargetedCellReference& target : targets_) {
            if (!target.found) {
                throw fastxlsx::FastXlsxError(
                    "worksheet indexed rewrite target cell is missing from source index; not found: "
                    + target.requested_reference);
            }
            plan.rewrites.push_back(fastxlsx::detail::WorksheetIndexedCellRewrite {
                target.requested_reference,
                target.range,
            });
        }
        sort_and_validate_rewrite_ranges(plan.rewrites);
        return plan;
    }

private:
    void add_targets(std::span<const std::string_view> cell_references)
    {
        std::set<std::string, std::less<>> seen_targets;
        targets_.reserve(cell_references.size());
        targets_by_coordinate_.reserve(cell_references.size());

        for (std::string_view cell_reference : cell_references) {
            if (cell_reference.empty()) {
                throw fastxlsx::FastXlsxError(
                    "worksheet indexed rewrite target cell reference is empty");
            }

            auto [_, inserted] = seen_targets.emplace(cell_reference);
            if (!inserted) {
                throw fastxlsx::FastXlsxError(
                    "worksheet indexed rewrite target cell reference is duplicated: "
                    + std::string(cell_reference));
            }

            TargetedCellReference target;
            target.requested_reference = std::string(cell_reference);
            target.coordinate = parse_cell_reference_for_lookup(cell_reference);

            const std::size_t target_index = targets_.size();
            targets_.push_back(std::move(target));
            if (targets_.back().coordinate.has_value()) {
                TargetCoordinateBucket bucket;
                bucket.key = target_coordinate_key(*targets_.back().coordinate);
                bucket.target_indices.push_back(target_index);
                targets_by_coordinate_.push_back(std::move(bucket));
            }
        }
        sort_and_merge_target_buckets();
    }

    [[nodiscard]] bool record_source_range(
        WorksheetCellCoordinate coordinate,
        fastxlsx::detail::WorksheetCellIndexedRange range)
    {
        if (range.end_offset < range.start_offset) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found an invalid source cell range");
        }

        const TargetCoordinateKey key = target_coordinate_key(coordinate);
        const TargetCoordinateBucket* targets = find_target_bucket(key);
        if (targets == nullptr) {
            return false;
        }

        for (const std::size_t target_index : targets->target_indices) {
            TargetedCellReference& target = targets_[target_index];
            if (target.found) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found duplicate source cell reference: "
                    + cell_reference_from_coordinate(coordinate));
            }
            target.found = true;
            target.range = range;
            ++found_target_count_;
            if (stop_after_all_targets_found_
                && found_target_count_ == targets_.size()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const TargetCoordinateBucket* find_target_bucket(
        TargetCoordinateKey key)
    {
        if (source_coordinates_monotonic_) {
            if (have_last_source_coordinate_ && key < last_source_coordinate_) {
                source_coordinates_monotonic_ = false;
            } else {
                have_last_source_coordinate_ = true;
                last_source_coordinate_ = key;
                while (next_target_bucket_index_ < targets_by_coordinate_.size()
                    && targets_by_coordinate_[next_target_bucket_index_].key < key) {
                    ++next_target_bucket_index_;
                }
                if (next_target_bucket_index_ >= targets_by_coordinate_.size()
                    || targets_by_coordinate_[next_target_bucket_index_].key != key) {
                    return nullptr;
                }
                return &targets_by_coordinate_[next_target_bucket_index_];
            }
        }

        const auto targets = std::lower_bound(targets_by_coordinate_.begin(),
            targets_by_coordinate_.end(), key,
            [](const TargetCoordinateBucket& bucket, TargetCoordinateKey target_key) {
                return bucket.key < target_key;
            });
        if (targets == targets_by_coordinate_.end() || targets->key != key) {
            return nullptr;
        }
        return &(*targets);
    }

    void sort_and_merge_target_buckets()
    {
        std::sort(targets_by_coordinate_.begin(), targets_by_coordinate_.end(),
            [](const TargetCoordinateBucket& left, const TargetCoordinateBucket& right) {
                return left.key < right.key;
            });

        std::vector<TargetCoordinateBucket> merged;
        merged.reserve(targets_by_coordinate_.size());
        for (TargetCoordinateBucket& bucket : targets_by_coordinate_) {
            if (!merged.empty() && merged.back().key == bucket.key) {
                merged.back().target_indices.insert(
                    merged.back().target_indices.end(),
                    bucket.target_indices.begin(),
                    bucket.target_indices.end());
                continue;
            }
            merged.push_back(std::move(bucket));
        }
        targets_by_coordinate_ = std::move(merged);
    }

    std::vector<TargetedCellReference> targets_;
    std::vector<TargetCoordinateBucket> targets_by_coordinate_;
    std::optional<ActiveTargetedCellRange> active_cell_;
    std::uint64_t scanned_source_cell_count_ = 0;
    std::size_t found_target_count_ = 0;
    std::size_t next_target_bucket_index_ = 0;
    TargetCoordinateKey last_source_coordinate_ = 0;
    bool source_has_top_level_dimension_ = false;
    bool stop_after_all_targets_found_ = false;
    bool source_coordinates_monotonic_ = true;
    bool have_last_source_coordinate_ = false;
};

class TargetedWorksheetCellFastScanner {
public:
    explicit TargetedWorksheetCellFastScanner(TargetedWorksheetCellRewritePlanner& planner)
        : planner_(planner)
    {
    }

    void emit_comment(std::string_view raw, std::uint64_t offset) const
    {
        (void)raw;
        (void)offset;
        reject_markup_after_root();
    }

    void emit_processing_instruction(std::string_view raw, std::uint64_t offset) const
    {
        (void)offset;
        reject_markup_after_root();
        if (is_xml_declaration_tag(raw) && seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found XML declaration after worksheet root");
        }
    }

    void emit_unsupported(std::string_view raw, std::uint64_t offset) const
    {
        (void)raw;
        (void)offset;
        reject_markup_outside_root();
    }

    void emit_text(std::string_view text, std::uint64_t offset) const
    {
        (void)offset;
        if (text.empty()) {
            return;
        }
        if (!seen_worksheet_start_ && xml_has_non_whitespace(text)) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found text before worksheet root");
        }
        if (seen_worksheet_end_ && xml_has_non_whitespace(text)) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found text after worksheet root");
        }
    }

    void emit_tag(std::string_view raw, std::uint64_t offset)
    {
        const std::string_view name = xml_element_name(raw);
        const bool closing = is_xml_closing_tag(raw);
        const bool self_closing = is_xml_self_closing_tag(raw);
        if (name != "worksheet") {
            reject_markup_outside_root();
        } else {
            reject_markup_after_root();
        }

        if (closing) {
            emit_closing_tag(name, offset, raw.size());
            return;
        }
        emit_opening_tag(raw, name, self_closing, offset);
    }

    void finish() const
    {
        if (!seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index requires a worksheet root");
        }
        if (!seen_worksheet_end_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index requires a closing worksheet root");
        }
        if (in_sheet_data_ || in_row_ || in_cell_ || in_cell_value_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index ended inside an open worksheet element");
        }
    }

private:
    void reject_markup_after_root() const
    {
        if (seen_worksheet_end_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found markup after worksheet root");
        }
    }

    void reject_markup_outside_root() const
    {
        if (!seen_worksheet_start_) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found markup before worksheet root");
        }
        reject_markup_after_root();
    }

    void clear_current_cell_value()
    {
        in_cell_value_ = false;
        current_cell_value_element_ = '\0';
    }

    void emit_closing_tag(
        std::string_view name, std::uint64_t offset, std::size_t raw_size)
    {
        const char value_element = cell_value_element_code(name);
        if (value_element != '\0' && in_cell_) {
            if (!in_cell_value_ || current_cell_value_element_ != value_element) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found a mismatched cell value boundary");
            }
            clear_current_cell_value();
            return;
        }

        if (name == "c") {
            if (!in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found a closing cell without a start");
            }
            if (in_cell_value_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found a cell boundary inside an open cell value");
            }
            planner_.consume_cell_end(offset, raw_size, false);
            in_cell_ = false;
            return;
        }

        if (name == "row") {
            if (!in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid row boundary");
            }
            in_row_ = false;
            return;
        }

        if (name == "sheetData") {
            if (!in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid sheetData boundary");
            }
            in_sheet_data_ = false;
            return;
        }

        if (name == "worksheet") {
            if (in_sheet_data_ || in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid worksheet boundary");
            }
            seen_worksheet_end_ = true;
            return;
        }

        if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_
            && name == "dimension") {
            planner_.observe_dimension_metadata();
        }
    }

    void emit_opening_tag(
        std::string_view raw,
        std::string_view name,
        bool self_closing,
        std::uint64_t offset)
    {
        if (name == "worksheet") {
            if (seen_worksheet_start_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found duplicate worksheet root");
            }
            seen_worksheet_start_ = true;
            if (self_closing) {
                seen_worksheet_end_ = true;
            }
            return;
        }

        if (name == "sheetData") {
            if (seen_sheet_data_ || in_sheet_data_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid sheetData boundary");
            }
            seen_sheet_data_ = true;
            in_sheet_data_ = true;
            if (self_closing) {
                in_sheet_data_ = false;
            }
            return;
        }

        if (name == "row") {
            if (!in_sheet_data_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found row outside sheetData");
            }
            if (in_row_ || in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid row boundary");
            }
            if (!self_closing) {
                in_row_ = true;
            }
            return;
        }

        if (name == "c") {
            if (!in_row_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found cell outside row");
            }
            if (in_cell_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an invalid cell boundary");
            }

            const std::string_view cell_reference =
                unqualified_xml_attribute_value(raw, "r");
            planner_.consume_cell_start(
                cell_reference, offset, raw.size(), self_closing);
            if (!self_closing) {
                in_cell_ = true;
            }
            return;
        }

        const char value_element = cell_value_element_code(name);
        if (value_element != '\0' && in_cell_) {
            if (in_cell_value_) {
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found nested cell value markup");
            }
            if (!self_closing) {
                in_cell_value_ = true;
                current_cell_value_element_ = value_element;
            }
            return;
        }

        if (seen_worksheet_start_ && !seen_worksheet_end_ && !in_sheet_data_
            && name == "dimension") {
            planner_.observe_dimension_metadata();
        }
    }

    TargetedWorksheetCellRewritePlanner& planner_;
    bool seen_worksheet_start_ = false;
    bool seen_worksheet_end_ = false;
    bool seen_sheet_data_ = false;
    bool in_sheet_data_ = false;
    bool in_row_ = false;
    bool in_cell_ = false;
    bool in_cell_value_ = false;
    char current_cell_value_element_ = '\0';
};

std::uint64_t add_targeted_source_offset(std::uint64_t base, std::size_t relative)
{
    return checked_range_end_offset(base, relative);
}

std::size_t consume_targeted_fast_scan_events(std::string_view xml_window,
    bool final_chunk,
    TargetedWorksheetCellFastScanner& scanner,
    std::uint64_t window_begin_offset)
{
    std::size_t position = 0;
    while (position < xml_window.size()) {
        const std::size_t open = xml_window.find('<', position);
        if (open == std::string_view::npos) {
            if (final_chunk) {
                scanner.emit_text(xml_window.substr(position),
                    add_targeted_source_offset(window_begin_offset, position));
                return xml_window.size();
            }
            return position;
        }

        if (open > position) {
            scanner.emit_text(xml_window.substr(position, open - position),
                add_targeted_source_offset(window_begin_offset, position));
            position = open;
        }

        if (starts_with_at(xml_window, position, "<!--")) {
            const std::size_t end = xml_window.find("-->", position + 4);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an unterminated XML comment");
            }
            scanner.emit_comment(
                xml_window.substr(position, end + 3 - position),
                add_targeted_source_offset(window_begin_offset, position));
            position = end + 3;
            continue;
        }

        if (starts_with_at(xml_window, position, "<?")) {
            const std::size_t end = xml_window.find("?>", position + 2);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found an unterminated processing instruction");
            }
            scanner.emit_processing_instruction(
                xml_window.substr(position, end + 2 - position),
                add_targeted_source_offset(window_begin_offset, position));
            position = end + 2;
            continue;
        }

        if (starts_with_at(xml_window, position, "<!")) {
            const std::size_t end = find_xml_markup_end_or_npos(xml_window, position);
            if (end == std::string_view::npos) {
                if (!final_chunk) {
                    return position;
                }
                throw fastxlsx::FastXlsxError(
                    "worksheet cell index found unterminated markup");
            }
            scanner.emit_unsupported(
                xml_window.substr(position, end + 1 - position),
                add_targeted_source_offset(window_begin_offset, position));
            position = end + 1;
            continue;
        }

        const std::size_t close = find_xml_markup_end_or_npos(xml_window, position);
        if (close == std::string_view::npos) {
            if (!final_chunk) {
                return position;
            }
            throw fastxlsx::FastXlsxError(
                "worksheet cell index found unterminated markup");
        }
        scanner.emit_tag(
            xml_window.substr(position, close + 1 - position),
            add_targeted_source_offset(window_begin_offset, position));
        position = close + 1;
    }

    return position;
}

void erase_targeted_consumed_prefix(std::string& window, std::size_t consumed)
{
    if (consumed == window.size()) {
        window.clear();
        return;
    }
    if (consumed > 0) {
        window.erase(0, consumed);
    }
}

void process_targeted_fast_scan_window(std::string& window,
    bool final_chunk,
    TargetedWorksheetCellFastScanner& scanner,
    std::uint64_t& window_begin_offset)
{
    const std::size_t consumed = consume_targeted_fast_scan_events(
        window, final_chunk, scanner, window_begin_offset);
    erase_targeted_consumed_prefix(window, consumed);
    window_begin_offset = add_targeted_source_offset(window_begin_offset, consumed);
}

void process_targeted_fast_scan_chunk(std::string_view chunk,
    fastxlsx::detail::WorksheetEventReaderOptions options,
    std::string& window,
    TargetedWorksheetCellFastScanner& scanner,
    std::uint64_t& window_begin_offset)
{
    std::size_t chunk_offset = 0;
    while (!window.empty() && chunk_offset < chunk.size()) {
        if (window.size() >= options.max_window_bytes) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index exceeded bounded input window");
        }

        const std::size_t available = options.max_window_bytes - window.size();
        const std::size_t remaining = chunk.size() - chunk_offset;
        const std::size_t bytes_to_append = std::min(available, remaining);
        window.append(chunk.data() + chunk_offset, bytes_to_append);
        chunk_offset += bytes_to_append;
        process_targeted_fast_scan_window(window, false, scanner, window_begin_offset);

        if (bytes_to_append == 0 && !window.empty()) {
            throw fastxlsx::FastXlsxError(
                "worksheet cell index exceeded bounded input window");
        }
    }

    if (chunk_offset >= chunk.size()) {
        return;
    }

    const std::string_view remaining = chunk.substr(chunk_offset);
    const std::size_t consumed = consume_targeted_fast_scan_events(
        remaining, false, scanner, window_begin_offset);
    window_begin_offset = add_targeted_source_offset(window_begin_offset, consumed);
    if (consumed == remaining.size()) {
        return;
    }

    const std::string_view unconsumed = remaining.substr(consumed);
    if (unconsumed.size() > options.max_window_bytes) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index exceeded bounded input window");
    }
    window.assign(unconsumed.data(), unconsumed.size());
}

void scan_targeted_rewrite_plan_from_chunk_source(
    const fastxlsx::detail::WorksheetInputChunkCallback& read_next_chunk,
    TargetedWorksheetCellRewritePlanner& planner,
    fastxlsx::detail::WorksheetEventReaderOptions options)
{
    if (!read_next_chunk) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index requires a chunk source");
    }
    if (options.max_window_bytes == 0) {
        throw fastxlsx::FastXlsxError(
            "worksheet cell index requires a nonzero input window limit");
    }

    TargetedWorksheetCellFastScanner scanner(planner);
    std::string window;
    window.reserve(std::min<std::size_t>(options.max_window_bytes, 4096U));
    std::uint64_t window_begin_offset = 0;

    std::string chunk;
    while (read_next_chunk(chunk)) {
        process_targeted_fast_scan_chunk(
            chunk, options, window, scanner, window_begin_offset);
    }

    process_targeted_fast_scan_window(window, true, scanner, window_begin_offset);
    scanner.finish();
}

std::size_t materialized_index_chunk_size(
    fastxlsx::detail::WorksheetEventReaderOptions options)
{
    if (options.max_window_bytes == 0) {
        return 1;
    }
    return std::max<std::size_t>(
        1U, std::min(default_materialized_index_chunk_size, options.max_window_bytes));
}

} // namespace

namespace fastxlsx::detail {

WorksheetCellIndex WorksheetCellIndex::build_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    WorksheetEventReaderOptions options)
{
    WorksheetCellIndexBuilder builder;
    scan_worksheet_events_from_chunk_source(
        read_next_chunk,
        [&](const WorksheetEvent& event) {
            builder.consume(event);
        },
        options);
    return std::move(builder).finish();
}

WorksheetCellIndex WorksheetCellIndex::build_from_xml(
    std::string_view worksheet_xml,
    WorksheetEventReaderOptions options)
{
    const std::size_t chunk_width = materialized_index_chunk_size(options);
    std::size_t position = 0;
    WorksheetInputChunkCallback source =
        [worksheet_xml, chunk_width, position](std::string& output_chunk) mutable {
            if (position >= worksheet_xml.size()) {
                output_chunk.clear();
                return false;
            }
            const std::size_t size = std::min(chunk_width, worksheet_xml.size() - position);
            output_chunk.assign(worksheet_xml.data() + position, size);
            position += size;
            return true;
        };
    return build_from_chunk_source(source, options);
}

const WorksheetCellIndexedRange* WorksheetCellIndex::find(
    std::string_view cell_reference) const noexcept
{
    const std::optional<WorksheetCellCoordinate> coordinate =
        parse_cell_reference_for_lookup(cell_reference);
    if (!coordinate.has_value()) {
        return nullptr;
    }

    if (!cells_are_sorted_) {
        const auto cell = std::find_if(
            cells_by_position_.begin(),
            cells_by_position_.end(),
            [&](const CellEntry& entry) {
                return coordinate_equal(entry, *coordinate);
            });
        return cell == cells_by_position_.end() ? nullptr : &cell->range;
    }

    const auto cell = std::lower_bound(
        cells_by_position_.begin(),
        cells_by_position_.end(),
        *coordinate,
        [](const CellEntry& entry, WorksheetCellCoordinate target) {
            return coordinate_less(entry, target);
        });
    if (cell == cells_by_position_.end() || !coordinate_equal(*cell, *coordinate)) {
        return nullptr;
    }
    return &cell->range;
}

const WorksheetCellIndex::CellRangeMap& WorksheetCellIndex::cells() const
{
    if (cells_snapshot_valid_) {
        return cells_snapshot_;
    }

    cells_snapshot_.clear();
    for (const CellEntry& entry : cells_by_position_) {
        cells_snapshot_.emplace(
            cell_reference_from_coordinate(WorksheetCellCoordinate {
                entry.row,
                entry.column,
            }),
            entry.range);
    }
    cells_snapshot_valid_ = true;
    return cells_snapshot_;
}

void WorksheetCellIndex::add_cell(
    std::string_view cell_reference,
    WorksheetCellIndexedRange range)
{
    if (range.end_offset < range.start_offset) {
        throw FastXlsxError("worksheet cell index found an invalid source cell range");
    }

    const WorksheetCellCoordinate coordinate = parse_source_cell_reference(cell_reference);
    const CellEntry entry {
        coordinate.row,
        coordinate.column,
        range,
    };
    if (!cells_by_position_.empty()) {
        const CellEntry& previous = cells_by_position_.back();
        if (coordinate_equal(previous, entry)) {
            throw FastXlsxError(
                "worksheet cell index found duplicate source cell reference: "
                + cell_reference_from_coordinate(coordinate));
        }
        if (coordinate_less(entry, previous)) {
            cells_are_sorted_ = false;
        }
    }
    cells_by_position_.push_back(entry);
    cells_snapshot_valid_ = false;
}

void WorksheetCellIndex::finalize()
{
    if (!cells_are_sorted_) {
        std::sort(cells_by_position_.begin(),
            cells_by_position_.end(),
            [](const CellEntry& left, const CellEntry& right) {
                return coordinate_less(left, right);
            });
    }

    for (std::size_t index_position = 1; index_position < cells_by_position_.size();
         ++index_position) {
        const CellEntry& previous = cells_by_position_[index_position - 1];
        const CellEntry& current = cells_by_position_[index_position];
        if (coordinate_equal(previous, current)) {
            throw FastXlsxError(
                "worksheet cell index found duplicate source cell reference: "
                + cell_reference_from_coordinate(WorksheetCellCoordinate {
                    current.row,
                    current.column,
                }));
        }
    }

    cells_are_sorted_ = true;
    cells_snapshot_valid_ = false;
}

std::vector<WorksheetIndexedCellRewrite> plan_indexed_cell_rewrites(
    const WorksheetCellIndex& index,
    std::span<const std::string_view> cell_references)
{
    std::set<std::string, std::less<>> seen_targets;
    std::vector<WorksheetIndexedCellRewrite> plan;
    plan.reserve(cell_references.size());

    for (std::string_view cell_reference : cell_references) {
        if (cell_reference.empty()) {
            throw FastXlsxError("worksheet indexed rewrite target cell reference is empty");
        }

        auto [_, inserted] = seen_targets.emplace(cell_reference);
        if (!inserted) {
            throw FastXlsxError(
                "worksheet indexed rewrite target cell reference is duplicated: "
                + std::string(cell_reference));
        }

        const WorksheetCellIndexedRange* range = index.find(cell_reference);
        if (range == nullptr) {
            throw FastXlsxError(
                "worksheet indexed rewrite target cell is missing from source index; not found: "
                + std::string(cell_reference));
        }

        plan.push_back(WorksheetIndexedCellRewrite {
            std::string(cell_reference),
            *range,
        });
    }

    sort_and_validate_rewrite_ranges(plan);

    return plan;
}

WorksheetTargetedCellRewritePlan plan_targeted_cell_rewrites_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::span<const std::string_view> cell_references,
    WorksheetEventReaderOptions options,
    bool stop_after_all_targets_found)
{
    if (cell_references.empty()) {
        return {};
    }

    TargetedWorksheetCellRewritePlanner planner(
        cell_references, stop_after_all_targets_found);
    try {
        scan_targeted_rewrite_plan_from_chunk_source(
            read_next_chunk, planner, options);
    } catch (const TargetedRewritePlanComplete&) {
        // Opt-in fast path for benchmark/prototype sparse rewrites: once every
        // requested target has an exact byte range, the remaining worksheet tail
        // can be copied by the byte-range emitter without more target lookup.
    }
    return std::move(planner).finish();
}

std::string_view worksheet_cell_range_xml(
    std::string_view worksheet_xml,
    const WorksheetCellIndexedRange& range)
{
    if (range.end_offset < range.start_offset
        || range.end_offset > static_cast<std::uint64_t>(worksheet_xml.size())) {
        throw FastXlsxError("worksheet cell index range is outside worksheet XML");
    }
    const std::uint64_t size = range.end_offset - range.start_offset;
    return worksheet_xml.substr(
        static_cast<std::size_t>(range.start_offset),
        static_cast<std::size_t>(size));
}

} // namespace fastxlsx::detail
