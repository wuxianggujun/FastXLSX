#include <fastxlsx/detail/worksheet_cell_index.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

fastxlsx::detail::WorksheetInputChunkCallback make_string_chunk_source(
    const std::string& xml, std::size_t chunk_width)
{
    if (chunk_width == 0) {
        throw TestFailure("test chunk width must be nonzero");
    }

    return [&xml, chunk_width, position = std::size_t {0}](
               std::string& chunk) mutable {
        if (position >= xml.size()) {
            chunk.clear();
            return false;
        }
        const std::size_t length = std::min(chunk_width, xml.size() - position);
        chunk.assign(xml.data() + position, length);
        position += length;
        return true;
    };
}

bool build_index_fails(std::string_view xml)
{
    try {
        (void)fastxlsx::detail::WorksheetCellIndex::build_from_xml(xml);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void test_cell_index_maps_exact_source_cell_ranges()
{
    const std::string xml =
        R"(<x:worksheet xmlns:x="urn:worksheet">)"
        R"(<x:sheetData>)"
        R"(<x:row r="1">)"
        R"(<x:c r="A1"><x:v>alpha</x:v></x:c>)"
        R"(<x:c r="B1"/>)"
        R"(</x:row>)"
        R"(<x:row r="3">)"
        R"(<x:c r="C3" t="inlineStr"><x:is><x:t>text</x:t></x:is></x:c>)"
        R"(</x:row>)"
        R"(</x:sheetData>)"
        R"(</x:worksheet>)";

    const fastxlsx::detail::WorksheetCellIndex index =
        fastxlsx::detail::WorksheetCellIndex::build_from_xml(xml);

    check(index.cell_count() == 3,
        "worksheet cell index should record one entry per source cell");

    const auto* a1 = index.find("A1");
    const auto* b1 = index.find("B1");
    const auto* c3 = index.find("C3");
    check(a1 != nullptr && b1 != nullptr && c3 != nullptr,
        "worksheet cell index should find indexed cell references");
    check(index.find("D4") == nullptr,
        "worksheet cell index should return null for missing cells");

    check(fastxlsx::detail::worksheet_cell_range_xml(xml, *a1)
            == R"(<x:c r="A1"><x:v>alpha</x:v></x:c>)",
        "A1 range should cover the full source cell element");
    check(fastxlsx::detail::worksheet_cell_range_xml(xml, *b1) == R"(<x:c r="B1"/>)",
        "B1 range should cover the self-closing source cell element");
    check(fastxlsx::detail::worksheet_cell_range_xml(xml, *c3)
            == R"(<x:c r="C3" t="inlineStr"><x:is><x:t>text</x:t></x:is></x:c>)",
        "C3 range should include nested inline string markup");

    check(a1->start_offset == xml.find(R"(<x:c r="A1">)"),
        "A1 start offset should match the source byte position");
    check(b1->start_offset == xml.find(R"(<x:c r="B1"/>)"),
        "B1 start offset should match the source byte position");
    check(c3->end_offset
            == xml.find(R"(</x:c>)", xml.find(R"(<x:c r="C3")")) + std::string_view("</x:c>").size(),
        "C3 end offset should point after the closing cell tag");
}

void test_cell_index_chunk_source_matches_materialized_offsets()
{
    const std::string xml =
        R"(<?xml version="1.0"?>)"
        R"(<worksheet><sheetData><row r="1">)"
        R"(<c r="A1"><v>1</v></c>)"
        R"(<c r="C1"><v>3</v></c>)"
        R"(</row><row r="5">)"
        R"(<c r="B5"><v>tail</v></c>)"
        R"(</row></sheetData></worksheet>)";

    const fastxlsx::detail::WorksheetCellIndex materialized =
        fastxlsx::detail::WorksheetCellIndex::build_from_xml(xml);
    fastxlsx::detail::WorksheetInputChunkCallback source = make_string_chunk_source(xml, 5);
    const fastxlsx::detail::WorksheetCellIndex chunked =
        fastxlsx::detail::WorksheetCellIndex::build_from_chunk_source(source);

    check(chunked.cells() == materialized.cells(),
        "chunk-source cell index should preserve materialized source offsets");
}

void test_cell_index_rejects_ambiguous_or_invalid_source_cells()
{
    check(build_index_fails(
              R"(<worksheet><sheetData><row r="1"><c r="A1"/><c r="A1"/></row></sheetData></worksheet>)"),
        "worksheet cell index should reject duplicate source cell references");
    check(build_index_fails(
              R"(<worksheet><sheetData><row r="1"><c><v>1</v></c></row></sheetData></worksheet>)"),
        "worksheet cell index should reject source cells without r attributes");
    check(build_index_fails(
              R"(<worksheet><sheetData><row r="1"><c r="A0"/></row></sheetData></worksheet>)"),
        "worksheet cell index should reject zero-row source cell references");
    check(build_index_fails(
              R"(<worksheet><sheetData><row r="1"><c r="XFE1"/></row></sheetData></worksheet>)"),
        "worksheet cell index should reject out-of-limit source columns");
}

void test_cell_index_propagates_event_reader_failures()
{
    check(build_index_fails(
              R"(<worksheet><sheetData><row r="1"><c r="A1"><v>1</f></c></row></sheetData></worksheet>)"),
        "worksheet cell index should propagate malformed value-wrapper failures");

    const std::string oversized =
        R"(<worksheet><sheetData><row r="1"><c r="A1"><v>)" + std::string(80, 'x')
        + R"(</v></c></row></sheetData></worksheet>)";
    fastxlsx::detail::WorksheetEventReaderOptions options;
    options.max_window_bytes = 32;

    bool failed = false;
    try {
        (void)fastxlsx::detail::WorksheetCellIndex::build_from_xml(oversized, options);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "worksheet cell index should preserve the event-reader retained-window guard");
}

} // namespace

int main()
{
    try {
        test_cell_index_maps_exact_source_cell_ranges();
        test_cell_index_chunk_source_matches_materialized_offsets();
        test_cell_index_rejects_ambiguous_or_invalid_source_cells();
        test_cell_index_propagates_event_reader_failures();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
