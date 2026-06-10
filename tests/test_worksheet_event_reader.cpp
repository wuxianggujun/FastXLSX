#include <fastxlsx/detail/worksheet_event_reader.hpp>

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

std::vector<WorksheetEvent> read_events(const std::string& xml)
{
    std::vector<WorksheetEvent> events;
    fastxlsx::detail::scan_worksheet_events(
        xml, [&](const WorksheetEvent& event) { events.push_back(event); });
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

const WorksheetEvent& find_first(const std::vector<WorksheetEvent>& events, WorksheetEventKind kind)
{
    for (const WorksheetEvent& event : events) {
        if (event.kind == kind) {
            return event;
        }
    }
    throw TestFailure("expected worksheet event kind not found");
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
        R"(<?fastxlsx probe?>)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
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

} // namespace

int main()
{
    try {
        test_event_reader_scans_core_worksheet_tokens();
        test_event_reader_handles_prefixes_inline_text_and_comments();
        test_event_reader_rejects_malformed_boundaries();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
