#include <fastxlsx/detail/worksheet_comment_writer.hpp>

#include <fastxlsx/detail/xml.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fastxlsx::detail {
namespace {

bool needs_space_preserve(std::string_view value) noexcept
{
    return !value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n'
            || value.front() == '\r' || value.back() == ' ' || value.back() == '\t'
            || value.back() == '\n' || value.back() == '\r');
}

std::vector<std::size_t> build_author_ids(
    std::span<const ClassicNote> notes, std::vector<std::string_view>& authors)
{
    std::unordered_map<std::string_view, std::size_t> author_ids;
    author_ids.reserve(notes.size());
    authors.reserve(notes.size());

    std::vector<std::size_t> note_author_ids;
    note_author_ids.reserve(notes.size());
    for (const ClassicNote& note : notes) {
        const auto [iterator, inserted] = author_ids.emplace(note.author, authors.size());
        if (inserted) {
            authors.push_back(note.author);
        }
        note_author_ids.push_back(iterator->second);
    }
    return note_author_ids;
}

void append_simple_text(std::string& xml, std::string_view text)
{
    if (needs_space_preserve(text)) {
        xml += R"(<t xml:space="preserve">)";
    } else {
        xml += "<t>";
    }
    append_escaped_xml_text(xml, text);
    xml += "</t>";
}

} // namespace

std::string serialize_classic_comments(std::span<const ClassicNote> notes)
{
    if (notes.empty()) {
        throw FastXlsxError("classic comments serializer requires at least one note");
    }

    std::vector<std::string_view> authors;
    const std::vector<std::size_t> author_ids = build_author_ids(notes, authors);

    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<comments xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><authors>)";
    for (const std::string_view author : authors) {
        xml += "<author>";
        append_escaped_xml_text(xml, author);
        xml += "</author>";
    }
    xml += "</authors><commentList>";
    for (std::size_t index = 0; index < notes.size(); ++index) {
        const ClassicNote& note = notes[index];
        xml += R"(<comment ref=")";
        append_cell_reference(xml, note.row, note.column);
        xml += R"(" authorId=")";
        append_unsigned_decimal(xml, static_cast<std::uint64_t>(author_ids[index]));
        xml += R"("><text>)";
        append_simple_text(xml, note.text);
        xml += "</text></comment>";
    }
    xml += "</commentList></comments>";
    return xml;
}

std::string serialize_classic_note_vml(std::span<const ClassicNote> notes)
{
    if (notes.empty()) {
        throw FastXlsxError("classic note VML serializer requires at least one note");
    }

    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<xml xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:x="urn:schemas-microsoft-com:office:excel">)";
    xml += R"(<o:shapelayout v:ext="edit"><o:idmap v:ext="edit" data="1"/></o:shapelayout>)";
    xml += R"(<v:shapetype id="_x0000_t202" coordsize="21600,21600" o:spt="202" path="m,l,21600r21600,l21600,xe"><v:stroke joinstyle="miter"/><v:path gradientshapeok="t" o:connecttype="rect"/></v:shapetype>)";

    for (std::size_t index = 0; index < notes.size(); ++index) {
        const ClassicNote& note = notes[index];
        xml += R"(<v:shape id="_x0000_s)";
        append_unsigned_decimal(xml, static_cast<std::uint64_t>(1025U + index));
        xml += R"(" type="#_x0000_t202" style="position:absolute;margin-left:59.25pt;margin-top:1.5pt;width:96pt;height:55.5pt;z-index:)";
        append_unsigned_decimal(xml, static_cast<std::uint64_t>(index + 1));
        xml += R"(;visibility:hidden" fillcolor="#ffffe1" o:insetmode="auto">)";
        xml += R"(<v:fill color2="#ffffe1"/><v:shadow on="t" color="black" obscured="t"/><v:path o:connecttype="none"/><v:textbox style="mso-direction-alt:auto"><div style="text-align:left"></div></v:textbox><x:ClientData ObjectType="Note"><x:MoveWithCells/><x:SizeWithCells/><x:AutoFill>False</x:AutoFill><x:Row>)";
        append_unsigned_decimal(xml, static_cast<std::uint64_t>(note.row - 1));
        xml += "</x:Row><x:Column>";
        append_unsigned_decimal(xml, static_cast<std::uint64_t>(note.column - 1));
        xml += "</x:Column></x:ClientData></v:shape>";
    }
    xml += "</xml>";
    return xml;
}

} // namespace fastxlsx::detail
