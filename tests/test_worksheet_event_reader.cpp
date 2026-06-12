#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

using fastxlsx::detail::WorksheetEvent;
using fastxlsx::detail::WorksheetEventKind;

struct CopiedWorksheetEvent {
    WorksheetEventKind kind = WorksheetEventKind::Unsupported;
    std::string raw_xml;
    std::string element_name;
    std::string row_number;
    std::string cell_reference;
    std::string text;
    bool self_closing = false;
};

CopiedWorksheetEvent copy_event(const WorksheetEvent& event)
{
    return CopiedWorksheetEvent { event.kind,
        std::string(event.raw_xml),
        std::string(event.element_name),
        std::string(event.row_number),
        std::string(event.cell_reference),
        std::string(event.text),
        event.self_closing };
}

std::vector<WorksheetEvent> read_events(const std::string& xml)
{
    std::vector<WorksheetEvent> events;
    fastxlsx::detail::scan_worksheet_events(
        xml, [&](const WorksheetEvent& event) { events.push_back(event); });
    return events;
}

std::vector<CopiedWorksheetEvent> read_copied_events(const std::string& xml)
{
    std::vector<CopiedWorksheetEvent> events;
    fastxlsx::detail::scan_worksheet_events(
        xml, [&](const WorksheetEvent& event) { events.push_back(copy_event(event)); });
    return events;
}

std::vector<std::string_view> split_every(const std::string& xml, std::size_t width)
{
    std::vector<std::string_view> chunks;
    for (std::size_t position = 0; position < xml.size(); position += width) {
        const std::size_t length = std::min(width, xml.size() - position);
        chunks.emplace_back(xml.data() + position, length);
    }
    return chunks;
}

std::vector<CopiedWorksheetEvent> read_chunked_events(
    const std::string& xml,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions options = {})
{
    const std::vector<std::string_view> chunks = split_every(xml, chunk_width);
    std::vector<CopiedWorksheetEvent> events;
    fastxlsx::detail::scan_worksheet_events_from_chunks(
        chunks, [&](const WorksheetEvent& event) { events.push_back(copy_event(event)); }, options);
    return events;
}

std::vector<CopiedWorksheetEvent> read_source_events(
    const std::string& xml,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions options = {})
{
    std::size_t position = 0;
    std::vector<CopiedWorksheetEvent> events;
    fastxlsx::detail::scan_worksheet_events_from_chunk_source(
        [&](std::string& chunk) {
            if (position >= xml.size()) {
                chunk.clear();
                return false;
            }
            const std::size_t length = std::min(chunk_width, xml.size() - position);
            chunk.assign(xml.data() + position, length);
            position += length;
            return true;
        },
        [&](const WorksheetEvent& event) { events.push_back(copy_event(event)); },
        options);
    return events;
}

std::size_t count_kind(const std::vector<WorksheetEvent>& events, WorksheetEventKind kind)
{
    std::size_t count = 0;
    for (const WorksheetEvent& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

std::size_t count_copied_kind(
    const std::vector<CopiedWorksheetEvent>& events, WorksheetEventKind kind)
{
    std::size_t count = 0;
    for (const CopiedWorksheetEvent& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

const WorksheetEvent& find_first(const std::vector<WorksheetEvent>& events, WorksheetEventKind kind)
{
    for (const WorksheetEvent& event : events) {
        if (event.kind == kind) {
            return event;
        }
    }
    throw TestFailure("expected worksheet event kind not found");
}

void check_same_events(const std::vector<CopiedWorksheetEvent>& actual,
    const std::vector<CopiedWorksheetEvent>& expected,
    const char* message)
{
    check(actual.size() == expected.size(), message);
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const CopiedWorksheetEvent& lhs = actual[index];
        const CopiedWorksheetEvent& rhs = expected[index];
        check(lhs.kind == rhs.kind, message);
        check(lhs.raw_xml == rhs.raw_xml, message);
        check(lhs.element_name == rhs.element_name, message);
        check(lhs.row_number == rhs.row_number, message);
        check(lhs.cell_reference == rhs.cell_reference, message);
        check(lhs.text == rhs.text, message);
        check(lhs.self_closing == rhs.self_closing, message);
    }
}

void test_event_reader_scans_core_worksheet_tokens()
{
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetViews><sheetView workbookViewId="0"/></sheetViews>)"
        R"(<sheetData>)"
        R"(<row r="1" ht="18" customHeight="1">)"
        R"(<c r="A1" t="str"><v>alpha</v></c>)"
        R"(<c r="B1"><v>42</v></c>)"
        R"(</row>)"
        R"(<row r="2"/>)"
        R"(</sheetData>)"
        R"(<mergeCells count="1"><mergeCell ref="A1:B1"/></mergeCells>)"
        R"(</worksheet>)";

    const std::vector<WorksheetEvent> events = read_events(xml);

    check(!events.empty(), "worksheet event reader should emit events");
    check(events.front().kind == WorksheetEventKind::XmlDeclaration,
        "first event should be XML declaration");
    check(count_kind(events, WorksheetEventKind::WorksheetStart) == 1,
        "worksheet start should be emitted once");
    check(count_kind(events, WorksheetEventKind::WorksheetEnd) == 1,
        "worksheet end should be emitted once");
    check(count_kind(events, WorksheetEventKind::SheetDataStart) == 1,
        "sheetData start should be emitted once");
    check(count_kind(events, WorksheetEventKind::SheetDataEnd) == 1,
        "sheetData end should be emitted once");
    check(count_kind(events, WorksheetEventKind::RowStart) == 2,
        "two row starts should be emitted");
    check(count_kind(events, WorksheetEventKind::RowEnd) == 2,
        "two row ends should be emitted");
    check(count_kind(events, WorksheetEventKind::CellStart) == 2,
        "two cell starts should be emitted");
    check(count_kind(events, WorksheetEventKind::CellEnd) == 2,
        "two cell ends should be emitted");
    check(count_kind(events, WorksheetEventKind::RawText) == 0,
        "compact fixture should not emit raw text between adjacent tags");
    check(count_kind(events, WorksheetEventKind::CellValueMarkup) == 4,
        "two value elements should expose start and end wrapper markup");
    check(count_kind(events, WorksheetEventKind::CellValue) == 2,
        "two cell value events should be emitted");
    check(count_kind(events, WorksheetEventKind::Metadata) >= 4,
        "worksheet metadata tags should be exposed as raw pass-through events");

    const WorksheetEvent& first_row = find_first(events, WorksheetEventKind::RowStart);
    check(first_row.row_number == "1", "row token should expose row number");

    const WorksheetEvent& first_cell = find_first(events, WorksheetEventKind::CellStart);
    check(first_cell.row_number == "1", "cell token should expose current row number");
    check(first_cell.cell_reference == "A1", "cell token should expose cell reference");

    const WorksheetEvent& first_value = find_first(events, WorksheetEventKind::CellValue);
    check(first_value.row_number == "1", "value token should expose current row number");
    check(first_value.cell_reference == "A1", "value token should expose cell reference");
    check(first_value.text == "alpha", "value token should expose raw text");
}

void test_event_reader_handles_prefixes_inline_text_and_comments()
{
    const std::string xml =
        R"(<!--before root-->)"
        "\n"
        R"(<?fastxlsx probe?>)"
        "\n"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        "\n"
        R"(<x:sheetData>)"
        R"(<x:row r="3">)"
        R"(<x:c r="C3" t="inlineStr"><x:is><x:t>hello &amp; raw</x:t></x:is></x:c>)"
        R"(</x:row>)"
        R"(</x:sheetData>)"
        R"(</x:worksheet>)";

    const std::vector<WorksheetEvent> events = read_events(xml);

    check(count_kind(events, WorksheetEventKind::Comment) == 1,
        "comments should be emitted as pass-through events");
    check(count_kind(events, WorksheetEventKind::ProcessingInstruction) == 1,
        "processing instruction should be emitted");
    check(count_kind(events, WorksheetEventKind::RawText) >= 2,
        "raw text separators should be emitted for pass-through reconstruction");
    check(count_kind(events, WorksheetEventKind::CellValueMarkup) == 2,
        "prefixed inline value wrapper should emit start and end markup");

    const WorksheetEvent& worksheet = find_first(events, WorksheetEventKind::WorksheetStart);
    check(worksheet.element_name == "worksheet", "qualified worksheet element should expose local name");

    const WorksheetEvent& row = find_first(events, WorksheetEventKind::RowStart);
    check(row.row_number == "3", "prefixed row token should expose row number");

    const WorksheetEvent& cell = find_first(events, WorksheetEventKind::CellStart);
    check(cell.cell_reference == "C3", "prefixed cell token should expose cell reference");

    const WorksheetEvent& value = find_first(events, WorksheetEventKind::CellValue);
    check(value.text == "hello &amp; raw", "inline text should remain raw and non-decoded");
}

void test_event_reader_rejects_malformed_boundaries()
{
    bool missing_root_failed = false;
    try {
        const std::string xml = R"(<sheetData/>)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        missing_root_failed = true;
    }
    check(missing_root_failed, "worksheet event reader should require a worksheet root");

    bool open_cell_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></row></sheetData></worksheet>)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        open_cell_failed = true;
    }
    check(open_cell_failed, "worksheet event reader should reject unbalanced cell boundaries");

    bool unquoted_attribute_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r=1></row></sheetData></worksheet>)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        unquoted_attribute_failed = true;
    }
    check(unquoted_attribute_failed,
        "worksheet event reader should reject unquoted row attributes it consumes");
}

void test_event_reader_rejects_non_prolog_markup_outside_worksheet_root()
{
    bool pre_root_element_failed = false;
    try {
        const std::string xml = R"(<ignored/><worksheet><sheetData/></worksheet>)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        pre_root_element_failed = true;
    }
    check(pre_root_element_failed,
        "worksheet event reader should reject non-prolog markup before worksheet root");

    bool post_root_element_failed = false;
    try {
        const std::string xml = R"(<worksheet><sheetData/></worksheet><ignored/>)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        post_root_element_failed = true;
    }
    check(post_root_element_failed,
        "worksheet event reader should reject markup after worksheet root");

    bool post_root_text_failed = false;
    try {
        const std::string xml = R"(<worksheet><sheetData/></worksheet>text)";
        (void)read_events(xml);
    } catch (const std::exception&) {
        post_root_text_failed = true;
    }
    check(post_root_text_failed,
        "worksheet event reader should reject non-whitespace text after worksheet root");
}

void test_event_reader_chunked_scanner_matches_full_scan_across_token_boundaries()
{
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<!--before root-->)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<x:dimension ref="A1:B2"/>)"
        R"(<x:sheetData>)"
        R"(<x:row r="1">)"
        R"(<x:c r="A1" t="inlineStr"><x:is><x:t>alpha</x:t></x:is></x:c>)"
        R"(<x:c r="B1"><x:v>42</x:v></x:c>)"
        R"(</x:row>)"
        R"(</x:sheetData>)"
        R"(<?after data?>)"
        R"(</x:worksheet>)";

    const std::vector<CopiedWorksheetEvent> full_events = read_copied_events(xml);
    const std::vector<CopiedWorksheetEvent> chunked_events = read_chunked_events(xml, 5);

    check_same_events(chunked_events,
        full_events,
        "chunked worksheet event reader should match full-buffer events");
}

void test_event_reader_chunk_source_matches_full_scan_across_token_boundaries()
{
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<!--before root-->)"
        R"(<worksheet>)"
        R"(<dimension ref="A1:B2"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>alpha</v></c><c r="B1"><v>42</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";

    const std::vector<CopiedWorksheetEvent> full_events = read_copied_events(xml);
    const std::vector<CopiedWorksheetEvent> source_events = read_source_events(xml, 3);

    check_same_events(source_events,
        full_events,
        "chunk-source worksheet event reader should match full-buffer events");
}

void test_event_reader_chunked_scanner_uses_bounded_window_not_full_document()
{
    std::string xml = R"(<worksheet><sheetData>)";
    for (int row = 1; row <= 24; ++row) {
        xml += R"(<row r=")";
        xml += std::to_string(row);
        xml += R"("><c r="A)";
        xml += std::to_string(row);
        xml += R"("><v>)";
        xml += std::to_string(row);
        xml += R"(</v></c></row>)";
    }
    xml += R"(</sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.max_window_bytes = 96;
    const std::vector<CopiedWorksheetEvent> events = read_chunked_events(xml, 7, options);

    check(xml.size() > options.max_window_bytes,
        "bounded-window fixture should be larger than the retained window");
    check(count_copied_kind(events, WorksheetEventKind::RowStart) == 24,
        "chunked scanner should emit every row without retaining the full worksheet");
    check(count_copied_kind(events, WorksheetEventKind::CellValue) == 24,
        "chunked scanner should emit every cell value without retaining the full worksheet");
}

void test_event_reader_chunked_scanner_rejects_oversized_incomplete_token()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>)" + std::string(80, 'x')
        + R"(</v></c></row></sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.max_window_bytes = 32;

    bool failed = false;
    try {
        (void)read_chunked_events(xml, 9, options);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "chunked scanner should reject an incomplete XML text token over the bounded window");
}

} // namespace

int main()
{
    try {
        test_event_reader_scans_core_worksheet_tokens();
        test_event_reader_handles_prefixes_inline_text_and_comments();
        test_event_reader_rejects_malformed_boundaries();
        test_event_reader_rejects_non_prolog_markup_outside_worksheet_root();
        test_event_reader_chunked_scanner_matches_full_scan_across_token_boundaries();
        test_event_reader_chunk_source_matches_full_scan_across_token_boundaries();
        test_event_reader_chunked_scanner_uses_bounded_window_not_full_document();
        test_event_reader_chunked_scanner_rejects_oversized_incomplete_token();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
