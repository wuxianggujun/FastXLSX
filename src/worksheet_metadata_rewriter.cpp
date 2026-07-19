#include <fastxlsx/detail/worksheet_metadata_rewriter.hpp>

#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr int hyperlink_schema_rank = 13;

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

bool is_closing_tag(std::string_view raw_xml) noexcept
{
    return raw_xml.size() > 2 && raw_xml[0] == '<' && raw_xml[1] == '/';
}

std::optional<std::string_view> attribute_value(
    std::string_view raw_tag, std::string_view requested_name)
{
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>') {
        throw FastXlsxError("worksheet hyperlink metadata contains an invalid XML tag");
    }

    std::size_t position = 1;
    if (position < raw_tag.size() && raw_tag[position] == '/') {
        ++position;
    }
    while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
        && raw_tag[position] != '/' && raw_tag[position] != '>') {
        ++position;
    }

    while (position < raw_tag.size()) {
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size() || raw_tag[position] == '/'
            || raw_tag[position] == '>') {
            return std::nullopt;
        }

        const std::size_t name_begin = position;
        while (position < raw_tag.size() && !is_xml_space(raw_tag[position])
            && raw_tag[position] != '=' && raw_tag[position] != '/'
            && raw_tag[position] != '>') {
            ++position;
        }
        const std::string_view name = raw_tag.substr(name_begin, position - name_begin);
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size() || raw_tag[position] != '=') {
            throw FastXlsxError(
                "worksheet hyperlink metadata contains an attribute without a value");
        }
        ++position;
        while (position < raw_tag.size() && is_xml_space(raw_tag[position])) {
            ++position;
        }
        if (position >= raw_tag.size()
            || (raw_tag[position] != '"' && raw_tag[position] != '\'')) {
            throw FastXlsxError(
                "worksheet hyperlink metadata contains an unquoted attribute value");
        }

        const char quote = raw_tag[position++];
        const std::size_t value_begin = position;
        while (position < raw_tag.size() && raw_tag[position] != quote) {
            ++position;
        }
        if (position >= raw_tag.size()) {
            throw FastXlsxError(
                "worksheet hyperlink metadata contains an unterminated attribute value");
        }
        const std::string_view value =
            raw_tag.substr(value_begin, position - value_begin);
        ++position;
        if (name == requested_name) {
            return value;
        }
    }

    return std::nullopt;
}

struct A1Coordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

std::optional<A1Coordinate> parse_a1_coordinate(std::string_view text)
{
    std::size_t position = 0;
    if (position < text.size() && text[position] == '$') {
        ++position;
    }

    std::uint32_t column = 0;
    const std::size_t column_begin = position;
    while (position < text.size()
        && std::isalpha(static_cast<unsigned char>(text[position])) != 0) {
        const char upper = static_cast<char>(
            std::toupper(static_cast<unsigned char>(text[position])));
        if (upper < 'A' || upper > 'Z'
            || column > (16384U - static_cast<std::uint32_t>(upper - 'A' + 1)) / 26U) {
            return std::nullopt;
        }
        column = column * 26U + static_cast<std::uint32_t>(upper - 'A' + 1);
        ++position;
    }
    if (position == column_begin || column == 0 || column > 16384U) {
        return std::nullopt;
    }
    if (position < text.size() && text[position] == '$') {
        ++position;
    }

    std::uint32_t row = 0;
    const std::size_t row_begin = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
        const std::uint32_t digit = static_cast<std::uint32_t>(text[position] - '0');
        if (row > (1048576U - digit) / 10U) {
            return std::nullopt;
        }
        row = row * 10U + digit;
        ++position;
    }
    if (position == row_begin || position != text.size() || row == 0 || row > 1048576U) {
        return std::nullopt;
    }
    return A1Coordinate {row, column};
}

bool hyperlink_ref_contains_target(
    std::string_view reference, const A1Coordinate& target)
{
    const std::size_t separator = reference.find(':');
    if (separator != std::string_view::npos
        && reference.find(':', separator + 1) != std::string_view::npos) {
        throw FastXlsxError("existing worksheet hyperlink ref is not a valid A1 range");
    }

    const std::optional<A1Coordinate> first = parse_a1_coordinate(
        separator == std::string_view::npos ? reference : reference.substr(0, separator));
    const std::optional<A1Coordinate> last = separator == std::string_view::npos
        ? first
        : parse_a1_coordinate(reference.substr(separator + 1));
    if (!first.has_value() || !last.has_value()
        || first->row > last->row || first->column > last->column) {
        throw FastXlsxError("existing worksheet hyperlink ref is not a valid A1 range");
    }
    return target.row >= first->row && target.row <= last->row
        && target.column >= first->column && target.column <= last->column;
}

std::optional<int> worksheet_suffix_schema_rank(std::string_view element_name)
{
    static constexpr std::pair<std::string_view, int> ranks[] = {
        {"sheetCalcPr", 1},
        {"sheetProtection", 2},
        {"protectedRanges", 3},
        {"scenarios", 4},
        {"autoFilter", 5},
        {"sortState", 6},
        {"dataConsolidate", 7},
        {"customSheetViews", 8},
        {"mergeCells", 9},
        {"phoneticPr", 10},
        {"conditionalFormatting", 11},
        {"dataValidations", 12},
        {"hyperlinks", hyperlink_schema_rank},
        {"printOptions", 14},
        {"pageMargins", 15},
        {"pageSetup", 16},
        {"headerFooter", 17},
        {"rowBreaks", 18},
        {"colBreaks", 19},
        {"customProperties", 20},
        {"cellWatches", 21},
        {"ignoredErrors", 22},
        {"smartTags", 23},
        {"drawing", 24},
        {"legacyDrawing", 25},
        {"legacyDrawingHF", 26},
        {"picture", 27},
        {"oleObjects", 28},
        {"controls", 29},
        {"webPublishItems", 30},
        {"tableParts", 31},
        {"extLst", 32},
    };
    const auto found = std::find_if(std::begin(ranks), std::end(ranks),
        [element_name](const auto& entry) { return entry.first == element_name; });
    if (found == std::end(ranks)) {
        return std::nullopt;
    }
    return found->second;
}

std::string internal_hyperlink_xml(const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" location=\"";
    append_escaped_xml_attribute(xml, hyperlink.location);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string external_hyperlink_xml(const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    std::string xml = "<hyperlink ref=\"";
    append_escaped_xml_attribute(xml, hyperlink.cell_reference);
    xml += "\" r:id=\"";
    append_escaped_xml_attribute(xml, hyperlink.relationship_id);
    xml += '"';
    if (!hyperlink.display.empty()) {
        xml += " display=\"";
        append_escaped_xml_attribute(xml, hyperlink.display);
        xml += '"';
    }
    if (!hyperlink.tooltip.empty()) {
        xml += " tooltip=\"";
        append_escaped_xml_attribute(xml, hyperlink.tooltip);
        xml += '"';
    }
    xml += "/>";
    return xml;
}

std::string worksheet_root_with_relationship_namespace(std::string_view raw_tag)
{
    constexpr std::string_view relationship_namespace =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const std::optional<std::string_view> current = attribute_value(raw_tag, "xmlns:r");
    if (current.has_value()) {
        if (*current != relationship_namespace) {
            throw FastXlsxError(
                "worksheet r namespace is not the OpenXML relationships namespace");
        }
        return std::string(raw_tag);
    }
    if (raw_tag.size() < 3 || raw_tag.front() != '<' || raw_tag.back() != '>'
        || is_closing_tag(raw_tag)) {
        throw FastXlsxError(
            "worksheet external hyperlink edit requires a valid worksheet root tag");
    }

    std::string root(raw_tag);
    root.insert(root.size() - 1,
        " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    return root;
}

bool is_synthetic_self_closing_end(const WorksheetEvent& event) noexcept
{
    if (!event.self_closing) {
        return false;
    }
    return event.kind == WorksheetEventKind::WorksheetEnd
        || event.kind == WorksheetEventKind::SheetDataEnd
        || event.kind == WorksheetEventKind::RowEnd
        || event.kind == WorksheetEventKind::CellEnd;
}

void write_bytes(std::ofstream& output, std::string_view bytes)
{
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw FastXlsxError("failed to write staged worksheet hyperlink metadata");
    }
}

std::string expand_self_closing_tag(std::string_view raw_tag)
{
    std::string opening(raw_tag);
    std::size_t slash = opening.size() - 2;
    while (slash > 0 && is_xml_space(opening[slash])) {
        --slash;
    }
    if (opening[slash] != '/') {
        throw FastXlsxError(
            "worksheet hyperlink rewrite expected a self-closing hyperlinks tag");
    }
    opening.erase(slash, 1);
    return opening;
}

} // namespace

WorksheetInternalHyperlinkRewritePlan plan_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view cell_reference)
{
    const std::optional<A1Coordinate> target = parse_a1_coordinate(cell_reference);
    if (!target.has_value()) {
        throw FastXlsxError("hyperlink cell reference is invalid");
    }

    bool saw_sheet_data_end = false;
    bool saw_worksheet_end = false;
    bool saw_hyperlinks = false;
    int last_suffix_rank = 0;
    std::vector<std::string> metadata_stack;
    std::optional<std::uint64_t> first_after_hyperlinks_offset;
    std::optional<WorksheetInternalHyperlinkRewritePlan> existing_container_plan;
    std::uint64_t worksheet_end_offset = 0;

    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (event.kind == WorksheetEventKind::SheetDataEnd) {
                saw_sheet_data_end = true;
                return;
            }
            if (event.kind == WorksheetEventKind::WorksheetEnd) {
                if (!event.self_closing) {
                    saw_worksheet_end = true;
                    worksheet_end_offset = event.raw_xml_offset;
                }
                return;
            }
            if (event.kind != WorksheetEventKind::Metadata) {
                return;
            }

            const bool closing = is_closing_tag(event.raw_xml);
            if (closing) {
                if (metadata_stack.empty() || metadata_stack.back() != event.element_name) {
                    throw FastXlsxError(
                        "worksheet hyperlink metadata contains mismatched element nesting");
                }
                if (metadata_stack.size() == 1 && event.element_name == "hyperlinks") {
                    if (!saw_hyperlinks || existing_container_plan.has_value()) {
                        throw FastXlsxError(
                            "worksheet contains duplicate or ambiguous hyperlinks metadata");
                    }
                    existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                        WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose,
                        event.raw_xml_offset,
                    };
                }
                metadata_stack.pop_back();
                return;
            }

            const bool top_level = metadata_stack.empty();
            const bool direct_hyperlink_child = metadata_stack.size() == 1
                && metadata_stack.front() == "hyperlinks";
            if (top_level && saw_sheet_data_end) {
                const std::optional<int> rank =
                    worksheet_suffix_schema_rank(event.element_name);
                if (!rank.has_value()) {
                    throw FastXlsxError(
                        "worksheet contains top-level suffix metadata whose position relative "
                        "to hyperlinks is unsupported");
                }
                if (*rank < last_suffix_rank) {
                    throw FastXlsxError(
                        "worksheet top-level suffix metadata is not in schema order");
                }
                last_suffix_rank = *rank;
                if (*rank > hyperlink_schema_rank
                    && !first_after_hyperlinks_offset.has_value()) {
                    first_after_hyperlinks_offset = event.raw_xml_offset;
                }
                if (event.element_name == "hyperlinks") {
                    if (saw_hyperlinks) {
                        throw FastXlsxError("worksheet contains duplicate hyperlinks containers");
                    }
                    saw_hyperlinks = true;
                    if (event.self_closing) {
                        existing_container_plan = WorksheetInternalHyperlinkRewritePlan {
                            WorksheetInternalHyperlinkRewriteAction::
                                ExpandSelfClosingContainer,
                            event.raw_xml_offset,
                        };
                    }
                }
            } else if (top_level && event.element_name == "hyperlinks") {
                throw FastXlsxError(
                    "worksheet hyperlinks metadata appears before sheetData");
            }

            if (direct_hyperlink_child) {
                if (event.element_name != "hyperlink") {
                    throw FastXlsxError(
                        "worksheet hyperlinks container has an unsupported child element");
                }
                const std::optional<std::string_view> reference =
                    attribute_value(event.raw_xml, "ref");
                if (!reference.has_value() || reference->empty()) {
                    throw FastXlsxError("existing worksheet hyperlink is missing its ref");
                }
                if (hyperlink_ref_contains_target(*reference, *target)) {
                    throw FastXlsxError(
                        "worksheet already has a hyperlink covering the requested cell");
                }
            }

            if (!event.self_closing) {
                metadata_stack.emplace_back(event.element_name);
            }
        });

    if (!metadata_stack.empty()) {
        throw FastXlsxError("worksheet hyperlink metadata ended inside an open element");
    }
    if (!saw_sheet_data_end || !saw_worksheet_end) {
        throw FastXlsxError(
            "worksheet hyperlink edit requires sheetData and a closing worksheet root");
    }
    if (saw_hyperlinks) {
        if (!existing_container_plan.has_value()) {
            throw FastXlsxError("worksheet hyperlinks container has no closing boundary");
        }
        return *existing_container_plan;
    }
    return WorksheetInternalHyperlinkRewritePlan {
        WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore,
        first_after_hyperlinks_offset.value_or(worksheet_end_offset),
    };
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.location.empty()) {
        throw FastXlsxError("internal hyperlink location cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

WorksheetInternalHyperlinkRewritePlan plan_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink)
{
    if (hyperlink.target.empty()) {
        throw FastXlsxError("external hyperlink target cannot be empty");
    }
    if (hyperlink.relationship_id.empty()) {
        throw FastXlsxError("external hyperlink relationship id cannot be empty");
    }
    return plan_worksheet_hyperlink_rewrite(
        read_next_chunk, hyperlink.cell_reference);
}

void write_worksheet_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view hyperlink_xml,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path,
    bool ensure_relationship_namespace)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to create staged worksheet hyperlink metadata file");
    }

    bool applied = false;
    scan_worksheet_events_from_chunk_source(read_next_chunk,
        [&](const WorksheetEvent& event) {
            if (is_synthetic_self_closing_end(event)) {
                return;
            }
            if (!applied && event.raw_xml_offset == plan.source_offset) {
                switch (plan.action) {
                case WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore:
                    write_bytes(output, "<hyperlinks>");
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    break;
                case WorksheetInternalHyperlinkRewriteAction::AppendBeforeContainerClose:
                    write_bytes(output, hyperlink_xml);
                    break;
                case WorksheetInternalHyperlinkRewriteAction::ExpandSelfClosingContainer:
                    write_bytes(output, expand_self_closing_tag(event.raw_xml));
                    write_bytes(output, hyperlink_xml);
                    write_bytes(output, "</hyperlinks>");
                    applied = true;
                    return;
                }
                applied = true;
            }
            if (ensure_relationship_namespace
                && event.kind == WorksheetEventKind::WorksheetStart) {
                write_bytes(output,
                    worksheet_root_with_relationship_namespace(event.raw_xml));
            } else {
                write_bytes(output, event.raw_xml);
            }
        });

    if (!applied) {
        throw FastXlsxError(
            "worksheet hyperlink rewrite did not reach its planned insertion boundary");
    }
    output.flush();
    if (!output) {
        throw FastXlsxError("failed to finalize staged worksheet hyperlink metadata file");
    }
}

void write_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        internal_hyperlink_xml(hyperlink), plan, output_path, false);
}

void write_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path)
{
    write_worksheet_hyperlink_rewrite(read_next_chunk,
        external_hyperlink_xml(hyperlink), plan, output_path, true);
}

} // namespace fastxlsx::detail
