#include "package_reader.hpp"
#include "zip_entry_name.hpp"
#include "zip_store_writer.hpp"

#include <fastxlsx/workbook.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef FASTXLSX_HAS_MINIZIP_NG
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#endif

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t local_file_header_signature = 0x04034b50u;
constexpr std::uint32_t central_directory_signature = 0x02014b50u;
constexpr std::uint32_t end_of_central_directory_signature = 0x06054b50u;
constexpr std::uint16_t stored_compression_method = 0;
constexpr std::uint16_t deflate_compression_method = 8;
constexpr std::uint16_t encrypted_flag = 0x0001u;
constexpr std::uint16_t data_descriptor_flag = 0x0008u;
constexpr std::uint64_t zip64_u32_sentinel = 0xffffffffull;
constexpr std::uint64_t zip64_u16_sentinel = 0xffffull;
constexpr std::size_t end_of_central_directory_size = 22;
constexpr std::size_t max_zip_comment_size = 0xffff;
constexpr std::size_t central_directory_header_size = 46;
constexpr std::size_t local_file_header_size = 30;
constexpr std::string_view content_types_entry_name = "[Content_Types].xml";
constexpr std::string_view package_relationships_entry_name = "_rels/.rels";
constexpr std::string_view relationship_part_segment = "/_rels/";
constexpr std::string_view root_relationships_prefix = "_rels/";
constexpr std::string_view relationship_part_extension = ".rels";
constexpr std::string_view content_type_worksheet =
    "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml";
constexpr std::string_view relationship_type_worksheet =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";
constexpr std::string_view relationship_type_office_document =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
constexpr std::string_view office_document_relationships_namespace =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

struct XmlAttribute {
    std::string name;
    std::string value;
};

struct XmlStartTag {
    std::string name;
    std::vector<XmlAttribute> attributes;
};

struct XmlNamespaceBinding {
    std::string prefix;
    std::string uri;
};

using XmlNamespaceScope = std::vector<XmlNamespaceBinding>;

struct ParsedRelationships {
    RelationshipSet package_relationships;
    std::vector<std::pair<PartName, RelationshipSet>> part_relationships;
};

#ifdef FASTXLSX_HAS_MINIZIP_NG

struct MinizipReaderDeleter {
    void operator()(void* reader) const
    {
        if (reader != nullptr) {
            mz_zip_reader_delete(&reader);
        }
    }
};

#endif

void require_bytes(std::string_view data, std::size_t offset, std::size_t size,
    const char* message)
{
    if (offset > data.size() || size > data.size() - offset) {
        throw FastXlsxError(message);
    }
}

std::uint16_t read_u16(std::string_view data, std::size_t offset)
{
    require_bytes(data, offset, 2, "ZIP field exceeds available data");
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset + 1])) << 8u)
        | static_cast<unsigned char>(data[offset]));
}

std::uint32_t read_u32(std::string_view data, std::size_t offset)
{
    require_bytes(data, offset, 4, "ZIP field exceeds available data");
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24u)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16u)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8u)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset]));
}

std::uint64_t checked_file_size(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw FastXlsxError("failed to stat XLSX package for reading");
    }
    return static_cast<std::uint64_t>(size);
}

bool is_supported_compression_method(std::uint16_t method) noexcept
{
    if (method == stored_compression_method) {
        return true;
    }
#ifdef FASTXLSX_HAS_MINIZIP_NG
    return method == deflate_compression_method;
#else
    (void)method;
    return false;
#endif
}

[[noreturn]] void throw_unsupported_compression_method()
{
#ifdef FASTXLSX_HAS_MINIZIP_NG
    throw FastXlsxError("PackageReader only supports stored or DEFLATE ZIP entries");
#else
    throw FastXlsxError(
        "PackageReader only supports stored ZIP entries without minizip-ng");
#endif
}

std::streamoff checked_stream_offset(std::uint64_t offset)
{
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw FastXlsxError("ZIP offset exceeds stream offset limit");
    }
    return static_cast<std::streamoff>(offset);
}

std::streamsize checked_stream_size(std::uint64_t size)
{
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw FastXlsxError("ZIP entry chunk is too large for one stream operation");
    }
    return static_cast<std::streamsize>(size);
}

constexpr std::size_t package_reader_io_buffer_size = 64U * 1024U;

const std::array<std::uint32_t, 256>& package_reader_crc32_table()
{
    static constexpr std::uint32_t polynomial = 0xedb88320u;
    static std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values {};
        for (std::uint32_t i = 0; i < values.size(); ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) != 0u ? (crc >> 1u) ^ polynomial : crc >> 1u;
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

class PackageReaderCrc32Accumulator {
public:
    void update(std::string_view data)
    {
        const auto& table = package_reader_crc32_table();
        for (const unsigned char byte : data) {
            value_ = (value_ >> 8u) ^ table[(value_ ^ byte) & 0xffu];
        }
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return value_ ^ 0xffffffffu;
    }

private:
    std::uint32_t value_ = 0xffffffffu;
};

bool is_xml_space(char ch) noexcept
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

void require_xml_text_is_whitespace(
    std::string_view text, bool allow_utf8_bom, const char* message)
{
    std::size_t offset = 0;
    if (allow_utf8_bom && text.substr(0, 3) == "\xef\xbb\xbf") {
        offset = 3;
    }

    for (; offset < text.size(); ++offset) {
        if (!is_xml_space(text[offset])) {
            throw FastXlsxError(message);
        }
    }
}

bool is_xml_name_stop(char ch) noexcept
{
    return is_xml_space(ch) || ch == '/' || ch == '>' || ch == '=';
}

std::string_view local_xml_name(std::string_view name) noexcept
{
    const std::size_t colon = name.rfind(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

std::string_view xml_name_prefix(std::string_view name) noexcept
{
    const std::size_t colon = name.find(':');
    return colon == std::string_view::npos ? std::string_view {} : name.substr(0, colon);
}

int hex_digit_value(char ch) noexcept
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

void append_utf8(std::string& output, std::uint32_t code_point)
{
    if (code_point == 0 || code_point > 0x10ffffu
        || (code_point >= 0xd800u && code_point <= 0xdfffu)) {
        throw FastXlsxError("invalid XML character reference");
    }

    if (code_point <= 0x7fu) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (code_point >> 6u)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else if (code_point <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (code_point >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (code_point >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    }
}

std::uint32_t parse_xml_character_reference(std::string_view entity)
{
    if (entity.size() < 2 || entity.front() != '#') {
        throw FastXlsxError("unknown XML entity reference");
    }

    std::size_t offset = 1;
    int base = 10;
    if (offset < entity.size() && (entity[offset] == 'x' || entity[offset] == 'X')) {
        base = 16;
        ++offset;
    }
    if (offset == entity.size()) {
        throw FastXlsxError("invalid XML character reference");
    }

    std::uint32_t value = 0;
    for (; offset < entity.size(); ++offset) {
        const char ch = entity[offset];
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = ch - 'a' + 10;
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = ch - 'A' + 10;
        }

        if (digit < 0 || digit >= base) {
            throw FastXlsxError("invalid XML character reference");
        }
        if (value > (0x10ffffu - static_cast<std::uint32_t>(digit))
                / static_cast<std::uint32_t>(base)) {
            throw FastXlsxError("XML character reference exceeds Unicode range");
        }
        value = value * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(digit);
    }

    return value;
}

std::string unescape_xml_attribute_value(std::string_view value)
{
    std::string output;
    output.reserve(value.size());

    for (std::size_t offset = 0; offset < value.size(); ++offset) {
        const char ch = value[offset];
        if (ch != '&') {
            output.push_back(ch);
            continue;
        }

        const std::size_t semicolon = value.find(';', offset + 1);
        if (semicolon == std::string_view::npos) {
            throw FastXlsxError("unterminated XML entity reference");
        }
        const std::string_view entity = value.substr(offset + 1, semicolon - offset - 1);
        if (entity == "amp") {
            output.push_back('&');
        } else if (entity == "lt") {
            output.push_back('<');
        } else if (entity == "gt") {
            output.push_back('>');
        } else if (entity == "quot") {
            output.push_back('"');
        } else if (entity == "apos") {
            output.push_back('\'');
        } else {
            append_utf8(output, parse_xml_character_reference(entity));
        }
        offset = semicolon;
    }

    return output;
}

std::size_t find_xml_tag_end(std::string_view xml, std::size_t open_offset)
{
    char quote = '\0';
    for (std::size_t offset = open_offset + 1; offset < xml.size(); ++offset) {
        const char ch = xml[offset];
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
            return offset;
        }
    }

    throw FastXlsxError("XML start tag is not closed");
}

XmlStartTag parse_xml_start_tag(std::string_view tag)
{
    std::size_t offset = 0;
    while (offset < tag.size() && is_xml_space(tag[offset])) {
        ++offset;
    }
    if (offset == tag.size()) {
        throw FastXlsxError("empty XML start tag");
    }

    const std::size_t name_begin = offset;
    while (offset < tag.size() && !is_xml_name_stop(tag[offset])) {
        ++offset;
    }
    if (offset == name_begin) {
        throw FastXlsxError("XML start tag name is missing");
    }

    XmlStartTag start_tag;
    start_tag.name = std::string(tag.substr(name_begin, offset - name_begin));

    while (offset < tag.size()) {
        while (offset < tag.size() && is_xml_space(tag[offset])) {
            ++offset;
        }
        if (offset == tag.size() || tag[offset] == '/') {
            break;
        }

        const std::size_t attribute_name_begin = offset;
        while (offset < tag.size() && !is_xml_name_stop(tag[offset])) {
            ++offset;
        }
        if (offset == attribute_name_begin) {
            throw FastXlsxError("XML attribute name is missing");
        }
        std::string attribute_name =
            std::string(tag.substr(attribute_name_begin, offset - attribute_name_begin));

        while (offset < tag.size() && is_xml_space(tag[offset])) {
            ++offset;
        }
        if (offset == tag.size() || tag[offset] != '=') {
            throw FastXlsxError("XML attribute is missing '='");
        }
        ++offset;
        while (offset < tag.size() && is_xml_space(tag[offset])) {
            ++offset;
        }
        if (offset == tag.size() || (tag[offset] != '"' && tag[offset] != '\'')) {
            throw FastXlsxError("XML attribute value must be quoted");
        }

        const char quote = tag[offset++];
        const std::size_t value_begin = offset;
        while (offset < tag.size() && tag[offset] != quote) {
            ++offset;
        }
        if (offset == tag.size()) {
            throw FastXlsxError("XML attribute value is not closed");
        }

        std::string attribute_value =
            unescape_xml_attribute_value(tag.substr(value_begin, offset - value_begin));
        ++offset;
        start_tag.attributes.push_back(
            XmlAttribute {std::move(attribute_name), std::move(attribute_value)});
    }

    return start_tag;
}

std::vector<XmlStartTag> parse_xml_start_tags(std::string_view xml)
{
    std::vector<XmlStartTag> tags;

    for (std::size_t offset = 0;;) {
        const std::size_t open = xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= xml.size()) {
            throw FastXlsxError("XML tag is truncated");
        }

        if (xml.substr(open, 4) == "<!--") {
            const std::size_t close = xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("XML comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const char marker = xml[open + 1];
        if (marker == '/' || marker == '?' || marker == '!') {
            const std::size_t close = find_xml_tag_end(xml, open);
            offset = close + 1;
            continue;
        }

        const std::size_t close = find_xml_tag_end(xml, open);
        tags.push_back(parse_xml_start_tag(xml.substr(open + 1, close - open - 1)));
        offset = close + 1;
    }

    return tags;
}

bool is_self_closing_start_tag_text(std::string_view tag_text) noexcept;
std::string parse_xml_closing_tag_name(std::string_view tag_text);

std::vector<XmlStartTag> parse_xml_root_child_start_tags(
    std::string_view xml, std::string_view root_name, const char* message)
{
    std::vector<XmlStartTag> children;
    std::vector<std::string> element_stack;
    bool saw_root = false;
    bool closed_root = false;

    std::size_t offset = 0;
    for (;;) {
        const std::size_t open = xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= xml.size()) {
            throw FastXlsxError("XML tag is truncated");
        }
        require_xml_text_is_whitespace(
            xml.substr(offset, open - offset), offset == 0, message);

        if (xml.substr(open, 4) == "<!--") {
            const std::size_t close = xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("XML comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const char marker = xml[open + 1];
        const std::size_t close = find_xml_tag_end(xml, open);
        const std::string_view tag_text = xml.substr(open + 1, close - open - 1);

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        if (marker == '/') {
            if (element_stack.empty()) {
                throw FastXlsxError(message);
            }

            const std::string closing_name = parse_xml_closing_tag_name(tag_text);
            if (closing_name != element_stack.back()) {
                throw FastXlsxError(message);
            }
            element_stack.pop_back();
            if (element_stack.empty()) {
                closed_root = true;
            }
            offset = close + 1;
            continue;
        }

        if (closed_root) {
            throw FastXlsxError(message);
        }

        XmlStartTag tag = parse_xml_start_tag(tag_text);
        const std::string local_name(local_xml_name(tag.name));
        const bool self_closing = is_self_closing_start_tag_text(tag_text);

        if (!saw_root) {
            if (std::string_view(local_name) != root_name) {
                throw FastXlsxError(message);
            }
            saw_root = true;
            if (self_closing) {
                closed_root = true;
            } else {
                element_stack.push_back(tag.name);
            }
            offset = close + 1;
            continue;
        }

        if (element_stack.size() == 1) {
            children.push_back(tag);
        }
        if (!self_closing) {
            element_stack.push_back(tag.name);
        }
        offset = close + 1;
    }

    require_xml_text_is_whitespace(xml.substr(offset), false, message);

    if (!saw_root || !element_stack.empty()) {
        throw FastXlsxError(message);
    }

    return children;
}

const std::string* find_xml_attribute(
    const std::vector<XmlAttribute>& attributes, std::string_view name) noexcept
{
    const auto item = std::find_if(attributes.begin(), attributes.end(),
        [name](const XmlAttribute& attribute) {
            return local_xml_name(attribute.name) == name;
        });
    return item == attributes.end() ? nullptr : &item->value;
}

const std::string& require_xml_attribute(
    const XmlStartTag& tag, std::string_view attribute_name, const char* message)
{
    const auto* attribute = find_xml_attribute(tag.attributes, attribute_name);
    if (attribute == nullptr || attribute->empty()) {
        throw FastXlsxError(message);
    }
    return *attribute;
}

const std::string* find_unqualified_xml_attribute(
    const std::vector<XmlAttribute>& attributes, std::string_view name) noexcept
{
    const auto item = std::find_if(attributes.begin(), attributes.end(),
        [name](const XmlAttribute& attribute) {
            return attribute.name == name;
        });
    return item == attributes.end() ? nullptr : &item->value;
}

const std::string& require_unqualified_xml_attribute(
    const XmlStartTag& tag, std::string_view attribute_name, const char* message)
{
    const auto* attribute = find_unqualified_xml_attribute(tag.attributes, attribute_name);
    if (attribute == nullptr || attribute->empty()) {
        throw FastXlsxError(message);
    }
    return *attribute;
}

void require_metadata_attributes_are_unqualified_and_unique(
    const XmlStartTag& tag, const char* message)
{
    for (const XmlAttribute& attribute : tag.attributes) {
        const std::string_view prefix = xml_name_prefix(attribute.name);
        if (!prefix.empty() && prefix != "xmlns") {
            throw FastXlsxError(message);
        }
        if (!prefix.empty() || attribute.name == "xmlns") {
            continue;
        }

        const auto duplicate = std::find_if(tag.attributes.begin(), tag.attributes.end(),
            [&attribute](const XmlAttribute& other) {
                return &other != &attribute && other.name == attribute.name;
            });
        if (duplicate != tag.attributes.end()) {
            throw FastXlsxError(message);
        }
    }
}

void upsert_namespace_binding(
    XmlNamespaceScope& namespaces, std::string prefix, std::string uri)
{
    const auto existing = std::find_if(namespaces.begin(), namespaces.end(),
        [&prefix](const XmlNamespaceBinding& binding) {
            return binding.prefix == prefix;
        });
    if (existing != namespaces.end()) {
        existing->uri = std::move(uri);
        return;
    }
    namespaces.push_back(XmlNamespaceBinding {std::move(prefix), std::move(uri)});
}

void ingest_namespace_declarations(
    XmlNamespaceScope& namespaces, const XmlStartTag& tag)
{
    for (const XmlAttribute& attribute : tag.attributes) {
        constexpr std::string_view namespace_prefix = "xmlns:";
        if (attribute.name.rfind(namespace_prefix, 0) != 0) {
            continue;
        }

        upsert_namespace_binding(namespaces,
            attribute.name.substr(namespace_prefix.size()), attribute.value);
    }
}

const std::string* find_namespace_uri(
    const std::vector<XmlNamespaceScope>& namespace_stack,
    std::string_view prefix) noexcept
{
    for (auto scope = namespace_stack.rbegin(); scope != namespace_stack.rend(); ++scope) {
        const auto item = std::find_if(scope->begin(), scope->end(),
            [prefix](const XmlNamespaceBinding& binding) {
                return binding.prefix == prefix;
            });
        if (item != scope->end()) {
            return &item->uri;
        }
    }
    return nullptr;
}

const std::string& require_relationship_id_attribute(
    const XmlStartTag& tag, const std::vector<XmlNamespaceScope>& namespace_stack,
    const char* message)
{
    for (const XmlAttribute& attribute : tag.attributes) {
        if (local_xml_name(attribute.name) != "id") {
            continue;
        }

        const std::string_view prefix = xml_name_prefix(attribute.name);
        if (prefix.empty()) {
            continue;
        }

        const std::string* uri = find_namespace_uri(namespace_stack, prefix);
        if (uri == nullptr || *uri != office_document_relationships_namespace) {
            continue;
        }

        if (attribute.value.empty()) {
            throw FastXlsxError(message);
        }
        return attribute.value;
    }

    throw FastXlsxError(message);
}

bool is_self_closing_start_tag_text(std::string_view tag_text) noexcept
{
    std::size_t offset = tag_text.size();
    while (offset > 0 && is_xml_space(tag_text[offset - 1])) {
        --offset;
    }
    return offset > 0 && tag_text[offset - 1] == '/';
}

std::string parse_xml_closing_tag_name(std::string_view tag_text)
{
    std::size_t offset = 0;
    while (offset < tag_text.size() && is_xml_space(tag_text[offset])) {
        ++offset;
    }
    if (offset == tag_text.size() || tag_text[offset] != '/') {
        throw FastXlsxError("XML closing tag is malformed");
    }
    ++offset;
    while (offset < tag_text.size() && is_xml_space(tag_text[offset])) {
        ++offset;
    }

    const std::size_t name_begin = offset;
    while (offset < tag_text.size() && !is_xml_name_stop(tag_text[offset])) {
        ++offset;
    }
    if (offset == name_begin) {
        throw FastXlsxError("XML closing tag name is missing");
    }
    const std::size_t name_end = offset;

    while (offset < tag_text.size()) {
        if (!is_xml_space(tag_text[offset])) {
            throw FastXlsxError("XML closing tag has unexpected content");
        }
        ++offset;
    }

    return std::string(tag_text.substr(name_begin, name_end - name_begin));
}

std::string decode_percent_encoded_relationship_target(std::string_view target)
{
    std::string decoded;
    decoded.reserve(target.size());

    for (std::size_t index = 0; index < target.size(); ++index) {
        const char ch = target[index];
        if (ch != '%') {
            decoded.push_back(ch);
            continue;
        }

        if (index + 2 >= target.size()) {
            throw FastXlsxError("relationship target percent escape is incomplete");
        }

        const int high = hex_digit_value(target[index + 1]);
        const int low = hex_digit_value(target[index + 2]);
        if (high < 0 || low < 0) {
            throw FastXlsxError("relationship target percent escape is invalid");
        }

        const char decoded_char = static_cast<char>((high << 4) | low);
        if (decoded_char == '\0') {
            throw FastXlsxError("relationship target cannot contain null bytes");
        }

        decoded.push_back(decoded_char);
        index += 2;
    }

    return decoded;
}

std::string resolve_internal_relationship_target_path(
    const PartName& source_part, const Relationship& relationship)
{
    if (relationship.target_mode == Relationship::TargetMode::External) {
        throw FastXlsxError("workbook sheet relationship target cannot be external");
    }
    if (relationship.target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError("workbook sheet relationship target must be a package part");
    }

    const std::string decoded_target =
        decode_percent_encoded_relationship_target(relationship.target);
    if (!decoded_target.empty() && decoded_target.front() == '/') {
        return decoded_target;
    }

    const std::string& source = source_part.value();
    const std::size_t slash = source.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/" + decoded_target;
    }
    return source.substr(0, slash) + "/" + decoded_target;
}

std::string read_bytes_at(const std::filesystem::path& path, std::uint64_t offset,
    std::uint64_t size)
{
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw FastXlsxError("ZIP entry is too large to read into memory");
    }
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw FastXlsxError("ZIP entry is too large for one read operation");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to open XLSX package for reading");
    }

    stream.seekg(checked_stream_offset(offset), std::ios::beg);
    if (!stream) {
        throw FastXlsxError("failed to seek inside XLSX package");
    }

    std::string data(static_cast<std::size_t>(size), '\0');
    if (!data.empty()) {
        stream.read(data.data(), static_cast<std::streamsize>(data.size()));
        if (stream.gcount() != static_cast<std::streamsize>(data.size())) {
            throw FastXlsxError("failed to read XLSX package bytes");
        }
    }

    return data;
}

void extract_stored_entry_to_file(const std::filesystem::path& package_path,
    const PackageReaderEntry& entry, const std::filesystem::path& output_path)
{
    std::ifstream input(package_path, std::ios::binary);
    if (!input) {
        throw FastXlsxError("failed to open XLSX package for reading");
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to open package entry extraction output");
    }

    input.seekg(checked_stream_offset(entry.data_offset), std::ios::beg);
    if (!input) {
        throw FastXlsxError("failed to seek inside XLSX package");
    }

    PackageReaderCrc32Accumulator crc;
    std::array<char, package_reader_io_buffer_size> buffer {};
    std::uint64_t remaining = entry.compressed_size;
    while (remaining != 0) {
        const std::uint64_t requested =
            std::min<std::uint64_t>(remaining, buffer.size());
        input.read(buffer.data(), checked_stream_size(requested));
        if (input.gcount() != checked_stream_size(requested)) {
            throw FastXlsxError("failed to read XLSX package bytes");
        }

        const auto chunk_size = static_cast<std::size_t>(requested);
        const std::string_view chunk(buffer.data(), chunk_size);
        crc.update(chunk);
        output.write(buffer.data(), checked_stream_size(requested));
        if (!output) {
            throw FastXlsxError("failed to write package entry extraction output");
        }
        remaining -= requested;
    }

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize package entry extraction output");
    }
    if (crc.value() != entry.crc32) {
        throw FastXlsxError("ZIP entry CRC mismatch");
    }
}

PackageReaderChunkCallback make_stored_entry_chunk_source(
    std::filesystem::path package_path, PackageReaderEntry entry)
{
    struct State {
        std::filesystem::path package_path;
        PackageReaderEntry entry;
        std::ifstream input;
        PackageReaderCrc32Accumulator crc;
        std::uint64_t remaining = 0;
        bool opened = false;
        bool complete = false;
    };

    auto state = std::make_shared<State>();
    state->package_path = std::move(package_path);
    state->entry = std::move(entry);
    state->remaining = state->entry.compressed_size;

    return [state](std::string& output_chunk) -> bool {
        output_chunk.clear();
        if (state->complete) {
            return false;
        }

        if (!state->opened) {
            state->input.open(state->package_path, std::ios::binary);
            if (!state->input) {
                throw FastXlsxError("failed to open XLSX package for reading");
            }
            state->input.seekg(checked_stream_offset(state->entry.data_offset), std::ios::beg);
            if (!state->input) {
                throw FastXlsxError("failed to seek inside XLSX package");
            }
            state->opened = true;
        }

        if (state->remaining == 0) {
            state->input.close();
            if (state->crc.value() != state->entry.crc32) {
                throw FastXlsxError("ZIP entry CRC mismatch");
            }
            state->complete = true;
            return false;
        }

        const std::uint64_t requested =
            std::min<std::uint64_t>(state->remaining, package_reader_io_buffer_size);
        output_chunk.resize(static_cast<std::size_t>(requested));
        state->input.read(output_chunk.data(), checked_stream_size(requested));
        if (state->input.gcount() != checked_stream_size(requested)) {
            throw FastXlsxError("failed to read XLSX package bytes");
        }

        state->crc.update(output_chunk);
        state->remaining -= requested;
        return true;
    };
}

void extract_entry_chunks_to_file(
    PackageReaderChunkCallback source, const std::filesystem::path& output_path)
{
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        throw FastXlsxError("failed to open package entry extraction output");
    }

    std::string chunk;
    while (source(chunk)) {
        output.write(chunk.data(), checked_stream_size(chunk.size()));
        if (!output) {
            throw FastXlsxError("failed to write package entry extraction output");
        }
    }

    output.close();
    if (!output) {
        throw FastXlsxError("failed to finalize package entry extraction output");
    }
}

#ifdef FASTXLSX_HAS_MINIZIP_NG

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void check_minizip_result(int result, const char* operation)
{
    if (result != MZ_OK) {
        throw FastXlsxError(std::string("minizip-ng failed to ") + operation
            + " (error " + std::to_string(result) + ")");
    }
}

void cleanup_open_minizip_reader(void* reader, bool& package_open, bool& entry_open) noexcept
{
    if (reader == nullptr) {
        entry_open = false;
        package_open = false;
        return;
    }

    if (entry_open) {
        (void)mz_zip_reader_entry_close(reader);
        entry_open = false;
    }
    if (package_open) {
        (void)mz_zip_reader_close(reader);
        package_open = false;
    }
}

struct DeflatedEntryChunkSourceState {
    ~DeflatedEntryChunkSourceState()
    {
        cleanup_open_minizip_reader(reader.get(), package_open, entry_open);
    }

    std::filesystem::path package_path;
    PackageReaderEntry entry;
    std::unique_ptr<void, MinizipReaderDeleter> reader;
    PackageReaderCrc32Accumulator crc;
    std::uint64_t emitted_bytes = 0;
    bool package_open = false;
    bool entry_open = false;
    bool complete = false;
};

void close_deflated_entry_chunk_source(DeflatedEntryChunkSourceState& state)
{
    int entry_result = MZ_OK;
    if (state.entry_open) {
        entry_result = mz_zip_reader_entry_close(state.reader.get());
        state.entry_open = false;
    }

    int close_result = MZ_OK;
    if (state.package_open) {
        close_result = mz_zip_reader_close(state.reader.get());
        state.package_open = false;
    }

    if (entry_result == MZ_CRC_ERROR) {
        throw FastXlsxError("ZIP entry CRC mismatch");
    }
    check_minizip_result(entry_result, "close ZIP entry");
    check_minizip_result(close_result, "close XLSX package");
}

void cleanup_deflated_entry_chunk_source(DeflatedEntryChunkSourceState& state) noexcept
{
    cleanup_open_minizip_reader(state.reader.get(), state.package_open, state.entry_open);
}

void open_deflated_entry_chunk_source(DeflatedEntryChunkSourceState& state)
{
    state.reader.reset(mz_zip_reader_create());
    if (!state.reader) {
        throw FastXlsxError("failed to create minizip-ng reader");
    }

    const std::string input_path = path_to_utf8(state.package_path);
    check_minizip_result(
        mz_zip_reader_open_file(state.reader.get(), input_path.c_str()), "open XLSX package");
    state.package_open = true;

    check_minizip_result(
        mz_zip_reader_locate_entry(state.reader.get(), state.entry.name.c_str(), 0),
        "locate ZIP entry");

    mz_zip_file* file_info = nullptr;
    check_minizip_result(
        mz_zip_reader_entry_get_info(state.reader.get(), &file_info), "read ZIP entry info");
    if (file_info == nullptr || file_info->filename == nullptr
        || state.entry.name != file_info->filename) {
        throw FastXlsxError("minizip-ng located an unexpected ZIP entry");
    }
    if (file_info->compression_method != deflate_compression_method) {
        throw FastXlsxError("minizip-ng located entry compression method mismatch");
    }
    if (file_info->uncompressed_size < 0
        || static_cast<std::uint64_t>(file_info->uncompressed_size)
            != state.entry.uncompressed_size) {
        throw FastXlsxError("DEFLATE ZIP entry uncompressed size mismatch");
    }
    if (file_info->crc != state.entry.crc32) {
        throw FastXlsxError("DEFLATE ZIP entry CRC metadata mismatch");
    }

    check_minizip_result(mz_zip_reader_entry_open(state.reader.get()), "open ZIP entry");
    state.entry_open = true;
}

PackageReaderChunkCallback make_deflated_entry_chunk_source(
    std::filesystem::path package_path, PackageReaderEntry entry)
{
    auto state = std::make_shared<DeflatedEntryChunkSourceState>();
    state->package_path = std::move(package_path);
    state->entry = std::move(entry);

    return [state](std::string& output_chunk) -> bool {
        output_chunk.clear();
        if (state->complete) {
            return false;
        }

        if (!state->package_open) {
            try {
                open_deflated_entry_chunk_source(*state);
            } catch (...) {
                cleanup_deflated_entry_chunk_source(*state);
                throw;
            }
        }

        output_chunk.resize(package_reader_io_buffer_size);
        const int read_size = mz_zip_reader_entry_read(state->reader.get(), output_chunk.data(),
            static_cast<std::int32_t>(package_reader_io_buffer_size));
        if (read_size > 0) {
            output_chunk.resize(static_cast<std::size_t>(read_size));
            state->crc.update(output_chunk);
            state->emitted_bytes += static_cast<std::uint64_t>(read_size);
            return true;
        }

        output_chunk.clear();
        if (read_size < 0 && read_size != MZ_END_OF_STREAM) {
            cleanup_deflated_entry_chunk_source(*state);
            check_minizip_result(read_size, "read ZIP entry data");
        }

        close_deflated_entry_chunk_source(*state);
        if (state->emitted_bytes != state->entry.uncompressed_size) {
            throw FastXlsxError("DEFLATE ZIP entry uncompressed size mismatch");
        }
        if (state->crc.value() != state->entry.crc32) {
            throw FastXlsxError("ZIP entry CRC mismatch");
        }
        state->complete = true;
        return false;
    };
}

std::string read_deflated_entry(const std::filesystem::path& path,
    const PackageReaderEntry& entry)
{
    if (entry.uncompressed_size
        > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw FastXlsxError("DEFLATE ZIP entry is too large for one read operation");
    }

    std::unique_ptr<void, MinizipReaderDeleter> reader(mz_zip_reader_create());
    if (!reader) {
        throw FastXlsxError("failed to create minizip-ng reader");
    }

    const std::string input_path = path_to_utf8(path);
    check_minizip_result(
        mz_zip_reader_open_file(reader.get(), input_path.c_str()), "open XLSX package");

    bool found = false;
    std::string data;
    try {
        for (int result = mz_zip_reader_goto_first_entry(reader.get()); result == MZ_OK;
             result = mz_zip_reader_goto_next_entry(reader.get())) {
            mz_zip_file* file_info = nullptr;
            check_minizip_result(
                mz_zip_reader_entry_get_info(reader.get(), &file_info), "read ZIP entry info");
            if (file_info == nullptr || file_info->filename == nullptr
                || entry.name != file_info->filename) {
                continue;
            }

            const int length = mz_zip_reader_entry_save_buffer_length(reader.get());
            if (length < 0) {
                throw FastXlsxError("minizip-ng failed to get ZIP entry length");
            }
            if (static_cast<std::uint64_t>(length) != entry.uncompressed_size) {
                throw FastXlsxError("DEFLATE ZIP entry uncompressed size mismatch");
            }

            data.assign(static_cast<std::size_t>(length), '\0');
            check_minizip_result(
                mz_zip_reader_entry_save_buffer(reader.get(), data.data(), length),
                "read ZIP entry data");
            found = true;
            break;
        }

        check_minizip_result(mz_zip_reader_close(reader.get()), "close XLSX package");
    } catch (...) {
        (void)mz_zip_reader_close(reader.get());
        throw;
    }

    if (!found) {
        throw FastXlsxError("ZIP entry is not present in the package");
    }
    return data;
}

#endif

bool is_relationship_entry(std::string_view name) noexcept
{
    if (name == package_relationships_entry_name) {
        return true;
    }
    if (name.size() <= relationship_part_extension.size()
        || name.substr(name.size() - relationship_part_extension.size())
            != relationship_part_extension) {
        return false;
    }
    return name.find(relationship_part_segment) != std::string_view::npos
        || name.rfind(root_relationships_prefix, 0) == 0;
}

bool is_metadata_entry(std::string_view name) noexcept
{
    return name == content_types_entry_name || is_relationship_entry(name);
}

PartName source_part_for_relationship_entry(std::string_view name)
{
    if (name.rfind(root_relationships_prefix, 0) == 0) {
        const std::string_view rel_name = name.substr(root_relationships_prefix.size());
        if (rel_name.size() <= relationship_part_extension.size()
            || rel_name.substr(rel_name.size() - relationship_part_extension.size())
                != relationship_part_extension) {
            throw FastXlsxError("relationship part extension is invalid");
        }
        return PartName(rel_name.substr(0, rel_name.size() - relationship_part_extension.size()));
    }

    const std::size_t segment = name.find(relationship_part_segment);
    if (segment == std::string_view::npos) {
        throw FastXlsxError("relationship part path is not source-owned");
    }
    const std::size_t rel_name_begin = segment + relationship_part_segment.size();
    const std::string_view rel_name = name.substr(rel_name_begin);
    if (rel_name.size() <= relationship_part_extension.size()
        || rel_name.substr(rel_name.size() - relationship_part_extension.size())
            != relationship_part_extension) {
        throw FastXlsxError("relationship part extension is invalid");
    }

    std::string source_path(name.substr(0, segment));
    if (!source_path.empty()) {
        source_path.push_back('/');
    }
    source_path += rel_name.substr(0, rel_name.size() - relationship_part_extension.size());
    return PartName(source_path);
}

struct EndOfCentralDirectory {
    std::uint64_t offset = 0;
    std::uint16_t entry_count = 0;
    std::uint64_t central_directory_size = 0;
    std::uint64_t central_directory_offset = 0;
};

EndOfCentralDirectory find_end_of_central_directory(
    const std::filesystem::path& path, std::uint64_t file_size)
{
    if (file_size < end_of_central_directory_size) {
        throw FastXlsxError("ZIP package is too small");
    }

    const std::uint64_t tail_size = std::min<std::uint64_t>(
        file_size, end_of_central_directory_size + max_zip_comment_size);
    const std::uint64_t tail_offset = file_size - tail_size;
    const std::string tail = read_bytes_at(path, tail_offset, tail_size);

    for (std::size_t offset = tail.size() - end_of_central_directory_size;; --offset) {
        if (read_u32(tail, offset) == end_of_central_directory_signature) {
            const std::uint16_t comment_size = read_u16(tail, offset + 20);
            if (offset + end_of_central_directory_size + comment_size == tail.size()) {
                const std::uint16_t disk_number = read_u16(tail, offset + 4);
                const std::uint16_t central_directory_disk = read_u16(tail, offset + 6);
                const std::uint16_t disk_entry_count = read_u16(tail, offset + 8);
                const std::uint16_t total_entry_count = read_u16(tail, offset + 10);
                const std::uint32_t central_directory_size = read_u32(tail, offset + 12);
                const std::uint32_t central_directory_offset = read_u32(tail, offset + 16);

                if (disk_number != 0 || central_directory_disk != 0
                    || disk_entry_count != total_entry_count) {
                    throw FastXlsxError("multi-disk ZIP packages are not supported");
                }
                if (total_entry_count == zip64_u16_sentinel
                    || central_directory_size == zip64_u32_sentinel
                    || central_directory_offset == zip64_u32_sentinel) {
                    throw FastXlsxError("Zip64 packages are not supported by PackageReader yet");
                }

                return EndOfCentralDirectory {
                    tail_offset + offset,
                    total_entry_count,
                    central_directory_size,
                    central_directory_offset,
                };
            }
        }

        if (offset == 0) {
            break;
        }
    }

    throw FastXlsxError("ZIP end of central directory not found");
}

void validate_central_directory_bounds(
    const EndOfCentralDirectory& eocd, std::uint64_t file_size)
{
    if (eocd.central_directory_offset > file_size
        || eocd.central_directory_size > file_size - eocd.central_directory_offset) {
        throw FastXlsxError("ZIP central directory is outside the package bounds");
    }

    const std::uint64_t central_directory_end =
        eocd.central_directory_offset + eocd.central_directory_size;
    if (central_directory_end > eocd.offset) {
        throw FastXlsxError("ZIP central directory is outside the package bounds");
    }
    if (central_directory_end != eocd.offset) {
        throw FastXlsxError("ZIP central directory has unsupported trailing data before EOCD");
    }
}

void validate_local_header(const std::filesystem::path& path, const PackageReaderEntry& entry,
    std::uint64_t file_size)
{
    const std::string local_header =
        read_bytes_at(path, entry.local_header_offset, local_file_header_size);
    if (read_u32(local_header, 0) != local_file_header_signature) {
        throw FastXlsxError("ZIP local file header signature mismatch");
    }

    const std::uint16_t flags = read_u16(local_header, 6);
    if ((flags & encrypted_flag) != 0u) {
        throw FastXlsxError("encrypted ZIP entries are not supported");
    }
    if ((flags & data_descriptor_flag) != 0u) {
        throw FastXlsxError("ZIP data descriptors are not supported");
    }
    const std::uint16_t local_compression_method = read_u16(local_header, 8);
    if (local_compression_method != entry.compression_method) {
        throw FastXlsxError("ZIP local file header method mismatch");
    }
    if (!is_supported_compression_method(local_compression_method)) {
        throw_unsupported_compression_method();
    }
    if (read_u32(local_header, 14) != entry.crc32) {
        throw FastXlsxError("ZIP local file header CRC mismatch");
    }

    const std::uint16_t local_name_size = read_u16(local_header, 26);
    const std::uint16_t local_extra_size = read_u16(local_header, 28);
    const std::string local_name = read_bytes_at(
        path, entry.local_header_offset + local_file_header_size, local_name_size);
    if (local_name != entry.name) {
        throw FastXlsxError("ZIP local file header name mismatch");
    }

    if (read_u32(local_header, 18) != entry.compressed_size
        || read_u32(local_header, 22) != entry.uncompressed_size) {
        throw FastXlsxError("ZIP local file header size mismatch");
    }

    const std::uint64_t data_offset =
        entry.local_header_offset + local_file_header_size + local_name_size + local_extra_size;
    if (data_offset > file_size || entry.compressed_size > file_size - data_offset) {
        throw FastXlsxError("ZIP entry data is outside the package bounds");
    }
}

std::vector<PackageReaderEntry> read_central_directory(
    const std::filesystem::path& path, const EndOfCentralDirectory& eocd, std::uint64_t file_size)
{
    const std::string central_directory =
        read_bytes_at(path, eocd.central_directory_offset, eocd.central_directory_size);

    std::size_t offset = 0;
    std::vector<PackageReaderEntry> entries;
    entries.reserve(eocd.entry_count);

    for (std::uint16_t index = 0; index < eocd.entry_count; ++index) {
        require_bytes(central_directory, offset, central_directory_header_size,
            "ZIP central directory header is truncated");
        if (read_u32(central_directory, offset) != central_directory_signature) {
            throw FastXlsxError("ZIP central directory signature mismatch");
        }

        const std::uint16_t flags = read_u16(central_directory, offset + 8);
        const std::uint16_t compression_method = read_u16(central_directory, offset + 10);
        const std::uint32_t crc32 = read_u32(central_directory, offset + 16);
        const std::uint32_t compressed_size = read_u32(central_directory, offset + 20);
        const std::uint32_t uncompressed_size = read_u32(central_directory, offset + 24);
        const std::uint16_t name_size = read_u16(central_directory, offset + 28);
        const std::uint16_t extra_size = read_u16(central_directory, offset + 30);
        const std::uint16_t comment_size = read_u16(central_directory, offset + 32);
        const std::uint16_t disk_start = read_u16(central_directory, offset + 34);
        const std::uint32_t local_header_offset = read_u32(central_directory, offset + 42);

        if ((flags & encrypted_flag) != 0u) {
            throw FastXlsxError("encrypted ZIP entries are not supported");
        }
        if ((flags & data_descriptor_flag) != 0u) {
            throw FastXlsxError("ZIP data descriptors are not supported");
        }
        if (!is_supported_compression_method(compression_method)) {
            throw_unsupported_compression_method();
        }
        if (compression_method == stored_compression_method
            && compressed_size != uncompressed_size) {
            throw FastXlsxError("stored ZIP entry size mismatch");
        }
        if (disk_start != 0) {
            throw FastXlsxError("multi-disk ZIP entries are not supported");
        }
        if (compressed_size == zip64_u32_sentinel || uncompressed_size == zip64_u32_sentinel
            || local_header_offset == zip64_u32_sentinel) {
            throw FastXlsxError("Zip64 entries are not supported by PackageReader yet");
        }

        const std::size_t name_offset = offset + central_directory_header_size;
        const std::size_t record_size =
            central_directory_header_size + name_size + extra_size + comment_size;
        require_bytes(central_directory, offset, record_size,
            "ZIP central directory record is truncated");

        std::string name = central_directory.substr(name_offset, name_size);
        validate_zip_entry_name(name);
        if (std::any_of(entries.begin(), entries.end(),
                [&name](const PackageReaderEntry& entry) { return entry.name == name; })) {
            throw FastXlsxError("duplicate ZIP entry name");
        }

        PackageReaderEntry entry {
            std::move(name),
            compression_method,
            crc32,
            compressed_size,
            uncompressed_size,
            local_header_offset,
            0,
        };
        validate_local_header(path, entry, file_size);

        const std::string local_header =
            read_bytes_at(path, entry.local_header_offset, local_file_header_size);
        entry.data_offset = entry.local_header_offset + local_file_header_size
            + read_u16(local_header, 26) + read_u16(local_header, 28);

        entries.push_back(std::move(entry));
        offset += record_size;
    }

    if (offset != central_directory.size()) {
        throw FastXlsxError("ZIP central directory has trailing data");
    }

    return entries;
}

ContentTypesManifest parse_content_types(std::string_view xml)
{
    ContentTypesManifest manifest;

    for (const XmlStartTag& tag : parse_xml_root_child_start_tags(
             xml, "Types", "content types XML root is missing")) {
        const std::string_view name = local_xml_name(tag.name);
        require_metadata_attributes_are_unqualified_and_unique(
            tag, "content type metadata attributes must be unqualified and unique");
        if (name == "Default") {
            manifest.add_default(
                require_unqualified_xml_attribute(tag, "Extension",
                    "content type default is missing Extension"),
                require_unqualified_xml_attribute(tag, "ContentType",
                    "content type default is missing ContentType"));
        } else if (name == "Override") {
            manifest.add_override(
                PartName(require_unqualified_xml_attribute(tag, "PartName",
                    "content type override is missing PartName")),
                require_unqualified_xml_attribute(tag, "ContentType",
                    "content type override is missing ContentType"));
        } else {
            throw FastXlsxError("content types XML contains unsupported element");
        }
    }

    return manifest;
}

RelationshipSet parse_relationships(std::string_view xml)
{
    RelationshipSet relationships;

    for (const XmlStartTag& tag : parse_xml_root_child_start_tags(
             xml, "Relationships", "relationships XML root is missing")) {
        const std::string_view name = local_xml_name(tag.name);
        if (name != "Relationship") {
            throw FastXlsxError("relationships XML contains unsupported element");
        }
        require_metadata_attributes_are_unqualified_and_unique(
            tag, "relationship metadata attributes must be unqualified and unique");

        Relationship::TargetMode target_mode = Relationship::TargetMode::Internal;
        if (const auto* target_mode_text =
                find_unqualified_xml_attribute(tag.attributes, "TargetMode")) {
            if (*target_mode_text == "External") {
                target_mode = Relationship::TargetMode::External;
            } else if (*target_mode_text != "Internal") {
                throw FastXlsxError("relationship TargetMode is not supported");
            }
        }

        relationships.add(Relationship {
            require_unqualified_xml_attribute(tag, "Id", "relationship is missing Id"),
            require_unqualified_xml_attribute(tag, "Type", "relationship is missing Type"),
            require_unqualified_xml_attribute(tag, "Target", "relationship is missing Target"),
            target_mode,
        });
    }

    return relationships;
}

PartName resolve_workbook_part_from_package_relationships(
    const RelationshipSet& package_relationships)
{
    const Relationship* workbook_relationship = nullptr;
    for (const Relationship& relationship : package_relationships.relationships()) {
        if (relationship.type != relationship_type_office_document) {
            continue;
        }
        if (workbook_relationship != nullptr) {
            throw FastXlsxError(
                "workbook sheet catalog has multiple officeDocument relationships");
        }
        workbook_relationship = &relationship;
    }

    if (workbook_relationship == nullptr) {
        throw FastXlsxError(
            "workbook sheet catalog requires package officeDocument relationship");
    }
    if (workbook_relationship->target_mode == Relationship::TargetMode::External) {
        throw FastXlsxError(
            "workbook sheet catalog officeDocument target cannot be external");
    }
    if (workbook_relationship->target.find_first_of("?#") != std::string::npos) {
        throw FastXlsxError(
            "workbook sheet catalog officeDocument target must be a package part");
    }

    std::string workbook_target =
        decode_percent_encoded_relationship_target(workbook_relationship->target);
    if (workbook_target.empty()) {
        throw FastXlsxError(
            "workbook sheet catalog officeDocument target must be a package part");
    }
    if (workbook_target.front() != '/') {
        workbook_target.insert(workbook_target.begin(), '/');
    }

    const PartName workbook_part(workbook_target);
    if (workbook_part != PartName("/xl/workbook.xml")) {
        throw FastXlsxError(
            "workbook sheet catalog only supports officeDocument target xl/workbook.xml");
    }
    return workbook_part;
}

std::vector<WorkbookSheetReference> parse_workbook_sheets(
    std::string_view workbook_xml, const RelationshipSet& workbook_relationships,
    const PartIndex& part_index)
{
    const PartName workbook_part("/xl/workbook.xml");
    std::vector<WorkbookSheetReference> sheets;
    std::vector<XmlNamespaceScope> namespace_stack;
    bool inside_workbook_root = false;
    bool inside_sheets = false;
    bool saw_sheets = false;
    std::size_t sheets_child_depth = 0;

    const auto add_sheet = [&](const XmlStartTag& tag,
                               const std::vector<XmlNamespaceScope>& sheet_namespaces) {
        const std::string& name =
            require_unqualified_xml_attribute(tag, "name", "workbook sheet is missing name");
        const std::string& sheet_id =
            require_unqualified_xml_attribute(
                tag, "sheetId", "workbook sheet is missing sheetId");
        const std::string& relationship_id = require_relationship_id_attribute(
            tag, sheet_namespaces, "workbook sheet is missing relationship id");

        const Relationship* relationship = workbook_relationships.find_by_id(relationship_id);
        if (relationship == nullptr) {
            throw FastXlsxError("workbook sheet relationship id is not present in workbook .rels");
        }
        if (relationship->type != relationship_type_worksheet) {
            throw FastXlsxError("workbook sheet relationship is not a worksheet relationship");
        }

        const PartName worksheet_part(
            resolve_internal_relationship_target_path(workbook_part, *relationship));
        const PackagePart* package_part = part_index.find_part(worksheet_part);
        if (package_part == nullptr) {
            throw FastXlsxError("workbook sheet relationship targets an unknown part");
        }
        if (package_part->content_type != content_type_worksheet) {
            throw FastXlsxError("workbook sheet relationship target is not a worksheet part");
        }

        sheets.push_back(WorkbookSheetReference {
            name,
            sheet_id,
            relationship_id,
            worksheet_part,
        });
    };

    for (std::size_t offset = 0;;) {
        const std::size_t open = workbook_xml.find('<', offset);
        if (open == std::string_view::npos) {
            break;
        }
        if (open + 1 >= workbook_xml.size()) {
            throw FastXlsxError("XML tag is truncated");
        }

        if (workbook_xml.substr(open, 4) == "<!--") {
            const std::size_t close = workbook_xml.find("-->", open + 4);
            if (close == std::string_view::npos) {
                throw FastXlsxError("XML comment is not closed");
            }
            offset = close + 3;
            continue;
        }

        const std::size_t close = find_xml_tag_end(workbook_xml, open);
        const std::string_view tag_text =
            workbook_xml.substr(open + 1, close - open - 1);
        const char marker = workbook_xml[open + 1];

        if (marker == '/') {
            const std::string closing_name = parse_xml_closing_tag_name(tag_text);
            if (inside_sheets) {
                if (sheets_child_depth == 0
                    && local_xml_name(closing_name) == "sheets") {
                    inside_sheets = false;
                    break;
                }
                if (sheets_child_depth > 0) {
                    --sheets_child_depth;
                }
            }
            if (namespace_stack.size() == 1
                && local_xml_name(closing_name) == "workbook") {
                inside_workbook_root = false;
            }
            if (namespace_stack.empty()) {
                throw FastXlsxError("XML closing tag has no matching start tag");
            }
            namespace_stack.pop_back();
            offset = close + 1;
            continue;
        }

        if (marker == '?' || marker == '!') {
            offset = close + 1;
            continue;
        }

        XmlStartTag tag = parse_xml_start_tag(tag_text);
        XmlNamespaceScope current_scope;
        ingest_namespace_declarations(current_scope, tag);
        std::vector<XmlNamespaceScope> current_namespaces = namespace_stack;
        current_namespaces.push_back(current_scope);

        const bool self_closing = is_self_closing_start_tag_text(tag_text);
        const std::string_view local_name = local_xml_name(tag.name);
        const std::size_t element_depth = namespace_stack.size();
        if (!inside_sheets) {
            if (inside_workbook_root && element_depth == 1 && local_name == "sheets") {
                saw_sheets = true;
                if (!self_closing) {
                    inside_sheets = true;
                    sheets_child_depth = 0;
                }
            }
        } else {
            if (sheets_child_depth == 0 && local_name == "sheet") {
                add_sheet(tag, current_namespaces);
            }
            if (!self_closing) {
                ++sheets_child_depth;
            }
        }

        if (!self_closing) {
            if (element_depth == 0 && local_name == "workbook") {
                inside_workbook_root = true;
            }
            namespace_stack.push_back(std::move(current_scope));
        }
        offset = close + 1;
    }

    if (!saw_sheets) {
        throw FastXlsxError("workbook sheet catalog is missing sheets element");
    }

    if (sheets.empty()) {
        throw FastXlsxError("workbook sheet catalog is empty");
    }
    return sheets;
}

PartName worksheet_part_by_sheet_name_from_sheets(
    const std::vector<WorkbookSheetReference>& sheets, std::string_view sheet_name)
{
    const WorkbookSheetReference* match = nullptr;
    for (const WorkbookSheetReference& sheet : sheets) {
        if (sheet.name != sheet_name) {
            continue;
        }
        if (match != nullptr) {
            throw FastXlsxError("workbook sheet name is ambiguous");
        }
        match = &sheet;
    }

    if (match == nullptr) {
        throw FastXlsxError("workbook sheet name is not present");
    }
    return match->part_name;
}

ContentTypesManifest read_content_types(const PackageReader& reader)
{
    if (reader.find_entry(content_types_entry_name) == nullptr) {
        throw FastXlsxError("XLSX package is missing [Content_Types].xml");
    }
    return parse_content_types(reader.read_entry(content_types_entry_name));
}

void copy_content_types_into_index(
    const ContentTypesManifest& content_types, PartIndex& part_index)
{
    for (const ContentTypeDefault& item : content_types.defaults()) {
        part_index.content_types().add_default(item.extension, item.content_type);
    }
    for (const ContentTypeOverride& item : content_types.overrides()) {
        part_index.content_types().add_override(item.part_name, item.content_type);
    }
}

PartIndex build_part_index(const std::vector<PackageReaderEntry>& entries,
    const ContentTypesManifest& content_types)
{
    PartIndex part_index;
    copy_content_types_into_index(content_types, part_index);

    for (const PackageReaderEntry& entry : entries) {
        if (is_metadata_entry(entry.name)) {
            continue;
        }

        PartName part_name(entry.name);
        const std::string* content_type = content_types.content_type_for(part_name);
        part_index.ensure_part(
            std::move(part_name), content_type == nullptr ? std::string() : *content_type);
    }

    return part_index;
}

ParsedRelationships read_relationships(const PackageReader& reader, const PartIndex& part_index)
{
    ParsedRelationships parsed;

    for (const PackageReaderEntry& entry : reader.entries()) {
        if (!is_relationship_entry(entry.name)) {
            continue;
        }

        RelationshipSet relationships = parse_relationships(reader.read_entry(entry.name));
        if (entry.name == package_relationships_entry_name) {
            for (const Relationship& relationship : relationships.relationships()) {
                parsed.package_relationships.add(relationship);
            }
            continue;
        }

        PartName source_part = source_part_for_relationship_entry(entry.name);
        if (part_index.find_part(source_part) == nullptr) {
            throw FastXlsxError("relationship source part is not present in the package");
        }
        parsed.part_relationships.push_back(
            {std::move(source_part), std::move(relationships)});
    }

    return parsed;
}

} // namespace

PackageReader PackageReader::open(std::filesystem::path path)
{
    const std::uint64_t file_size = checked_file_size(path);
    const EndOfCentralDirectory eocd = find_end_of_central_directory(path, file_size);
    validate_central_directory_bounds(eocd, file_size);

    PackageReader reader;
    reader.entries_ = read_central_directory(path, eocd, file_size);
    reader.path_ = std::move(path);
    reader.content_types_ = read_content_types(reader);
    reader.part_index_ = build_part_index(reader.entries_, reader.content_types_);
    ParsedRelationships relationships = read_relationships(reader, reader.part_index_);
    reader.package_relationships_ = std::move(relationships.package_relationships);
    reader.part_relationships_ = std::move(relationships.part_relationships);
    return reader;
}

const std::filesystem::path& PackageReader::path() const noexcept
{
    return path_;
}

const std::vector<PackageReaderEntry>& PackageReader::entries() const noexcept
{
    return entries_;
}

const PackageReaderEntry* PackageReader::find_entry(std::string_view name) const noexcept
{
    const auto item = std::find_if(entries_.begin(), entries_.end(),
        [name](const PackageReaderEntry& entry) { return entry.name == name; });
    return item == entries_.end() ? nullptr : &*item;
}

std::string PackageReader::read_entry(std::string_view name) const
{
    const auto* entry = find_entry(name);
    if (entry == nullptr) {
        throw FastXlsxError("ZIP entry is not present in the package");
    }

    std::string data;
    if (entry->compression_method == stored_compression_method) {
        data = read_bytes_at(path_, entry->data_offset, entry->compressed_size);
#ifdef FASTXLSX_HAS_MINIZIP_NG
    } else if (entry->compression_method == deflate_compression_method) {
        data = read_deflated_entry(path_, *entry);
#endif
    } else {
        throw_unsupported_compression_method();
    }
    if (crc32(data) != entry->crc32) {
        throw FastXlsxError("ZIP entry CRC mismatch");
    }
    return data;
}

PackageReaderChunkCallback PackageReader::entry_chunk_source(std::string_view name) const
{
    const auto* entry = find_entry(name);
    if (entry == nullptr) {
        throw FastXlsxError("ZIP entry is not present in the package");
    }

    if (entry->compression_method == stored_compression_method) {
        return make_stored_entry_chunk_source(path_, *entry);
    }

#ifdef FASTXLSX_HAS_MINIZIP_NG
    if (entry->compression_method == deflate_compression_method) {
        return make_deflated_entry_chunk_source(path_, *entry);
    }
#endif

    throw_unsupported_compression_method();
}

void PackageReader::extract_entry_to_file(
    std::string_view name, const std::filesystem::path& output_path) const
{
    const auto* entry = find_entry(name);
    if (entry == nullptr) {
        throw FastXlsxError("ZIP entry is not present in the package");
    }

    if (entry->compression_method == stored_compression_method) {
        extract_stored_entry_to_file(path_, *entry, output_path);
        return;
    }

#ifdef FASTXLSX_HAS_MINIZIP_NG
    if (entry->compression_method == deflate_compression_method) {
        extract_entry_chunks_to_file(entry_chunk_source(name), output_path);
        return;
    }
#endif

    throw_unsupported_compression_method();
}

const ContentTypesManifest& PackageReader::content_types() const noexcept
{
    return content_types_;
}

const PartIndex& PackageReader::part_index() const noexcept
{
    return part_index_;
}

const RelationshipSet& PackageReader::package_relationships() const noexcept
{
    return package_relationships_;
}

const RelationshipSet* PackageReader::relationships_for(
    const PartName& source_part) const noexcept
{
    const auto item = std::find_if(part_relationships_.begin(), part_relationships_.end(),
        [&source_part](const auto& value) { return value.first == source_part; });
    return item == part_relationships_.end() ? nullptr : &item->second;
}

RelationshipGraph PackageReader::relationship_graph() const
{
    RelationshipGraph graph(part_index_);
    for (const Relationship& relationship : package_relationships_.relationships()) {
        graph.add_package_relationship(relationship);
    }
    for (const auto& [source_part, relationships] : part_relationships_) {
        for (const Relationship& relationship : relationships.relationships()) {
            graph.add_relationship(source_part, relationship);
        }
    }
    return graph;
}

std::vector<WorkbookSheetReference> PackageReader::workbook_sheets() const
{
    const PartName workbook_part =
        resolve_workbook_part_from_package_relationships(package_relationships_);
    if (find_entry(workbook_part.zip_path()) == nullptr) {
        throw FastXlsxError("workbook sheet catalog requires xl/workbook.xml");
    }

    const RelationshipSet* workbook_relationships = relationships_for(workbook_part);
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook sheet catalog requires workbook relationships");
    }

    return parse_workbook_sheets(
        read_entry(workbook_part.zip_path()), *workbook_relationships, part_index_);
}

std::vector<WorkbookSheetReference> PackageReader::workbook_sheets_from_xml(
    std::string_view workbook_xml) const
{
    const PartName workbook_part =
        resolve_workbook_part_from_package_relationships(package_relationships_);

    const RelationshipSet* workbook_relationships = relationships_for(workbook_part);
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook sheet catalog requires workbook relationships");
    }

    return parse_workbook_sheets(workbook_xml, *workbook_relationships, part_index_);
}

PartName PackageReader::worksheet_part_by_sheet_name(std::string_view sheet_name) const
{
    return worksheet_part_by_sheet_name_from_sheets(workbook_sheets(), sheet_name);
}

PartName PackageReader::worksheet_part_by_sheet_name_from_xml(
    std::string_view sheet_name, std::string_view workbook_xml) const
{
    return worksheet_part_by_sheet_name_from_sheets(
        workbook_sheets_from_xml(workbook_xml), sheet_name);
}

} // namespace fastxlsx::detail
