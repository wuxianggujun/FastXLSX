#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <algorithm>
#include <cstdint>
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
    std::uint64_t raw_xml_offset = 0;
    bool complete_cell = false;
    bool contains_formula = false;
    bool uses_shared_string_index = false;
    bool has_style_reference = false;
};

CopiedWorksheetEvent copy_event(const WorksheetEvent& event)
{
    return CopiedWorksheetEvent { event.kind,
        std::string(event.raw_xml),
        std::string(event.element_name),
        std::string(event.row_number),
        std::string(event.cell_reference),
        std::string(event.text),
        event.self_closing,
        event.raw_xml_offset,
        event.complete_cell,
        event.contains_formula,
        event.uses_shared_string_index,
        event.has_style_reference };
}

std::vector<CopiedWorksheetEvent> read_source_events(
    const std::string& xml,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions options = {})
{
    if (chunk_width == 0) {
        throw TestFailure("test chunk width must be nonzero");
    }

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

std::vector<CopiedWorksheetEvent> read_single_chunk_events(const std::string& xml)
{
    return read_source_events(xml, std::max<std::size_t>(xml.size(), 1U));
}

std::vector<CopiedWorksheetEvent> read_chunked_events(
    const std::string& xml,
    std::size_t chunk_width,
    fastxlsx::detail::WorksheetEventReaderOptions options = {})
{
    return read_source_events(xml, chunk_width, options);
}

std::size_t count_kind(const std::vector<CopiedWorksheetEvent>& events, WorksheetEventKind kind)
{
    std::size_t count = 0;
    for (const CopiedWorksheetEvent& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

const CopiedWorksheetEvent& find_first(
    const std::vector<CopiedWorksheetEvent>& events, WorksheetEventKind kind)
{
    for (const CopiedWorksheetEvent& event : events) {
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
        check(lhs.raw_xml_offset == rhs.raw_xml_offset, message);
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

    const std::vector<CopiedWorksheetEvent> events = read_single_chunk_events(xml);

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

    const CopiedWorksheetEvent& first_row = find_first(events, WorksheetEventKind::RowStart);
    check(first_row.row_number == "1", "row token should expose row number");

    const CopiedWorksheetEvent& first_cell = find_first(events, WorksheetEventKind::CellStart);
    check(first_cell.row_number == "1", "cell token should expose current row number");
    check(first_cell.cell_reference == "A1", "cell token should expose cell reference");

    const CopiedWorksheetEvent& first_value = find_first(events, WorksheetEventKind::CellValue);
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

    const std::vector<CopiedWorksheetEvent> events = read_single_chunk_events(xml);

    check(count_kind(events, WorksheetEventKind::Comment) == 1,
        "comments should be emitted as pass-through events");
    check(count_kind(events, WorksheetEventKind::ProcessingInstruction) == 1,
        "processing instruction should be emitted");
    check(count_kind(events, WorksheetEventKind::RawText) >= 2,
        "raw text separators should be emitted for pass-through reconstruction");
    check(count_kind(events, WorksheetEventKind::CellValueMarkup) == 2,
        "prefixed inline value wrapper should emit start and end markup");

    const CopiedWorksheetEvent& worksheet = find_first(events, WorksheetEventKind::WorksheetStart);
    check(worksheet.element_name == "worksheet", "qualified worksheet element should expose local name");

    const CopiedWorksheetEvent& row = find_first(events, WorksheetEventKind::RowStart);
    check(row.row_number == "3", "prefixed row token should expose row number");

    const CopiedWorksheetEvent& cell = find_first(events, WorksheetEventKind::CellStart);
    check(cell.cell_reference == "C3", "prefixed cell token should expose cell reference");

    const CopiedWorksheetEvent& value = find_first(events, WorksheetEventKind::CellValue);
    check(value.text == "hello &amp; raw", "inline text should remain raw and non-decoded");
}

void test_event_reader_exposes_cell_inner_metadata()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1" t="inlineStr"><is><r><t>rich</t></r></is></c>)"
        R"(</row></sheetData></worksheet>)";

    const std::vector<CopiedWorksheetEvent> events = read_single_chunk_events(xml);
    const auto has_cell_metadata = [&](std::string_view raw_xml) {
        return std::any_of(events.begin(), events.end(), [&](const CopiedWorksheetEvent& event) {
            return event.kind == WorksheetEventKind::Metadata && event.raw_xml == raw_xml
                && event.row_number == "1" && event.cell_reference == "A1";
        });
    };

    check(has_cell_metadata("<is>"),
        "inline-string container should be exposed as cell metadata");
    check(has_cell_metadata("<r>"),
        "inline rich-text run should be exposed as cell metadata");
    check(has_cell_metadata("</r>"),
        "inline rich-text run close should be exposed as cell metadata");
    check(has_cell_metadata("</is>"),
        "inline-string container close should be exposed as cell metadata");
    check(count_kind(events, WorksheetEventKind::CellValue) == 1,
        "inline rich-text text should still be exposed as cell value text");
}

void test_event_reader_distinguishes_xml_stylesheet_processing_instruction()
{
    const std::string xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<?xml-stylesheet type="text/xsl" href="sheet.xsl"?>)"
        R"(<worksheet><sheetData/></worksheet>)";

    const std::vector<CopiedWorksheetEvent> events = read_chunked_events(xml, 4);

    check(count_kind(events, WorksheetEventKind::XmlDeclaration) == 1,
        "xml declaration should be classified once");
    check(count_kind(events, WorksheetEventKind::ProcessingInstruction) == 1,
        "xml-stylesheet should be classified as a processing instruction");
    const CopiedWorksheetEvent& processing_instruction =
        find_first(events, WorksheetEventKind::ProcessingInstruction);
    check(processing_instruction.raw_xml.find("xml-stylesheet") != std::string::npos,
        "processing instruction should preserve the xml-stylesheet raw XML");
}

void test_event_reader_exposes_absolute_source_offsets_across_chunks()
{
    const std::string xml =
        "\n"
        R"(<!--prolog-->)"
        "\n"
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>alpha</v></c>)"
        R"(<c r="B1"/>)"
        R"(</row></sheetData></worksheet>)";

    const std::vector<CopiedWorksheetEvent> events = read_source_events(xml, 3);

    const auto find_cell_event = [&](WorksheetEventKind kind, std::string_view reference) {
        for (const CopiedWorksheetEvent& event : events) {
            if (event.kind == kind && event.cell_reference == reference) {
                return event;
            }
        }
        throw TestFailure("expected cell event with source offset not found");
    };

    const CopiedWorksheetEvent a1_start = find_cell_event(WorksheetEventKind::CellStart, "A1");
    const CopiedWorksheetEvent a1_value = find_cell_event(WorksheetEventKind::CellValue, "A1");
    const CopiedWorksheetEvent a1_end = find_cell_event(WorksheetEventKind::CellEnd, "A1");
    const CopiedWorksheetEvent b1_start = find_cell_event(WorksheetEventKind::CellStart, "B1");
    const CopiedWorksheetEvent b1_end = find_cell_event(WorksheetEventKind::CellEnd, "B1");

    const std::size_t a1_start_offset = xml.find(R"(<c r="A1">)");
    const std::size_t a1_value_offset = xml.find("alpha");
    const std::size_t a1_end_offset = xml.find("</c>", a1_value_offset);
    const std::size_t b1_offset = xml.find(R"(<c r="B1"/>)");

    check(a1_start.raw_xml_offset == a1_start_offset,
        "A1 start event should expose the absolute byte offset");
    check(a1_value.raw_xml_offset == a1_value_offset,
        "A1 value event should expose the absolute byte offset");
    check(a1_end.raw_xml_offset == a1_end_offset,
        "A1 end event should expose the absolute byte offset");
    check(b1_start.raw_xml_offset == b1_offset,
        "self-closing B1 start event should expose the absolute byte offset");
    check(b1_end.raw_xml_offset == b1_offset,
        "self-closing B1 end event should reuse the start tag byte offset");
    check(xml.substr(static_cast<std::size_t>(a1_start.raw_xml_offset), a1_start.raw_xml.size())
            == a1_start.raw_xml,
        "A1 start offset should point back into the original source XML");
    check(xml.substr(static_cast<std::size_t>(b1_start.raw_xml_offset), b1_start.raw_xml.size())
            == b1_start.raw_xml,
        "B1 start offset should point back into the original source XML");
}

void test_event_reader_can_skip_context_attribute_copies()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>alpha</v></c>)"
        R"(</row></sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.copy_context_attributes = false;
    const std::vector<CopiedWorksheetEvent> events = read_source_events(xml, 4, options);

    const CopiedWorksheetEvent& row_start = find_first(events, WorksheetEventKind::RowStart);
    check(row_start.row_number == "1",
        "no-copy event reader should still expose row number on row start");

    const CopiedWorksheetEvent& cell_start = find_first(events, WorksheetEventKind::CellStart);
    check(cell_start.cell_reference == "A1",
        "no-copy event reader should still expose cell reference on cell start");

    const CopiedWorksheetEvent& value = find_first(events, WorksheetEventKind::CellValue);
    check(value.row_number.empty() && value.cell_reference.empty(),
        "no-copy event reader should not retain context attributes for later nested events");

    const CopiedWorksheetEvent& cell_end = find_first(events, WorksheetEventKind::CellEnd);
    check(cell_end.cell_reference.empty(),
        "no-copy event reader should not retain cell reference for cell end");
}

void test_event_reader_coalesces_patch_value_events_with_telemetry()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>alpha</v></c>)"
        R"(<c r="B1"><f>A1+1</f><v>2</v></c>)"
        R"(</row></sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderTelemetry telemetry;
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.copy_context_attributes = false;
    options.coalesce_cell_value_events = true;
    options.max_window_bytes = 32;
    options.telemetry = &telemetry;
    const std::vector<CopiedWorksheetEvent> events =
        read_source_events(xml, 7, options);

    std::string reconstructed;
    for (const CopiedWorksheetEvent& event : events) {
        reconstructed += event.raw_xml;
    }
    check(reconstructed == xml,
        "coalesced event reader should preserve exact worksheet bytes");
    check(count_kind(events, WorksheetEventKind::CellValue) == 0,
        "coalesced event reader should replace cell value callbacks with raw byte spans");
    check(count_kind(events, WorksheetEventKind::CellValueMarkup) == 2,
        "coalesced event reader should keep formula boundaries visible");
    check(telemetry.parsed_event_count > telemetry.callback_event_count,
        "coalesced event reader should reduce callback traffic");
    check(telemetry.coalesced_input_event_count > telemetry.coalesced_output_event_count,
        "coalesced event reader should merge multiple value events per output event");
    check(telemetry.callback_event_count == events.size(),
        "event telemetry callback count should match observed events");
}

void test_event_reader_fast_paths_simple_inline_strings_and_falls_back_safely()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1" t="inlineStr"><is><t xml:space="preserve">alpha &amp; beta</t></is></c>)"
        R"(<c r="B1" t="inlineStr"><is><t/></is></c>)"
        R"(<c r="C1" t="inlineStr"><is><r><t>rich</t></r></is></c>)"
        R"(<c r="D1"><f>A1&amp;"!"</f><v>0</v></c>)"
        R"(</row></sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderTelemetry detailed_telemetry;
    fastxlsx::detail::WorksheetEventReaderOptions detailed_options;
    detailed_options.telemetry = &detailed_telemetry;
    const std::vector<CopiedWorksheetEvent> detailed =
        read_source_events(xml, xml.size(), detailed_options);

    fastxlsx::detail::WorksheetEventReaderTelemetry telemetry;
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.copy_context_attributes = false;
    options.coalesce_cell_value_events = true;
    options.telemetry = &telemetry;
    const std::vector<CopiedWorksheetEvent> events =
        read_source_events(xml, xml.size(), options);

    std::string reconstructed;
    for (const CopiedWorksheetEvent& event : events) {
        reconstructed += event.raw_xml;
    }
    check(reconstructed == xml,
        "inline-string fast path should preserve exact worksheet bytes");
    check(telemetry.simple_inline_string_fast_path_count == 2,
        "simple inline strings should use the bounded fast path");
    check(telemetry.simple_inline_string_fast_path_bytes
            == std::string_view(R"(<is><t xml:space="preserve">alpha &amp; beta</t></is>)").size()
                + std::string_view(R"(<is><t/></is>)").size(),
        "inline-string fast path should report exact consumed bytes");
    check(telemetry.simple_inline_string_fallback_count == 1,
        "rich inline strings should report one ordinary-parser fallback");
    check(telemetry.parsed_event_count < detailed_telemetry.parsed_event_count,
        "inline-string fast path should reduce parsed event traffic");
    check(count_kind(events, WorksheetEventKind::CellValueMarkup) == 2,
        "inline-string fast path should keep formula boundaries visible");
    check(count_kind(detailed, WorksheetEventKind::Metadata)
            > count_kind(events, WorksheetEventKind::Metadata),
        "detailed mode should retain inline-string metadata events");
}

void test_event_reader_coalesces_complete_cells_and_preserves_fallbacks()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1" t="s"><v>1</v></c>)"
        R"(<c r="B1" s="3" t="inlineStr"><is><t>text</t></is></c>)"
        R"(<c r="C1"><f>A1+1</f><v>2</v></c>)"
        R"(<c r="D1" t="inlineStr"><is><r><t>rich</t></r></is></c>)"
        R"(</row></sheetData></worksheet>)";

    fastxlsx::detail::WorksheetEventReaderTelemetry telemetry;
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.copy_context_attributes = false;
    options.coalesce_cell_value_events = true;
    options.coalesce_complete_cell_events = true;
    options.telemetry = &telemetry;
    const std::vector<CopiedWorksheetEvent> events =
        read_source_events(xml, xml.size(), options);

    std::string reconstructed;
    for (const CopiedWorksheetEvent& event : events) {
        reconstructed += event.raw_xml;
    }
    check(reconstructed == xml,
        "complete-cell coalescing should preserve exact worksheet bytes");
    check(telemetry.complete_cell_coalesced_count == 3,
        "numeric, simple inline-string, and formula cells should coalesce");
    check(telemetry.complete_cell_fallback_count == 1,
        "rich inline-string metadata should retain the detailed fallback path");
    check(telemetry.complete_cell_coalesced_bytes
            > telemetry.complete_cell_coalesced_count,
        "complete-cell telemetry should report exact-byte traffic");

    const auto formula = std::find_if(events.begin(), events.end(),
        [](const CopiedWorksheetEvent& event) {
            return event.cell_reference == "C1";
        });
    check(formula != events.end() && formula->complete_cell && formula->contains_formula,
        "coalesced formula cells should retain formula audit metadata");
    const auto shared_string = std::find_if(events.begin(), events.end(),
        [](const CopiedWorksheetEvent& event) {
            return event.cell_reference == "A1";
        });
    check(shared_string != events.end() && shared_string->uses_shared_string_index,
        "coalesced cells should retain shared-string audit metadata");
    const auto styled = std::find_if(events.begin(), events.end(),
        [](const CopiedWorksheetEvent& event) {
            return event.cell_reference == "B1";
        });
    check(styled != events.end() && styled->has_style_reference,
        "coalesced cells should retain style audit metadata");

    fastxlsx::detail::WorksheetEventReaderTelemetry boundary_telemetry;
    options.max_window_bytes = 32;
    options.telemetry = &boundary_telemetry;
    const std::vector<CopiedWorksheetEvent> boundary_events =
        read_source_events(xml, 1, options);
    std::string boundary_reconstructed;
    for (const CopiedWorksheetEvent& event : boundary_events) {
        boundary_reconstructed += event.raw_xml;
    }
    check(boundary_reconstructed == xml,
        "window-split complete cells should preserve bytes through fallback");
    check(boundary_telemetry.complete_cell_fallback_count > 0,
        "window-split cells should report complete-cell fallback traffic");
}

void test_event_reader_inline_string_fast_path_preserves_chunked_fallback_diagnostics()
{
    const std::string xml =
        R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>alpha</t></is></c></row></sheetData></worksheet>)";
    fastxlsx::detail::WorksheetEventReaderTelemetry telemetry;
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.copy_context_attributes = false;
    options.coalesce_cell_value_events = true;
    options.max_window_bytes = 32;
    options.telemetry = &telemetry;
    const std::vector<CopiedWorksheetEvent> events = read_source_events(xml, 1, options);

    std::string reconstructed;
    for (const CopiedWorksheetEvent& event : events) {
        reconstructed += event.raw_xml;
    }
    check(reconstructed == xml,
        "boundary-split inline strings should preserve exact bytes through fallback");
    check(telemetry.simple_inline_string_fast_path_count == 0,
        "boundary-split inline strings should retain the ordinary parser path");
    check(telemetry.simple_inline_string_fallback_count == 1,
        "boundary-split inline strings should report one fallback");

    bool failed = false;
    try {
        const std::string malformed =
            R"(<worksheet><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>alpha</v></is></c></row></sheetData></worksheet>)";
        (void)read_source_events(malformed, malformed.size(), options);
    } catch (const std::exception& error) {
        failed = true;
        check(std::string_view(error.what()).find("mismatched cell value boundary")
                != std::string_view::npos,
            "inline-string fallback should retain mismatched-value diagnostics");
    }
    check(failed,
        "inline-string fast-path candidates should not bypass malformed value diagnostics");
}

void test_event_reader_rejects_xml_declaration_after_root_start()
{
    const std::string xml =
        R"(<worksheet><?xml version="1.0"?><sheetData/></worksheet>)";

    bool failed = false;
    try {
        (void)read_chunked_events(xml, 5);
    } catch (const std::exception& error) {
        failed = true;
        check(std::string_view(error.what()).find("XML declaration after worksheet root")
                != std::string_view::npos,
            "late XML declaration failure should name the invalid root position");
    }
    check(failed, "worksheet event reader should reject XML declaration inside the root");

    const std::string processing_instruction_xml =
        R"(<worksheet><?fastxlsx probe?><sheetData/></worksheet>)";
    const std::vector<CopiedWorksheetEvent> events =
        read_chunked_events(processing_instruction_xml, 5);
    check(count_kind(events, WorksheetEventKind::ProcessingInstruction) == 1,
        "ordinary processing instructions inside the root should remain pass-through tokens");
}

void test_event_reader_rejects_mismatched_cell_value_boundaries()
{
    bool mismatched_value_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</f></c></row></sheetData></worksheet>)";
        (void)read_chunked_events(xml, 4);
    } catch (const std::exception& error) {
        mismatched_value_failed = true;
        check(std::string_view(error.what()).find("mismatched cell value boundary")
                != std::string_view::npos,
            "mismatched value wrapper failure should name the cell value boundary");
    }
    check(mismatched_value_failed,
        "worksheet event reader should reject mismatched cell value wrapper tags");

    bool orphan_value_end_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"></v></c></row></sheetData></worksheet>)";
        (void)read_chunked_events(xml, 5);
    } catch (const std::exception& error) {
        orphan_value_end_failed = true;
        check(std::string_view(error.what()).find("mismatched cell value boundary")
                != std::string_view::npos,
            "orphan value wrapper failure should name the cell value boundary");
    }
    check(orphan_value_end_failed,
        "worksheet event reader should reject orphan cell value closing tags");

    bool nested_value_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v><t>nested</t></v></c></row></sheetData></worksheet>)";
        (void)read_chunked_events(xml, 6);
    } catch (const std::exception& error) {
        nested_value_failed = true;
        check(std::string_view(error.what()).find("nested cell value markup")
                != std::string_view::npos,
            "nested value wrapper failure should name nested value markup");
    }
    check(nested_value_failed,
        "worksheet event reader should reject nested cell value wrapper tags");
}

void test_event_reader_rejects_invalid_core_element_nesting()
{
    bool duplicate_sheet_data_failed = false;
    try {
        const std::string xml = R"(<worksheet><sheetData/><sheetData/></worksheet>)";
        (void)read_chunked_events(xml, 3);
    } catch (const std::exception& error) {
        duplicate_sheet_data_failed = true;
        check(std::string_view(error.what()).find("invalid sheetData boundary")
                != std::string_view::npos,
            "duplicate sheetData failure should name the sheetData boundary");
    }
    check(duplicate_sheet_data_failed,
        "worksheet event reader should reject duplicate sheetData elements");

    bool nested_row_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><row r="2"/></row></sheetData></worksheet>)";
        (void)read_chunked_events(xml, 4);
    } catch (const std::exception& error) {
        nested_row_failed = true;
        check(std::string_view(error.what()).find("invalid row boundary")
                != std::string_view::npos,
            "nested row failure should name the row boundary");
    }
    check(nested_row_failed,
        "worksheet event reader should reject nested row elements");

    bool nested_cell_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"><c r="B1"/></c></row></sheetData></worksheet>)";
        (void)read_chunked_events(xml, 5);
    } catch (const std::exception& error) {
        nested_cell_failed = true;
        check(std::string_view(error.what()).find("invalid cell boundary")
                != std::string_view::npos,
            "nested cell failure should name the cell boundary");
    }
    check(nested_cell_failed,
        "worksheet event reader should reject nested cell elements");
}

void test_event_reader_rejects_malformed_boundaries()
{
    bool missing_root_failed = false;
    try {
        const std::string xml = R"(<sheetData/>)";
        (void)read_single_chunk_events(xml);
    } catch (const std::exception&) {
        missing_root_failed = true;
    }
    check(missing_root_failed, "worksheet event reader should require a worksheet root");

    bool open_cell_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</v></row></sheetData></worksheet>)";
        (void)read_single_chunk_events(xml);
    } catch (const std::exception&) {
        open_cell_failed = true;
    }
    check(open_cell_failed, "worksheet event reader should reject unbalanced cell boundaries");

    bool unquoted_attribute_failed = false;
    try {
        const std::string xml =
            R"(<worksheet><sheetData><row r=1></row></sheetData></worksheet>)";
        (void)read_single_chunk_events(xml);
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
        (void)read_single_chunk_events(xml);
    } catch (const std::exception&) {
        pre_root_element_failed = true;
    }
    check(pre_root_element_failed,
        "worksheet event reader should reject non-prolog markup before worksheet root");

    bool post_root_element_failed = false;
    try {
        const std::string xml = R"(<worksheet><sheetData/></worksheet><ignored/>)";
        (void)read_single_chunk_events(xml);
    } catch (const std::exception&) {
        post_root_element_failed = true;
    }
    check(post_root_element_failed,
        "worksheet event reader should reject markup after worksheet root");

    bool post_root_text_failed = false;
    try {
        const std::string xml = R"(<worksheet><sheetData/></worksheet>text)";
        (void)read_single_chunk_events(xml);
    } catch (const std::exception&) {
        post_root_text_failed = true;
    }
    check(post_root_text_failed,
        "worksheet event reader should reject non-whitespace text after worksheet root");
}

void test_event_reader_chunk_source_matches_single_chunk_scan_across_token_boundaries()
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

    const std::vector<CopiedWorksheetEvent> single_chunk_events = read_single_chunk_events(xml);
    const std::vector<CopiedWorksheetEvent> chunked_events = read_source_events(xml, 5);

    check_same_events(chunked_events,
        single_chunk_events,
        "chunk-source worksheet event reader should match single-chunk events");
}

void test_event_reader_tiny_chunk_source_matches_single_chunk_scan_across_token_boundaries()
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

    const std::vector<CopiedWorksheetEvent> single_chunk_events = read_single_chunk_events(xml);
    const std::vector<CopiedWorksheetEvent> source_events = read_source_events(xml, 3);

    check_same_events(source_events,
        single_chunk_events,
        "chunk-source worksheet event reader should match single-chunk events");
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
    const std::vector<CopiedWorksheetEvent> events = read_source_events(xml, 7, options);

    check(xml.size() > options.max_window_bytes,
        "bounded-window fixture should be larger than the retained window");
    check(count_kind(events, WorksheetEventKind::RowStart) == 24,
        "chunked scanner should emit every row without retaining the full worksheet");
    check(count_kind(events, WorksheetEventKind::CellValue) == 24,
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
        (void)read_source_events(xml, 9, options);
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
        test_event_reader_exposes_cell_inner_metadata();
        test_event_reader_distinguishes_xml_stylesheet_processing_instruction();
        test_event_reader_exposes_absolute_source_offsets_across_chunks();
        test_event_reader_can_skip_context_attribute_copies();
        test_event_reader_coalesces_patch_value_events_with_telemetry();
        test_event_reader_fast_paths_simple_inline_strings_and_falls_back_safely();
        test_event_reader_coalesces_complete_cells_and_preserves_fallbacks();
        test_event_reader_inline_string_fast_path_preserves_chunked_fallback_diagnostics();
        test_event_reader_rejects_xml_declaration_after_root_start();
        test_event_reader_rejects_mismatched_cell_value_boundaries();
        test_event_reader_rejects_invalid_core_element_nesting();
        test_event_reader_rejects_malformed_boundaries();
        test_event_reader_rejects_non_prolog_markup_outside_worksheet_root();
        test_event_reader_chunk_source_matches_single_chunk_scan_across_token_boundaries();
        test_event_reader_tiny_chunk_source_matches_single_chunk_scan_across_token_boundaries();
        test_event_reader_chunked_scanner_uses_bounded_window_not_full_document();
        test_event_reader_chunked_scanner_rejects_oversized_incomplete_token();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
